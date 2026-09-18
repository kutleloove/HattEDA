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

// Limits how far a voltage guess may move in one Newton iteration: exp()-based junctions (diode/
// zener/LED/BJT) use 4*n*Vt so exp() never overflows; MOSFET terminal voltages are not exponential,
// but a fixed cap still keeps Newton from bouncing between conduction regions (cutoff/triode/
// saturation) on a wild first step from a zero guess.
double limitStep(double previous, double proposed, double maxStep) {
    if (proposed - previous > maxStep) return previous + maxStep;
    if (previous - proposed > maxStep) return previous - maxStep;
    return proposed;
}
constexpr double MosfetVoltageStep = 1.0;

// BJT Ebers-Moll transport model (junction ideality 1), NPN sign convention: v1 = Vbe = Vb - Ve,
// v2 = Vbc = Vb - Vc. Returns collector/base current (into the device at each terminal) and the
// four partial derivatives needed to linearize both around (v1, v2): gm = dIc/dv1, goMag =
// -dIc/dv2 (always used with a minus sign; kept positive here), gpi = dIb/dv1, gmu = dIb/dv2.
// PNP reuses this with v1 = Veb = Ve - Vb, v2 = Vcb = Vc - Vb (see the `sign` handling at the call
// site): the four conductances are identical in that frame, only the reported currents and the
// linearization constants flip sign.
struct BjtCore { double ic, ib, gm, goMag, gpi, gmu; };
BjtCore bjtCore(double v1, double v2, double is, double bf, double br) {
    const double e1 = std::exp(std::min(v1 / ThermalVoltage, 80.0));
    const double e2 = std::exp(std::min(v2 / ThermalVoltage, 80.0));
    BjtCore core;
    core.ic = is * (e1 - e2) - (is / br) * (e2 - 1.0);
    core.ib = (is / bf) * (e1 - 1.0) + (is / br) * (e2 - 1.0);
    core.gm = is * e1 / ThermalVoltage;
    core.goMag = is * (1.0 + 1.0 / br) * e2 / ThermalVoltage;
    core.gpi = (is / bf) * e1 / ThermalVoltage;
    core.gmu = (is / br) * e2 / ThermalVoltage;
    return core;
}

// MOSFET level-1 square law (no channel-length modulation), NMOS sign convention: v1 = Vgs =
// Vg - Vs, v2 = Vds = Vd - Vs, vto > 0. Returns drain current (into the device at the drain) and
// its two partial derivatives. PMOS reuses this with v1 = Vsg = Vs - Vg, v2 = Vsd = Vs - Vd and
// vto = -Vto_param > 0 (see the call site): `id` then means Isd (source to drain).
struct MosfetCore { double id, gm, gds; };
MosfetCore mosfetCore(double v1, double v2, double vto, double k) {
    const double vov = v1 - vto;
    MosfetCore core{0, 0, 0};
    if (vov <= 0) return core; // cutoff
    if (v2 < vov) { // triode
        core.id = k * (2.0 * vov * v2 - v2 * v2);
        core.gm = 2.0 * k * v2;
        core.gds = 2.0 * k * (vov - v2);
    } else { // saturation
        core.id = k * vov * vov;
        core.gm = 2.0 * k * vov;
        core.gds = 0.0;
    }
    return core;
}

// Ideal op-amp, modelled as a Norton-equivalent voltage-controlled voltage source: a smooth
// tanh saturation between the (fixed) supply rails, so it needs no auxiliary MNA unknown (see
// the call site's derivation in the doc comment above the stamping code). `diff` is
// Vplus - Vminus. Returns the target output voltage and dTarget/dDiff.
struct OpAmpCore { double target, gm; };
OpAmpCore opAmpCore(double diff, double gain, double vpos, double vneg) {
    const double vmid = (vpos + vneg) / 2.0;
    const double vswing = (vpos - vneg) / 2.0;
    if (vswing <= 0) return {vmid, 0.0};
    const double t = std::tanh(gain * diff / vswing);
    return {vmid + vswing * t, gain * (1.0 - t * t)};
}

// A logic gate's output as a smooth "soft" boolean function of one or two input voltages, each
// compared to `vth` via tanh (steepness controls how sharp the digital transition is). Returns
// the target output voltage and its two partial derivatives (dTarget/dVa, dTarget/dVb; the
// second is 0 for LogicFunction::Not, which ignores vb entirely).
struct LogicCore { double target, ga, gb; };
LogicCore logicGateCore(double va, double vb, LogicFunction function, double vth, double steepness,
                         double vol, double voh) {
    const double vmid = (voh + vol) / 2.0, vswing = (voh - vol) / 2.0;
    const double sa = std::tanh((va - vth) * steepness);
    const double dsa = steepness * (1.0 - sa * sa);
    double sb = 0, dsb = 0;
    if (function != LogicFunction::Not) {
        sb = std::tanh((vb - vth) * steepness);
        dsb = steepness * (1.0 - sb * sb);
    }
    double combined = 0, dCombinedA = 0, dCombinedB = 0;
    switch (function) {
    case LogicFunction::Not: combined = -sa; dCombinedA = -1.0; break;
    case LogicFunction::And:
        if (sa <= sb) { combined = sa; dCombinedA = 1.0; } else { combined = sb; dCombinedB = 1.0; }
        break;
    case LogicFunction::Or:
        if (sa >= sb) { combined = sa; dCombinedA = 1.0; } else { combined = sb; dCombinedB = 1.0; }
        break;
    case LogicFunction::Nand:
        if (sa <= sb) { combined = -sa; dCombinedA = -1.0; } else { combined = -sb; dCombinedB = -1.0; }
        break;
    case LogicFunction::Nor:
        if (sa >= sb) { combined = -sa; dCombinedA = -1.0; } else { combined = -sb; dCombinedB = -1.0; }
        break;
    case LogicFunction::Xor: combined = -sa * sb; dCombinedA = -sb; dCombinedB = -sa; break;
    }
    return {vmid + vswing * combined, vswing * dCombinedA * dsa, vswing * dCombinedB * dsb};
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
    const auto isTwoTerminal = [](NonlinearKind kind) {
        return kind == NonlinearKind::Diode || kind == NonlinearKind::Zener || kind == NonlinearKind::Led;
    };
    for (std::size_t i = 0; i < circuit.nonlinear.size(); ++i) {
        const auto& element = circuit.nonlinear[i];
        const std::size_t expectedNets = isTwoTerminal(element.kind) ? 2 : 3;
        if (element.nets.size() != expectedNets)
            return failure("Wrong terminal count for " + element.reference + ".");
        for (int net : element.nets)
            if (net < 0 || net >= circuit.netCount)
                return failure("Invalid net index for " + element.reference + ".");
        for (double value : element.parameters)
            if (!std::isfinite(value)) return failure("Non-finite parameter for " + element.reference + ".");
        if (element.kind == NonlinearKind::BjtNpn || element.kind == NonlinearKind::BjtPnp) {
            if (element.parameters.size() != 3)
                return failure("Wrong parameter count for " + element.reference + ".");
            if (element.parameters[0] <= 0)
                return failure("Saturation current must be finite and strictly positive: " + element.reference + ".");
            if (element.parameters[1] <= 0)
                return failure("Forward beta must be finite and strictly positive: " + element.reference + ".");
            if (element.parameters[2] <= 0)
                return failure("Reverse beta must be finite and strictly positive: " + element.reference + ".");
            continue;
        }
        if (element.kind == NonlinearKind::NMosfet || element.kind == NonlinearKind::PMosfet) {
            if (element.parameters.size() != 2)
                return failure("Wrong parameter count for " + element.reference + ".");
            if (element.parameters[1] <= 0)
                return failure("K must be finite and strictly positive: " + element.reference + ".");
            continue;
        }
        if (element.kind == NonlinearKind::OpAmp) {
            if (element.parameters.size() != 4)
                return failure("Wrong parameter count for " + element.reference + ".");
            if (element.parameters[0] <= 0)
                return failure("Gain must be finite and strictly positive: " + element.reference + ".");
            if (element.parameters[1] <= 0)
                return failure("Output conductance must be finite and strictly positive: " + element.reference + ".");
            if (element.parameters[2] <= element.parameters[3])
                return failure("Vpos must be greater than Vneg: " + element.reference + ".");
            continue;
        }
        if (element.kind == NonlinearKind::LogicGate) {
            if (element.parameters.size() != 6)
                return failure("Wrong parameter count for " + element.reference + ".");
            const double function = element.parameters[0];
            if (function != std::floor(function) || function < 0 ||
                function > static_cast<double>(LogicFunction::Xor))
                return failure("Invalid logic function: " + element.reference + ".");
            if (element.parameters[2] <= 0)
                return failure("Steepness must be finite and strictly positive: " + element.reference + ".");
            if (element.parameters[4] <= element.parameters[3])
                return failure("Voh must be greater than Vol: " + element.reference + ".");
            if (element.parameters[5] <= 0)
                return failure("Output conductance must be finite and strictly positive: " + element.reference + ".");
            continue;
        }
        const std::size_t expectedParams = element.kind == NonlinearKind::Zener ? 5
            : element.kind == NonlinearKind::Led ? 4 : 3;
        if (element.parameters.size() != expectedParams)
            return failure("Wrong parameter count for " + element.reference + ".");
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
        if (isTwoTerminal(element.kind)) {
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
        } else if (element.kind == NonlinearKind::BjtNpn || element.kind == NonlinearKind::BjtPnp) {
            // Collector, base and emitter all conduct through the device; generous (all pairs)
            // rather than modelling exactly which path gmin would need to avoid a spurious tie.
            const int c = element.nets[0], b = element.nets[1], e = element.nets[2];
            link(adjacent, c, b); link(coupled, c, b);
            link(adjacent, b, e); link(coupled, b, e);
            link(adjacent, c, e); link(coupled, c, e);
        } else if (element.kind == NonlinearKind::NMosfet || element.kind == NonlinearKind::PMosfet) {
            // Drain-source conducts; the gate carries no current (ideal, no leakage) and must
            // reach ground through some other element, or it is reported as a floating net.
            const int d = element.nets[0], s = element.nets[2];
            link(adjacent, d, s);
            link(coupled, d, s);
        } else {
            // OpAmp/LogicGate: the output is strongly driven by the device itself (Norton
            // companion model with a large conductance), so it needs no gmin fallback - treat it
            // as if conducting straight to ground for reachability purposes. The inputs carry no
            // current (ideal) and must reach ground through some other element, or they are
            // reported as floating nets.
            const int output = element.nets[0];
            link(adjacent, output, circuit.ground);
            link(coupled, output, circuit.ground);
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

    // Stamps every nonlinear element's linearized companion model at `guess` (two scalars per
    // element - a single junction voltage in slot 0, slot 1 unused, for the 2-terminal kinds; two
    // independent junction/gate-overdrive voltages for BJT/MOSFET) onto a copy of `base`.
    const auto stampRow = [&](std::vector<std::vector<double>>& matrix, int row, int colA, double dA,
                               int colB, double dB, int colC, double dC, double xEq) {
        if (row < 0) return;
        if (colA >= 0) matrix[row][colA] += dA;
        if (colB >= 0) matrix[row][colB] += dB;
        if (colC >= 0) matrix[row][colC] += dC;
        matrix[row][n] -= xEq;
    };
    const auto stampJunctions = [&](std::vector<std::vector<double>> matrix,
                                     const std::vector<double>& guess) {
        for (std::size_t i = 0; i < circuit.nonlinear.size(); ++i) {
            const auto& element = circuit.nonlinear[i];
            const double v1 = guess[2 * i], v2 = guess[2 * i + 1];
            if (isTwoTerminal(element.kind)) {
                const int junctionAnode = auxNode[i] < 0 ? element.nets[0] : auxNode[i];
                const int p = nodeIndex(junctionAnode), m = nodeIndex(element.nets[1]);
                if (p == m) continue;
                double current = 0, conductance = 0;
                const double is = element.parameters[0], nn = element.parameters[1];
                if (element.kind == NonlinearKind::Zener)
                    zenerCurrent(v1, is, nn, element.parameters[3], element.parameters[4], current, conductance);
                else
                    diodeCurrent(v1, is, nn, current, conductance);
                const double ieq = current - conductance * v1;
                if (p >= 0) matrix[p][p] += conductance;
                if (m >= 0) matrix[m][m] += conductance;
                if (p >= 0 && m >= 0) { matrix[p][m] -= conductance; matrix[m][p] -= conductance; }
                if (p >= 0) matrix[p][n] -= ieq;
                if (m >= 0) matrix[m][n] += ieq;
            } else if (element.kind == NonlinearKind::BjtNpn || element.kind == NonlinearKind::BjtPnp) {
                // v1/v2 are (Vbe, Vbc) for NPN, (Veb, Vcb) for PNP; `sign` flips the reported
                // currents and the linearization constants for PNP - the four conductances below
                // are identical in either frame (see bjtCore's doc comment).
                const double sign = element.kind == NonlinearKind::BjtNpn ? 1.0 : -1.0;
                const auto core = bjtCore(v1, v2, element.parameters[0], element.parameters[1], element.parameters[2]);
                const int pc = nodeIndex(element.nets[0]), pb = nodeIndex(element.nets[1]), pe = nodeIndex(element.nets[2]);
                const double dIcDb = core.gm - core.goMag, dIcDc = core.goMag, dIcDe = -core.gm;
                const double dIbDb = core.gpi + core.gmu, dIbDc = -core.gmu, dIbDe = -core.gpi;
                const double icEq = sign * (core.ic - core.gm * v1 + core.goMag * v2);
                const double ibEq = sign * (core.ib - core.gpi * v1 - core.gmu * v2);
                stampRow(matrix, pc, pc, dIcDc, pb, dIcDb, pe, dIcDe, icEq);
                stampRow(matrix, pb, pc, dIbDc, pb, dIbDb, pe, dIbDe, ibEq);
                stampRow(matrix, pe, pc, -(dIcDc + dIbDc), pb, -(dIcDb + dIbDb), pe, -(dIcDe + dIbDe), -(icEq + ibEq));
            } else if (element.kind == NonlinearKind::NMosfet || element.kind == NonlinearKind::PMosfet) {
                // v1/v2 are (Vgs, Vds) for NMOS, (Vsg, Vsd) for PMOS; `vto` is passed with the
                // sign that makes it a positive "overdrive" threshold in either frame.
                const bool nmos = element.kind == NonlinearKind::NMosfet;
                const double vto = nmos ? element.parameters[0] : -element.parameters[0];
                const auto core = mosfetCore(v1, v2, vto, element.parameters[1]);
                const int pd = nodeIndex(element.nets[0]), pg = nodeIndex(element.nets[1]), ps = nodeIndex(element.nets[2]);
                const double idEq = core.id - core.gds * v2 - core.gm * v1;
                if (nmos) {
                    // Current into drain = +id, into source = -id, into gate = 0.
                    stampRow(matrix, pd, pd, core.gds, pg, core.gm, ps, -(core.gm + core.gds), idEq);
                    stampRow(matrix, ps, pd, -core.gds, pg, -core.gm, ps, core.gm + core.gds, -idEq);
                } else {
                    // `core.id` is Isd (source to drain); current into source = +Isd, into drain
                    // = -Isd, into gate = 0. Same conductance magnitudes as NMOS (source/drain
                    // swapped in the v1/v2 frame above).
                    stampRow(matrix, ps, pd, -core.gds, pg, -core.gm, ps, core.gm + core.gds, idEq);
                    stampRow(matrix, pd, pd, core.gds, pg, core.gm, ps, -(core.gm + core.gds), -idEq);
                }
            } else if (element.kind == NonlinearKind::OpAmp) {
                // v1 = Vplus - Vminus (v2 unused). Modelled as a Norton-equivalent VCVS: a huge
                // output conductance pulling the output toward a smooth, rail-saturating target;
                // the inputs draw no current (ideal). See opAmpCore's doc comment for why this
                // needs no auxiliary MNA unknown.
                const double gain = element.parameters[0], gOut = element.parameters[1];
                const double vpos = element.parameters[2], vneg = element.parameters[3];
                const auto core = opAmpCore(v1, gain, vpos, vneg);
                const int pOut = nodeIndex(element.nets[2]), pPlus = nodeIndex(element.nets[0]),
                          pMinus = nodeIndex(element.nets[1]);
                const double ga = gOut * core.gm;
                const double xEq = gOut * core.target - ga * v1;
                stampRow(matrix, pOut, pOut, -gOut, pPlus, ga, pMinus, -ga, xEq);
                // A tiny, element-index-dependent conductance to ground (~1 gigaohm - negligible
                // next to any real load or gOut itself). Two identically-parameterized op-amps or
                // gates in a symmetric feedback loop (e.g. a latch) would otherwise linearize to
                // an exactly singular Jacobian at their unstable symmetric point; this nudges that
                // without measurably affecting any non-degenerate circuit's answer.
                if (pOut >= 0) matrix[pOut][pOut] += (i % 2 == 0 ? 1.0 : -1.0) * 1e-9;
            } else {
                // LogicGate: v1/v2 are the two input voltages (v2 unused for LogicFunction::Not).
                // Same Norton-equivalent technique as the op-amp, driven by a "soft" boolean
                // function of the inputs instead of their difference.
                const auto function = static_cast<LogicFunction>(static_cast<int>(element.parameters[0]));
                const double vth = element.parameters[1], steepness = element.parameters[2];
                const double vol = element.parameters[3], voh = element.parameters[4], gOut = element.parameters[5];
                const auto core = logicGateCore(v1, v2, function, vth, steepness, vol, voh);
                const int pOut = nodeIndex(element.nets[0]), pA = nodeIndex(element.nets[1]),
                          pB = nodeIndex(element.nets[2]);
                const double ga = gOut * core.ga, gb = gOut * core.gb;
                const double xEq = gOut * core.target - ga * v1 - gb * v2;
                stampRow(matrix, pOut, pOut, -gOut, pA, ga, pB, gb, xEq);
                // See the identical comment on the OpAmp branch above.
                if (pOut >= 0) matrix[pOut][pOut] += (i % 2 == 0 ? 1.0 : -1.0) * 1e-9;
            }
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
    constexpr int maxIterations = 300;
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
            const auto update = [&](std::size_t slot, double newV, double maxStep) {
                const double limited = limitStep(guess[slot], newV, maxStep);
                maxDelta = std::max(maxDelta, std::abs(limited - guess[slot]));
                guess[slot] = limited;
            };
            for (std::size_t i = 0; i < circuit.nonlinear.size(); ++i) {
                const auto& element = circuit.nonlinear[i];
                if (isTwoTerminal(element.kind)) {
                    const int junctionAnode = auxNode[i] < 0 ? element.nets[0] : auxNode[i];
                    const double newV = voltages[junctionAnode] - voltages[element.nets[1]];
                    update(2 * i, newV, 4.0 * element.parameters[1] * ThermalVoltage);
                } else if (element.kind == NonlinearKind::BjtNpn || element.kind == NonlinearKind::BjtPnp) {
                    // nets = {collector, base, emitter}. NPN tracks (Vbe, Vbc); PNP tracks the
                    // mirrored (Veb, Vcb) - see bjtCore's doc comment.
                    const double vc = voltages[element.nets[0]], vb = voltages[element.nets[1]], ve = voltages[element.nets[2]];
                    const bool npn = element.kind == NonlinearKind::BjtNpn;
                    const double v1 = npn ? (vb - ve) : (ve - vb);
                    const double v2 = npn ? (vb - vc) : (vc - vb);
                    update(2 * i, v1, 4.0 * ThermalVoltage);
                    update(2 * i + 1, v2, 4.0 * ThermalVoltage);
                } else if (element.kind == NonlinearKind::NMosfet || element.kind == NonlinearKind::PMosfet) {
                    // nets = {drain, gate, source}. NMOS tracks (Vgs, Vds); PMOS tracks the
                    // mirrored (Vsg, Vsd) - see mosfetCore's doc comment.
                    const bool nmos = element.kind == NonlinearKind::NMosfet;
                    const double vd = voltages[element.nets[0]], vg = voltages[element.nets[1]], vs = voltages[element.nets[2]];
                    const double v1 = nmos ? (vg - vs) : (vs - vg);
                    const double v2 = nmos ? (vd - vs) : (vs - vd);
                    update(2 * i, v1, MosfetVoltageStep);
                    update(2 * i + 1, v2, MosfetVoltageStep);
                } else if (element.kind == NonlinearKind::OpAmp) {
                    // nets = {nonInvertingIn, invertingIn, output}; tracks Vplus - Vminus. A very
                    // high gain makes the sensitive transition width (vswing/gain) far too small
                    // to use directly as a step cap - reaching a saturated operating point can
                    // genuinely need a multi-volt change in diff. A step of vswing/10 comfortably
                    // reaches saturation in a handful of iterations while staying bounded by the
                    // supply range; once |diff| is a few vswing/gain past the transition, tanh is
                    // already flat there, so stepping past it does not cause the oscillation a
                    // similarly oversized step would on the exp()-based junctions above.
                    const double diff = voltages[element.nets[0]] - voltages[element.nets[1]];
                    const double vswing = (element.parameters[2] - element.parameters[3]) / 2.0;
                    update(2 * i, diff, vswing / 10.0);
                } else {
                    // LogicGate: nets = {output, inputA, inputB}; tracks the two input voltages,
                    // capped to a small multiple of the tanh's sensitive width (1/steepness).
                    const double va = voltages[element.nets[1]], vb = voltages[element.nets[2]];
                    const double step = 10.0 / element.parameters[2];
                    update(2 * i, va, step);
                    update(2 * i + 1, vb, step);
                }
            }
            if (maxDelta < voltageTolerance) return voltages;
        }
        error = "Newton-Raphson did not converge within " + std::to_string(maxIterations) + " iterations.";
        return std::nullopt;
    };

    // Warm start (#62): seed each nonlinear element's guess from a previous solve's node voltages
    // instead of 0 V, so a bistable circuit (e.g. a latch) keeps its state across repeated solves
    // (the live simulation mode re-solves on every edit) instead of always settling to the same
    // one. Falls back to 0 V unless the caller supplied exactly `netCount` voltages. For a
    // 2-terminal element with a series resistance, this approximates the internal auxiliary node
    // as the anode itself (ignoring the Rs drop) - only the seed, not the final answer, so a few
    // Newton iterations refine it either way.
    std::vector<double> guess(2 * circuit.nonlinear.size(), 0.0);
    if (circuit.initialVoltages.size() == static_cast<std::size_t>(circuit.netCount)) {
        const auto& seed = circuit.initialVoltages;
        for (std::size_t i = 0; i < circuit.nonlinear.size(); ++i) {
            const auto& element = circuit.nonlinear[i];
            if (isTwoTerminal(element.kind)) {
                guess[2 * i] = seed[element.nets[0]] - seed[element.nets[1]];
            } else if (element.kind == NonlinearKind::BjtNpn || element.kind == NonlinearKind::BjtPnp) {
                const double vc = seed[element.nets[0]], vb = seed[element.nets[1]], ve = seed[element.nets[2]];
                const bool npn = element.kind == NonlinearKind::BjtNpn;
                guess[2 * i] = npn ? (vb - ve) : (ve - vb);
                guess[2 * i + 1] = npn ? (vb - vc) : (vc - vb);
            } else if (element.kind == NonlinearKind::NMosfet || element.kind == NonlinearKind::PMosfet) {
                const bool nmos = element.kind == NonlinearKind::NMosfet;
                const double vd = seed[element.nets[0]], vg = seed[element.nets[1]], vs = seed[element.nets[2]];
                guess[2 * i] = nmos ? (vg - vs) : (vs - vg);
                guess[2 * i + 1] = nmos ? (vd - vs) : (vs - vd);
            } else if (element.kind == NonlinearKind::OpAmp) {
                guess[2 * i] = seed[element.nets[0]] - seed[element.nets[1]];
            } else {
                guess[2 * i] = seed[element.nets[1]];
                guess[2 * i + 1] = seed[element.nets[2]];
            }
        }
    } else {
        // No warm start: a tiny alternating perturbation instead of an exact 0 V guess for every
        // element. A perfectly symmetric bistable circuit (e.g. two identically-parameterized
        // cross-coupled gates) linearized exactly at its unstable symmetric point has a genuinely
        // singular Jacobian; this breaks that exact symmetry (voltages elsewhere are unaffected
        // to far better than any test tolerance - Newton converges to within 1e-9 V regardless of
        // which side of a symmetric point it starts from).
        for (std::size_t i = 0; i < guess.size(); ++i) guess[i] = (i % 2 == 0) ? 1e-6 : -1e-6;
    }
    const std::vector<double> coldGuess = guess;
    std::string error;
    auto voltages = newton(1.0, 0.0, guess, error);
    if (!voltages) {
        // Source stepping: ramp independent sources up from a small fraction, each stage seeded
        // from the previous stage's converged junction voltages.
        guess = coldGuess;
        bool ok = true;
        for (double scale : {1e-3, 1e-2, 1e-1, 0.3, 1.0}) {
            auto stage = newton(scale, 0.0, guess, error);
            if (!stage) { ok = false; break; }
            voltages = stage;
        }
        if (!ok || !voltages) {
            // Gmin stepping: add a large parallel conductance at every node and anneal it away.
            guess = coldGuess;
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
        const double v1 = guess[2 * i], v2 = guess[2 * i + 1];
        double current = 0;
        if (isTwoTerminal(element.kind)) {
            double conductance = 0;
            if (element.kind == NonlinearKind::Zener)
                zenerCurrent(v1, element.parameters[0], element.parameters[1], element.parameters[3],
                             element.parameters[4], current, conductance);
            else
                diodeCurrent(v1, element.parameters[0], element.parameters[1], current, conductance);
            if (element.kind == NonlinearKind::Led && element.parameters[3] > 0)
                result.nonlinearAux[i] = std::clamp(current / element.parameters[3], 0.0, 1.0);
        } else if (element.kind == NonlinearKind::BjtNpn || element.kind == NonlinearKind::BjtPnp) {
            // Reported current is into the first listed terminal (collector).
            const double sign = element.kind == NonlinearKind::BjtNpn ? 1.0 : -1.0;
            current = sign * bjtCore(v1, v2, element.parameters[0], element.parameters[1], element.parameters[2]).ic;
        } else if (element.kind == NonlinearKind::NMosfet || element.kind == NonlinearKind::PMosfet) {
            // Reported current is into the first listed terminal (drain): +Id for NMOS, -Isd for
            // PMOS (PMOS conducting current flows out of the drain into the external circuit).
            const bool nmos = element.kind == NonlinearKind::NMosfet;
            const double vto = nmos ? element.parameters[0] : -element.parameters[0];
            const double id = mosfetCore(v1, v2, vto, element.parameters[1]).id;
            current = nmos ? id : -id;
        } else if (element.kind == NonlinearKind::OpAmp) {
            current = 0.0; // ideal: no current into either input
        } else {
            current = 0.0; // LogicGate: ideal, no current into either input
            const double vol = element.parameters[3], voh = element.parameters[4];
            if (voh > vol)
                result.nonlinearAux[i] =
                    std::clamp((result.voltages[element.nets[0]] - vol) / (voh - vol), 0.0, 1.0);
        }
        if (!std::isfinite(current)) return failure("DC current exceeds numerical range.");
        result.nonlinearCurrents[i] = current;
    }
    result.success = true;
    return result;
}

} // namespace hatt::electrical
