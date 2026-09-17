#include "hatt/electrical/Circuit.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <string_view>

namespace hatt::electrical {

bool parseSpiceValue(const std::string& text, double& value) {
    // Only SI/SPICE scale suffixes are accepted; unit labels are deliberately rejected.
    std::string_view input(text);
    const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!input.empty() && space(input.front())) input.remove_prefix(1);
    while (!input.empty() && space(input.back())) input.remove_suffix(1);
    if (input.empty()) return false;
    std::size_t i = 0;
    if (input[i] == '+' || input[i] == '-') ++i;
    const auto digit = [](char c) { return c >= '0' && c <= '9'; };
    std::size_t digits = 0;
    while (i < input.size() && digit(input[i])) { ++i; ++digits; }
    if (i < input.size() && input[i] == '.') {
        ++i;
        while (i < input.size() && digit(input[i])) { ++i; ++digits; }
    }
    if (digits == 0) return false;
    if (i < input.size() && (input[i] == 'e' || input[i] == 'E')) {
        ++i;
        if (i < input.size() && (input[i] == '+' || input[i] == '-')) ++i;
        const std::size_t start = i;
        while (i < input.size() && digit(input[i])) ++i;
        if (i == start) return false;
    }
    auto number = input.substr(0, i);
    if (number.front() == '+') number.remove_prefix(1);
    double parsed = 0;
    const auto result = std::from_chars(number.data(), number.data() + number.size(), parsed,
                                        std::chars_format::general);
    if (result.ec != std::errc{} || result.ptr != number.data() + number.size()) return false;
    std::string suffix(input.substr(i));
    for (char& c : suffix) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    double scale = 1;
    if (suffix == "p") scale = 1e-12;
    else if (suffix == "n") scale = 1e-9;
    else if (suffix == "u") scale = 1e-6;
    else if (suffix == "m") scale = 1e-3;
    else if (suffix == "k") scale = 1e3;
    else if (suffix == "meg") scale = 1e6;
    else if (suffix == "g") scale = 1e9;
    else if (!suffix.empty()) return false;
    const double scaled = parsed * scale;
    if (!std::isfinite(scaled) || (parsed != 0 && scaled == 0)) return false;
    value = scaled;
    return true;
}

DcResult solveDc(const DcCircuit& circuit, const std::atomic_bool* cancelled) {
    const auto stopped = [cancelled] { return cancelled && cancelled->load(std::memory_order_relaxed); };
    const auto failure = [](const std::string& message) {
        DcResult result;
        result.error = message;
        return result;
    };
    if (stopped()) return failure("DC analysis cancelled.");
    if (circuit.netCount <= 0 || circuit.elements.empty()) return failure("Circuit is empty.");
    if (circuit.ground < 0 || circuit.ground >= circuit.netCount)
        return failure("Circuit has no valid ground net (0).");
    if (circuit.netCount > 257) return failure("DC analysis supports at most 256 unknowns.");
    int sources = 0;
    // Conducting adjacency, and adjacency that also crosses capacitors.
    std::vector<std::vector<int>> adjacent(static_cast<std::size_t>(circuit.netCount));
    std::vector<std::vector<int>> coupled(static_cast<std::size_t>(circuit.netCount));
    for (const auto& element : circuit.elements) {
        if (stopped()) return failure("DC analysis cancelled.");
        if (element.positive < 0 || element.positive >= circuit.netCount ||
            element.negative < 0 || element.negative >= circuit.netCount)
            return failure("Invalid net index for " + element.reference + ".");
        if (!std::isfinite(element.value)) return failure("Non-finite value for " + element.reference + ".");
        if (element.kind == DcKind::Resistor) {
            if (element.value <= 0 || !std::isfinite(1 / element.value))
                return failure("Resistance must be finite and strictly positive: " + element.reference + ".");
        } else if (element.kind == DcKind::VoltageSource || element.kind == DcKind::Inductor) {
            if (++sources + circuit.netCount - 1 > 256)
                return failure("DC analysis supports at most 256 unknowns.");
        } else if (element.kind != DcKind::Capacitor && element.kind != DcKind::CurrentSource) {
            return failure("Unsupported DC element: " + element.reference + ".");
        }
        coupled[element.positive].push_back(element.negative);
        coupled[element.negative].push_back(element.positive);
        if (element.kind == DcKind::Capacitor || element.kind == DcKind::CurrentSource) continue;
        adjacent[element.positive].push_back(element.negative);
        adjacent[element.negative].push_back(element.positive);
    }
    const auto reach = [&](const std::vector<std::vector<int>>& graph) {
        std::vector<bool> reachable(static_cast<std::size_t>(circuit.netCount), false);
        std::vector<int> pending{circuit.ground};
        reachable[circuit.ground] = true;
        for (std::size_t i = 0; i < pending.size(); ++i) {
            for (int net : graph[pending[i]]) if (!reachable[net]) {
                reachable[net] = true;
                pending.push_back(net);
            }
        }
        return reachable;
    };
    if (stopped()) return failure("DC analysis cancelled.");
    const std::vector<bool> conducting = reach(adjacent);
    const std::vector<bool> reachable = reach(coupled);
    for (int net = 0; net < circuit.netCount; ++net)
        if (!reachable[net]) return failure("Floating net " + std::to_string(net) + ": no path to ground.");

    const int nodeUnknowns = circuit.netCount - 1;
    const int n = nodeUnknowns + sources;
    const auto nodeIndex = [&circuit](int net) {
        return net == circuit.ground ? -1 : net - (net > circuit.ground ? 1 : 0);
    };
    std::vector<std::vector<double>> a(n, std::vector<double>(n + 1, 0));
    int source = nodeUnknowns;
    constexpr double gmin = 1e-12;
    for (int net = 0; net < circuit.netCount; ++net) {
        if (!conducting[net]) a[nodeIndex(net)][nodeIndex(net)] += gmin;
    }
    for (const auto& element : circuit.elements) {
        if (stopped()) return failure("DC analysis cancelled.");
        const int p = nodeIndex(element.positive), m = nodeIndex(element.negative);
        if (element.kind == DcKind::Capacitor) continue;
        if (element.kind == DcKind::Resistor) {
            // A resistor whose pins share a net contributes no conductance.
            if (p == m) continue;
            const double g = 1 / element.value;
            if (p >= 0) a[p][p] += g;
            if (m >= 0) a[m][m] += g;
            if (p >= 0 && m >= 0) { a[p][m] -= g; a[m][p] -= g; }
        } else if (element.kind == DcKind::CurrentSource) {
            if (p >= 0) a[p][n] -= element.value;
            if (m >= 0) a[m][n] += element.value;
        } else {
            if (p >= 0) { a[p][source] += 1; a[source][p] += 1; }
            if (m >= 0) { a[m][source] -= 1; a[source][m] -= 1; }
            a[source][n] = element.kind == DcKind::Inductor ? 0.0 : element.value;
            ++source;
        }
    }
    // Normalize rows before partial pivoting so small conductances are not treated as zero.
    for (auto& row : a) {
        double scale = 0;
        for (int col = 0; col <= n; ++col) {
            if (!std::isfinite(row[col])) return failure("DC matrix exceeds numerical range.");
            if (col < n) scale = std::max(scale, std::abs(row[col]));
        }
        if (scale == 0) return failure("Singular DC circuit: redundant or conflicting voltage sources.");
        for (double& entry : row) entry /= scale;
    }
    const double tolerance = std::numeric_limits<double>::epsilon() * std::max(n, 1) * 8;
    for (int col = 0; col < n; ++col) {
        if (stopped()) return failure("DC analysis cancelled.");
        int pivot = col;
        for (int row = col + 1; row < n; ++row)
            if (std::abs(a[row][col]) > std::abs(a[pivot][col])) pivot = row;
        if (std::abs(a[pivot][col]) <= tolerance)
            return failure("Singular or ill-conditioned DC circuit: check voltage sources and floating nodes.");
        std::swap(a[pivot], a[col]);
        for (int row = col + 1; row < n; ++row) {
            const double factor = a[row][col] / a[col][col];
            a[row][col] = 0;
            for (int k = col + 1; k <= n; ++k) a[row][k] -= factor * a[col][k];
        }
    }
    std::vector<double> solution(n, 0);
    for (int row = n - 1; row >= 0; --row) {
        if (stopped()) return failure("DC analysis cancelled.");
        double rhs = a[row][n];
        for (int col = row + 1; col < n; ++col) rhs -= a[row][col] * solution[col];
        solution[row] = rhs / a[row][row];
        if (!std::isfinite(solution[row])) return failure("DC solution exceeds numerical range.");
    }
    DcResult result;
    result.voltages.resize(circuit.netCount, 0);
    for (int net = 0; net < circuit.netCount; ++net)
        if (net != circuit.ground) result.voltages[net] = solution[nodeIndex(net)];
    source = nodeUnknowns;
    for (const auto& element : circuit.elements) {
        if (stopped()) return failure("DC analysis cancelled.");
        const double current = element.kind == DcKind::Capacitor ? 0.0
            : element.kind == DcKind::CurrentSource ? element.value
            : element.kind == DcKind::Resistor
            ? (result.voltages[element.positive] - result.voltages[element.negative]) / element.value
            : solution[source++];
        if (!std::isfinite(current)) return failure("DC current exceeds numerical range.");
        result.currents.push_back(current);
    }
    result.success = true;
    return result;
}

} // namespace hatt::electrical
