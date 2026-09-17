#include "hatt/electrical/Circuit.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <optional>
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

namespace {

// kT/q at ~300 K, the common SPICE default thermal voltage.
constexpr double ThermalVoltage = 0.025852;

// Ideal-diode current and its derivative (conductance) at junction voltage v = Vanode - Vcathode.
// The exponent is capped well below double overflow; damped Newton-Raphson (limitJunctionVoltage)
// keeps the iteration from ever needing a much larger one for a converging circuit.
void diodeCurrent(double v, double is, double n, double& current, double& conductance) {
    const double vt = n * ThermalVoltage;
    const double exponent = std::min(v / vt, 80.0);
    const double e = std::exp(exponent);
    current = is * (e - 1.0);
    conductance = is * e / vt;
}

// Diode equation above the breakdown knee (v > -vz); below it, a linear breakdown branch
// continuous with the forward/leakage branch at v = -vz (current there is ~ -is).
void zenerCurrent(double v, double is, double n, double vz, double rz, double& current,
                   double& conductance) {
    if (v > -vz) {
        diodeCurrent(v, is, n, current, conductance);
        return;
    }
    current = -is + (v + vz) / rz;
    conductance = 1.0 / rz;
}

// Limits how far a junction voltage guess may move in one Newton iteration so exp() never
// overflows and the iteration does not overshoot into a wildly wrong region.
double limitJunctionVoltage(double previous, double proposed, double n) {
    const double step = 4.0 * n * ThermalVoltage;
    if (proposed - previous > step) return previous + step;
    if (previous - proposed > step) return previous - step;
    return proposed;
}

// Row-normalized, partial-pivot Gaussian elimination on the n x (n+1) augmented matrix `a`
// (consumed by value: each Newton iteration and continuation stage solves its own copy).
std::optional<std::vector<double>> gaussianSolve(std::vector<std::vector<double>> a, int n,
                                                  std::string& error,
                                                  const std::atomic_bool* cancelled) {
    const auto stopped = [cancelled] { return cancelled && cancelled->load(std::memory_order_relaxed); };
    for (auto& row : a) {
        double scale = 0;
        for (int col = 0; col <= n; ++col) {
            if (!std::isfinite(row[col])) { error = "DC matrix exceeds numerical range."; return std::nullopt; }
            if (col < n) scale = std::max(scale, std::abs(row[col]));
        }
        if (scale == 0) {
            error = "Singular DC circuit: redundant or conflicting voltage sources.";
            return std::nullopt;
        }
        for (double& entry : row) entry /= scale;
    }
    const double tolerance = std::numeric_limits<double>::epsilon() * std::max(n, 1) * 8;
    for (int col = 0; col < n; ++col) {
        if (stopped()) { error = "DC analysis cancelled."; return std::nullopt; }
        int pivot = col;
        for (int row = col + 1; row < n; ++row)
            if (std::abs(a[row][col]) > std::abs(a[pivot][col])) pivot = row;
        if (std::abs(a[pivot][col]) <= tolerance) {
            error = "Singular or ill-conditioned DC circuit: check voltage sources and floating nodes.";
            return std::nullopt;
        }
        std::swap(a[pivot], a[col]);
        for (int row = col + 1; row < n; ++row) {
            const double factor = a[row][col] / a[col][col];
            a[row][col] = 0;
            for (int k = col + 1; k <= n; ++k) a[row][k] -= factor * a[col][k];
        }
    }
    std::vector<double> solution(static_cast<std::size_t>(n), 0);
    for (int row = n - 1; row >= 0; --row) {
        if (stopped()) { error = "DC analysis cancelled."; return std::nullopt; }
        double rhs = a[row][n];
        for (int col = row + 1; col < n; ++col) rhs -= a[row][col] * solution[col];
        solution[row] = rhs / a[row][row];
        if (!std::isfinite(solution[row])) { error = "DC solution exceeds numerical range."; return std::nullopt; }
    }
    return solution;
}

} // namespace

DcResult solveDc(const DcCircuit& circuit, const std::atomic_bool* cancelled) {
    const auto stopped = [cancelled] { return cancelled && cancelled->load(std::memory_order_relaxed); };
    const auto failure = [](const std::string& message) {
        DcResult result;
        result.error = message;
        return result;
    };
    if (stopped()) return failure("DC analysis cancelled.");
    if (circuit.netCount <= 0 || (circuit.elements.empty() && circuit.nonlinear.empty()))
        return failure("Circuit is empty.");
    if (circuit.ground < 0 || circuit.ground >= circuit.netCount)
        return failure("Circuit has no valid ground net (0).");
    if (circuit.netCount > 257) return failure("DC analysis supports at most 256 unknowns.");

    // Nonlinear elements with a series resistance get an internal auxiliary node between the
    // resistor and the ideal junction, appended after the caller's own nets; the caller never
    // sees these indices (they exist only inside this solve).
    std::vector<int> auxNode(circuit.nonlinear.size(), -1);
    int totalNets = circuit.netCount;
    for (std::size_t i = 0; i < circuit.nonlinear.size(); ++i) {
        const auto& element = circuit.nonlinear[i];
        if (element.nets.size() != 2)
            return failure("Nonlinear element needs exactly 2 terminals: " + element.reference + ".");
        for (int net : element.nets)
            if (net < 0 || net >= circuit.netCount)
                return failure("Invalid net index for " + element.reference + ".");
        const std::size_t expectedParams = element.kind == NonlinearKind::Zener ? 5
            : element.kind == NonlinearKind::Led ? 4 : 3;
        if (element.parameters.size() != expectedParams)
            return failure("Wrong parameter count for " + element.reference + ".");
        for (double value : element.parameters)
            if (!std::isfinite(value)) return failure("Non-finite parameter for " + element.reference + ".");
        const double is = element.parameters[0];
        const double n = element.parameters[1];
        const double rs = element.parameters[2];
        if (is <= 0) return failure("Saturation current must be finite and strictly positive: " + element.reference + ".");
        if (n <= 0) return failure("Ideality factor must be finite and strictly positive: " + element.reference + ".");
        if (rs < 0) return failure("Series resistance must be finite and non-negative: " + element.reference + ".");
        if (element.kind == NonlinearKind::Zener) {
            if (element.parameters[3] <= 0)
                return failure("Breakdown voltage must be finite and strictly positive: " + element.reference + ".");
            if (element.parameters[4] <= 0)
                return failure("Breakdown resistance must be finite and strictly positive: " + element.reference + ".");
        } else if (element.kind == NonlinearKind::Led) {
            if (element.parameters[3] < 0)
                return failure("Rated current must be finite and non-negative: " + element.reference + ".");
        }
        if (rs > 0) {
            if (totalNets >= 257) return failure("DC analysis supports at most 256 unknowns.");
            auxNode[i] = totalNets++;
        }
    }
    if (totalNets > 257) return failure("DC analysis supports at most 256 unknowns.");

    int sources = 0;
    std::vector<std::vector<int>> adjacent(static_cast<std::size_t>(totalNets));
    std::vector<std::vector<int>> coupled(static_cast<std::size_t>(totalNets));
    const auto link = [](std::vector<std::vector<int>>& graph, int a, int b) {
        graph[a].push_back(b);
        graph[b].push_back(a);
    };
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
            if (++sources + totalNets - 1 > 256) return failure("DC analysis supports at most 256 unknowns.");
        } else if (element.kind != DcKind::Capacitor && element.kind != DcKind::CurrentSource) {
            return failure("Unsupported DC element: " + element.reference + ".");
        }
        link(coupled, element.positive, element.negative);
        if (element.kind == DcKind::Capacitor || element.kind == DcKind::CurrentSource) continue;
        link(adjacent, element.positive, element.negative);
    }
    for (std::size_t i = 0; i < circuit.nonlinear.size(); ++i) {
        const auto& element = circuit.nonlinear[i];
        const int anode = element.nets[0], cathode = element.nets[1];
        if (auxNode[i] < 0) {
            link(adjacent, anode, cathode);
            link(coupled, anode, cathode);
        } else {
            link(adjacent, anode, auxNode[i]);
            link(coupled, anode, auxNode[i]);
            link(adjacent, auxNode[i], cathode);
            link(coupled, auxNode[i], cathode);
        }
    }
    const auto reach = [&](const std::vector<std::vector<int>>& graph) {
        std::vector<bool> reachable(static_cast<std::size_t>(totalNets), false);
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

    const int nodeUnknowns = totalNets - 1;
    const int n = nodeUnknowns + sources;
    const auto nodeIndex = [&circuit](int net) {
        return net == circuit.ground ? -1 : net - (net > circuit.ground ? 1 : 0);
    };
    constexpr double gmin = 1e-12;

    // Everything that does not depend on the nonlinear guess or on continuation staging: the
    // fixed gmin ties, the linear elements, and the (linear) series resistors ahead of nonlinear
    // junctions. Rebuilt per source-stepping stage since VoltageSource/CurrentSource values scale.
    const auto buildBase = [&](double sourceScale, double gminExtra) {
        std::vector<std::vector<double>> a(static_cast<std::size_t>(n),
                                           std::vector<double>(static_cast<std::size_t>(n) + 1, 0));
        for (int net = 0; net < totalNets; ++net) {
            const int idx = nodeIndex(net);
            if (idx < 0) continue;
            if (!conducting[net]) a[idx][idx] += gmin;
            if (gminExtra > 0) a[idx][idx] += gminExtra;
        }
        int source = nodeUnknowns;
        for (const auto& element : circuit.elements) {
            const int p = nodeIndex(element.positive), m = nodeIndex(element.negative);
            if (element.kind == DcKind::Capacitor) continue;
            if (element.kind == DcKind::Resistor) {
                if (p == m) continue;
                const double g = 1 / element.value;
                if (p >= 0) a[p][p] += g;
                if (m >= 0) a[m][m] += g;
                if (p >= 0 && m >= 0) { a[p][m] -= g; a[m][p] -= g; }
            } else if (element.kind == DcKind::CurrentSource) {
                const double value = element.value * sourceScale;
                if (p >= 0) a[p][n] -= value;
                if (m >= 0) a[m][n] += value;
            } else {
                if (p >= 0) { a[p][source] += 1; a[source][p] += 1; }
                if (m >= 0) { a[m][source] -= 1; a[source][m] -= 1; }
                a[source][n] = element.kind == DcKind::Inductor ? 0.0 : element.value * sourceScale;
                ++source;
            }
        }
        for (std::size_t i = 0; i < circuit.nonlinear.size(); ++i) {
            if (auxNode[i] < 0) continue;
            const auto& element = circuit.nonlinear[i];
            const int p = nodeIndex(element.nets[0]), m = nodeIndex(auxNode[i]);
            const double g = 1 / element.parameters[2];
            if (p == m) continue;
            if (p >= 0) a[p][p] += g;
            if (m >= 0) a[m][m] += g;
            if (p >= 0 && m >= 0) { a[p][m] -= g; a[m][p] -= g; }
        }
        return a;
    };

    // Stamps every nonlinear junction's Norton companion model (conductance in parallel with a
    // current source) at `guess`, the present per-element junction voltage, onto a copy of `base`.
    const auto stampJunctions = [&](std::vector<std::vector<double>> matrix,
                                     const std::vector<double>& guess) {
        for (std::size_t i = 0; i < circuit.nonlinear.size(); ++i) {
            const auto& element = circuit.nonlinear[i];
            const int junctionAnode = auxNode[i] < 0 ? element.nets[0] : auxNode[i];
            const int p = nodeIndex(junctionAnode), m = nodeIndex(element.nets[1]);
            if (p == m) continue;
            double current = 0, conductance = 0;
            const double is = element.parameters[0], nn = element.parameters[1];
            if (element.kind == NonlinearKind::Zener)
                zenerCurrent(guess[i], is, nn, element.parameters[3], element.parameters[4], current, conductance);
            else
                diodeCurrent(guess[i], is, nn, current, conductance);
            const double ieq = current - conductance * guess[i];
            if (p >= 0) matrix[p][p] += conductance;
            if (m >= 0) matrix[m][m] += conductance;
            if (p >= 0 && m >= 0) { matrix[p][m] -= conductance; matrix[m][p] -= conductance; }
            if (p >= 0) matrix[p][n] -= ieq;
            if (m >= 0) matrix[m][n] += ieq;
        }
        return matrix;
    };

    // Fast path: no nonlinear elements, single linear solve (identical to the pre-#62 solver).
    if (circuit.nonlinear.empty()) {
        std::string error;
        const auto solution = gaussianSolve(buildBase(1.0, 0.0), n, error, cancelled);
        if (!solution) return failure(error);
        DcResult result;
        result.voltages.resize(static_cast<std::size_t>(circuit.netCount), 0);
        for (int net = 0; net < circuit.netCount; ++net)
            if (net != circuit.ground) result.voltages[net] = (*solution)[nodeIndex(net)];
        int source = nodeUnknowns;
        for (const auto& element : circuit.elements) {
            const double current = element.kind == DcKind::Capacitor ? 0.0
                : element.kind == DcKind::CurrentSource ? element.value
                : element.kind == DcKind::Resistor
                ? (result.voltages[element.positive] - result.voltages[element.negative]) / element.value
                : (*solution)[source++];
            if (!std::isfinite(current)) return failure("DC current exceeds numerical range.");
            result.currents.push_back(current);
        }
        result.success = true;
        return result;
    }

    // Damped Newton-Raphson: iterate the junction voltage guesses to self-consistency. Returns the
    // converged full node-voltage vector, or nullopt if this stage did not converge (the caller
    // tries a more gradual continuation stage next; only the final failure is reported).
    constexpr int maxIterations = 100;
    constexpr double voltageTolerance = 1e-9;
    const auto newton = [&](double sourceScale, double gminExtra, std::vector<double>& guess,
                             std::string& error) -> std::optional<std::vector<double>> {
        const auto base = buildBase(sourceScale, gminExtra);
        for (int iter = 0; iter < maxIterations; ++iter) {
            if (stopped()) { error = "DC analysis cancelled."; return std::nullopt; }
            const auto solution = gaussianSolve(stampJunctions(base, guess), n, error, cancelled);
            if (!solution) return std::nullopt;
            std::vector<double> voltages(static_cast<std::size_t>(totalNets), 0);
            for (int net = 0; net < totalNets; ++net)
                if (net != circuit.ground) voltages[net] = (*solution)[nodeIndex(net)];
            double maxDelta = 0;
            for (std::size_t i = 0; i < circuit.nonlinear.size(); ++i) {
                const int junctionAnode = auxNode[i] < 0 ? circuit.nonlinear[i].nets[0] : auxNode[i];
                const double newV = voltages[junctionAnode] - voltages[circuit.nonlinear[i].nets[1]];
                const double limited = limitJunctionVoltage(guess[i], newV, circuit.nonlinear[i].parameters[1]);
                maxDelta = std::max(maxDelta, std::abs(limited - guess[i]));
                guess[i] = limited;
            }
            if (maxDelta < voltageTolerance) return voltages;
        }
        error = "Newton-Raphson did not converge within " + std::to_string(maxIterations) + " iterations.";
        return std::nullopt;
    };

    std::vector<double> guess(circuit.nonlinear.size(), 0.0);
    std::string error;
    auto voltages = newton(1.0, 0.0, guess, error);
    if (!voltages) {
        // Source stepping: ramp independent sources up from a small fraction, each stage seeded
        // from the previous stage's converged junction voltages.
        std::fill(guess.begin(), guess.end(), 0.0);
        bool ok = true;
        for (double scale : {1e-3, 1e-2, 1e-1, 0.3, 1.0}) {
            auto stage = newton(scale, 0.0, guess, error);
            if (!stage) { ok = false; break; }
            voltages = stage;
        }
        if (!ok || !voltages) {
            // Gmin stepping: add a large parallel conductance at every node and anneal it away.
            std::fill(guess.begin(), guess.end(), 0.0);
            ok = true;
            for (double gminExtra : {1e-1, 1e-2, 1e-3, 1e-4, 1e-5, 1e-6, 1e-7, 1e-8, 1e-9, 0.0}) {
                auto stage = newton(1.0, gminExtra, guess, error);
                if (!stage) { ok = false; break; }
                voltages = stage;
            }
            if (!ok || !voltages) return failure(error);
        }
    }
    if (stopped()) return failure("DC analysis cancelled.");

    DcResult result;
    result.voltages.assign(voltages->begin(), voltages->begin() + circuit.netCount);
    int source = nodeUnknowns;
    // Re-run the linear solve's source-current extraction: recompute via one more gaussianSolve
    // at the converged guess so voltage-source/inductor branch currents are available.
    {
        std::string ignoredError;
        const auto solution = gaussianSolve(stampJunctions(buildBase(1.0, 0.0), guess), n, ignoredError, cancelled);
        if (!solution) return failure("DC solution exceeds numerical range.");
        for (const auto& element : circuit.elements) {
            const double current = element.kind == DcKind::Capacitor ? 0.0
                : element.kind == DcKind::CurrentSource ? element.value
                : element.kind == DcKind::Resistor
                ? (result.voltages[element.positive] - result.voltages[element.negative]) / element.value
                : (*solution)[source++];
            if (!std::isfinite(current)) return failure("DC current exceeds numerical range.");
            result.currents.push_back(current);
        }
    }
    result.nonlinearCurrents.resize(circuit.nonlinear.size(), 0.0);
    result.nonlinearAux.resize(circuit.nonlinear.size(), 0.0);
    for (std::size_t i = 0; i < circuit.nonlinear.size(); ++i) {
        const auto& element = circuit.nonlinear[i];
        double current = 0, conductance = 0;
        if (element.kind == NonlinearKind::Zener)
            zenerCurrent(guess[i], element.parameters[0], element.parameters[1], element.parameters[3],
                         element.parameters[4], current, conductance);
        else
            diodeCurrent(guess[i], element.parameters[0], element.parameters[1], current, conductance);
        if (!std::isfinite(current)) return failure("DC current exceeds numerical range.");
        result.nonlinearCurrents[i] = current;
        if (element.kind == NonlinearKind::Led && element.parameters[3] > 0)
            result.nonlinearAux[i] = std::clamp(current / element.parameters[3], 0.0, 1.0);
    }
    result.success = true;
    return result;
}

} // namespace hatt::electrical
