#include "hatt/electrical/Circuit.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <string>

using namespace hatt::electrical;

namespace {
int failures = 0;
void check(bool condition, const char* name) {
    if (!condition) { std::cerr << "FAIL: " << name << '\n'; ++failures; }
}
bool near(double actual, double expected) { return std::abs(actual - expected) < 1e-10; }
DcCircuit divider() {
    return {3, 0, {{"V1", DcKind::VoltageSource, 1, 0, 5},
                   {"R1", DcKind::Resistor, 1, 2, 1000},
                   {"R2", DcKind::Resistor, 2, 0, 1000}}};
}
void rejects(const DcCircuit& circuit, const char* name) {
    const auto result = solveDc(circuit);
    check(!result.success && !result.error.empty() && result.voltages.empty() &&
              result.currents.empty(), name);
}
}

int main() {
    const auto result = solveDc(divider());
    check(result.success, "divider solves");
    if (result.success) {
        check(near(result.voltages[0], 0) && near(result.voltages[1], 5) &&
                  near(result.voltages[2], 2.5), "analytic divider voltages");
        check(near(result.currents[0], -0.0025) && near(result.currents[1], 0.0025) &&
                  near(result.currents[2], 0.0025), "positive-to-negative current signs");
    }
    auto circuit = divider();
    circuit.elements.push_back({"Rshort", DcKind::Resistor, 2, 2, 1e-100});
    const auto sameNet = solveDc(circuit);
    check(sameNet.success && near(sameNet.voltages[2], 2.5) &&
              near(sameNet.currents.back(), 0), "same-net resistor has no effect");
    circuit = divider();
    circuit.ground = -1;
    rejects(circuit, "missing ground");
    circuit = divider(); circuit.ground = 3;
    rejects(circuit, "out-of-range ground");
    rejects({}, "empty circuit");
    circuit = divider(); circuit.elements[1].value = 0;
    rejects(circuit, "zero resistance");
    circuit.elements[1].value = -1;
    rejects(circuit, "negative resistance");
    circuit.elements[1].value = std::numeric_limits<double>::infinity();
    rejects(circuit, "infinite resistance");
    circuit = divider(); circuit.elements[0].value = std::numeric_limits<double>::quiet_NaN();
    rejects(circuit, "NaN source");
    circuit = divider(); circuit.elements[1].positive = -1;
    rejects(circuit, "negative net index");
    circuit.elements[1].positive = 3;
    rejects(circuit, "out-of-range net index");
    circuit = divider(); circuit.netCount = 5;
    circuit.elements.push_back({"Rfloat", DcKind::Resistor, 3, 4, 1000});
    rejects(circuit, "disconnected passive island");
    circuit = divider(); circuit.netCount = 4;
    rejects(circuit, "unused floating net");
    circuit = divider(); circuit.elements.push_back({"V2", DcKind::VoltageSource, 1, 0, 6});
    rejects(circuit, "conflicting sources");
    circuit.elements.back().value = 5;
    rejects(circuit, "redundant sources have undetermined currents");
    rejects({1, 0, {{"V1", DcKind::VoltageSource, 0, 0, 1}}}, "shorted source");
    circuit = divider(); circuit.netCount = 258;
    rejects(circuit, "unknown count bound");
    std::atomic_bool cancelled{true};
    const auto cancelledResult = solveDc(divider(), &cancelled);
    check(!cancelledResult.success && cancelledResult.error.find("cancelled") != std::string::npos,
          "cancellation");
    cancelled = false;
    check(solveDc(divider(), &cancelled).success, "clear cancellation permits solve");
    // Ground is an index, not necessarily net zero; reversed source polarity is supported.
    const auto reversed = solveDc({2, 1, {{"V1", DcKind::VoltageSource, 1, 0, 3},
                                        {"R1", DcKind::Resistor, 0, 1, 1000}}});
    check(reversed.success, "arbitrary ground index");
    if (reversed.success) check(near(reversed.voltages[0], -3) &&
                               near(reversed.currents[0], -0.003), "reversed source sign");
    const auto currentDriven = solveDc({2, 0, {{"I1", DcKind::CurrentSource, 1, 0, 0.002},
                                               {"R1", DcKind::Resistor, 1, 0, 1000}}});
    check(currentDriven.success && near(currentDriven.voltages[1], -2.0) &&
              near(currentDriven.currents[0], 0.002) && near(currentDriven.currents[1], -0.002),
          "independent current source stamps KCL and reports signed current");
    // DC models: a capacitor is open, an inductor is a short with a reported current.
    const auto open = solveDc({3, 0, {{"V1", DcKind::VoltageSource, 1, 0, 5},
                                      {"R1", DcKind::Resistor, 1, 2, 1000},
                                      {"C1", DcKind::Capacitor, 2, 0, 1e-7}}});
    check(open.success && near(open.voltages[2], 5) && near(open.currents[2], 0), "capacitor is open at DC");
    const auto shorted = solveDc({3, 0, {{"V1", DcKind::VoltageSource, 1, 0, 5},
                                         {"R1", DcKind::Resistor, 1, 2, 1000},
                                         {"L1", DcKind::Inductor, 2, 0, 1e-5}}});
    check(shorted.success && near(shorted.voltages[2], 0) && near(shorted.currents[2], 0.005),
          "inductor is a short at DC");
    const auto coupled = solveDc({3, 0, {{"V1", DcKind::VoltageSource, 1, 0, 5},
                                         {"C1", DcKind::Capacitor, 1, 2, 1e-7},
                                         {"R1", DcKind::Resistor, 2, 2, 1000}}});
    check(coupled.success && near(coupled.voltages[2], 0), "net behind a capacitor solves with gmin");
    const auto passive = solveDc({2, 0, {{"R1", DcKind::Resistor, 1, 0, 1e15}}});
    check(passive.success && near(passive.voltages[1], 0), "small conductance row scaling");
    struct ParseCase { const char* text; double value; };
    for (const ParseCase entry : {ParseCase{"5", 5}, {" .5 ", .5}, {"+1.5e+2", 150},
                                 {"-2E-3", -.002}, {"1p", 1e-12}, {"2n", 2e-9},
                                 {"3u", 3e-6}, {"4M", .004}, {"10k", 10000},
                                 {"2.2MEG", 2200000}, {"1g", 1e9}, {"1e3m", 1}}) {
        double value = 0;
        check(parseSpiceValue(entry.text, value) &&
                  std::abs(value - entry.value) <= std::abs(entry.value) * 1e-12,
              entry.text);
    }
    for (const char* text : {"", " ", "+", ".", "nan", "NaN", "inf", "-inf", "1e", "1e+",
                             "1kfoo", "1kk", "1 k", "1V", "1ohm", "0x10", "1,5", "1e309",
                             "1e-999", "1e308g", "1e-320p", "--1", "1.2.3"}) {
        double value = 123;
        check(!parseSpiceValue(text, value) && value == 123, text);
    }
    // #62 nonlinear DC operating point: diode, zener, LED.
    constexpr double Vt = 0.025852;
    // Independent reference: solves V = I*R + n*Vt*ln(I/Is + 1) for I by bisection, entirely
    // separate from the solver's own Newton-Raphson, so the two can be cross-checked.
    const auto seriesDiodeCurrent = [](double supply, double resistance, double is, double n) {
        double lo = 0, hi = supply / resistance;
        for (int iter = 0; iter < 200; ++iter) {
            const double mid = (lo + hi) / 2;
            const double vDiode = n * Vt * std::log(mid / is + 1.0);
            const double predictedSupply = mid * resistance + vDiode;
            if (predictedSupply < supply) lo = mid; else hi = mid;
        }
        return (lo + hi) / 2;
    };
    {
        // 3.3 V + 100 ohm + red LED (issue #62 acceptance: Vf ~1.8 V at rated current). Net 1 is
        // the supply node, net 2 the LED anode (after the series resistor), net 0 ground.
        const double is = 5e-18, n = 1.8, ratedCurrent = 0.02;
        const auto expected = seriesDiodeCurrent(3.3, 100, is, n);
        const auto result = solveDc({3, 0,
                                     {{"V1", DcKind::VoltageSource, 1, 0, 3.3}, {"R1", DcKind::Resistor, 1, 2, 100}},
                                     {{"D1", NonlinearKind::Led, {2, 0}, {is, n, 0, ratedCurrent}}}});
        check(result.success, "LED divider solves");
        if (result.success) {
            check(std::abs(result.nonlinearCurrents[0] - expected) < expected * 1e-6,
                  "LED current matches independent reference within 1e-6");
            check(near(result.voltages[2] + result.nonlinearCurrents[0] * 100, 3.3),
                  "LED divider satisfies KVL");
            check(result.nonlinearAux[0] > 0 && result.nonlinearAux[0] <= 1,
                  "LED brightness is a normalized fraction of rated current");
        }
    }
    {
        // Plain diode, forward-biased through a 1 kohm resistor: cross-check against the
        // independent reference. Net 1 is the supply node, net 2 the diode anode, net 0 ground.
        const double is = 1e-12, n = 1.0;
        const auto expected = seriesDiodeCurrent(5.0, 1000, is, n);
        const auto result = solveDc({3, 0,
                                     {{"V1", DcKind::VoltageSource, 1, 0, 5.0}, {"R1", DcKind::Resistor, 1, 2, 1000}},
                                     {{"D1", NonlinearKind::Diode, {2, 0}, {is, n, 0}}}});
        check(result.success && std::abs(result.nonlinearCurrents[0] - expected) < expected * 1e-6,
              "diode current matches independent reference");
    }
    {
        // Reverse-biased diode: current is the tiny leakage current, essentially -Is. No resistor
        // is needed since the reverse current is minuscule.
        const auto result = solveDc({2, 0, {{"V1", DcKind::VoltageSource, 1, 0, -5.0}},
                                     {{"D1", NonlinearKind::Diode, {1, 0}, {1e-12, 1.0, 0}}},
                                     });
        check(result.success && result.nonlinearCurrents[0] < 0 &&
                  std::abs(result.nonlinearCurrents[0] + 1e-12) < 1e-13,
              "reverse-biased diode carries only leakage current");
    }
    {
        // Diode with series resistance: V = I*(R + Rs) + Vjunction(I). The 220 ohm here is the
        // diode's own internal Rs; a 1 kohm external resistor limits the current the same way as
        // the two tests above.
        const double is = 1e-12, n = 1.0, rs = 220.0;
        const auto expected = seriesDiodeCurrent(5.0, 1000.0 + rs, is, n);
        const auto noRs = solveDc({3, 0,
                                   {{"V1", DcKind::VoltageSource, 1, 0, 5.0}, {"R1", DcKind::Resistor, 1, 2, 1000}},
                                   {{"D1", NonlinearKind::Diode, {2, 0}, {is, n, 0}}}});
        const auto withRs = solveDc({3, 0,
                                     {{"V1", DcKind::VoltageSource, 1, 0, 5.0}, {"R1", DcKind::Resistor, 1, 2, 1000}},
                                     {{"D1", NonlinearKind::Diode, {2, 0}, {is, n, rs}}}});
        check(noRs.success && withRs.success, "diode with Rs solves");
        if (noRs.success && withRs.success) {
            check(std::abs(withRs.nonlinearCurrents[0] - expected) < expected * 1e-6,
                  "series resistance current matches independent reference");
            check(withRs.nonlinearCurrents[0] < noRs.nonlinearCurrents[0],
                  "series resistance reduces forward current");
        }
    }
    {
        // Zener shunt regulator: supply -> 1k series resistor -> zener to ground, cathode toward
        // the supply (the usual reverse-breakdown orientation, anode = ground = net 0, cathode =
        // net 2), so the regulated node (net 2) sits near +Vz for both a 12 V and a 15 V supply.
        const double is = 1e-12, n = 1.0, rs = 0, vz = 5.1, rz = 5.0;
        for (double supply : {12.0, 15.0}) {
            const auto result = solveDc({3, 0,
                                         {{"V1", DcKind::VoltageSource, 1, 0, supply},
                                          {"R1", DcKind::Resistor, 1, 2, 1000}},
                                         {{"Z1", NonlinearKind::Zener, {0, 2}, {is, n, rs, vz, rz}}}});
            check(result.success, "zener regulator solves");
            if (result.success) {
                check(std::abs(result.voltages[2] - vz) < 0.3,
                      "zener output regulates near breakdown voltage");
                check(near(result.currents[1] + result.nonlinearCurrents[0], 0.0),
                      "resistor current balances zener current (KCL at the regulated node)");
            }
        }
    }
    {
        // Parameter/topology validation.
        const auto badTerminals = solveDc({2, 0, {}, {{"D1", NonlinearKind::Diode, {0}, {1e-12, 1, 0}}}});
        check(!badTerminals.success, "nonlinear element needs exactly 2 terminals");
        const auto badNet = solveDc({2, 0, {}, {{"D1", NonlinearKind::Diode, {0, 5}, {1e-12, 1, 0}}}});
        check(!badNet.success, "nonlinear element net index in range");
        const auto badParamCount = solveDc({2, 0, {}, {{"D1", NonlinearKind::Diode, {0, 1}, {1e-12, 1}}}});
        check(!badParamCount.success, "diode needs exactly 3 parameters");
        const auto zeroIs = solveDc({2, 0, {}, {{"D1", NonlinearKind::Diode, {0, 1}, {0, 1, 0}}}});
        check(!zeroIs.success, "saturation current must be positive");
        const auto negativeN = solveDc({2, 0, {}, {{"D1", NonlinearKind::Diode, {0, 1}, {1e-12, -1, 0}}}});
        check(!negativeN.success, "ideality factor must be positive");
        const auto negativeRs = solveDc({2, 0, {}, {{"D1", NonlinearKind::Diode, {0, 1}, {1e-12, 1, -1}}}});
        check(!negativeRs.success, "series resistance must be non-negative");
        const auto zenerBadVz = solveDc({2, 0, {}, {{"Z1", NonlinearKind::Zener, {0, 1}, {1e-12, 1, 0, 0, 5}}}});
        check(!zenerBadVz.success, "zener breakdown voltage must be positive");
        const auto zenerBadRz = solveDc({2, 0, {}, {{"Z1", NonlinearKind::Zener, {0, 1}, {1e-12, 1, 0, 5, 0}}}});
        check(!zenerBadRz.success, "zener breakdown resistance must be positive");
        const auto ledBadRated =
            solveDc({2, 0, {}, {{"L1", NonlinearKind::Led, {0, 1}, {1e-18, 1, 0, -1}}}});
        check(!ledBadRated.success, "LED rated current must be non-negative");
    }
    {
        // Never crashes on an extreme case: two independent voltage sources pin a diode 10 V
        // forward-biased (no resistor limits the current), forcing many damped Newton steps
        // (the per-iteration junction voltage step is capped) to reach that fixed operating
        // point. Either a finite answer or a clear convergence error is acceptable; a crash,
        // hang, NaN or infinity is not.
        const auto extreme =
            solveDc({3, 0, {{"V1", DcKind::VoltageSource, 1, 0, 5.0}, {"V2", DcKind::VoltageSource, 2, 0, -5.0}},
                     {{"D1", NonlinearKind::Diode, {1, 2}, {1e-12, 1.0, 0}}}});
        check(extreme.success ? (!extreme.nonlinearCurrents.empty() && std::isfinite(extreme.nonlinearCurrents[0]))
                               : !extreme.error.empty(),
              "extreme nonlinear circuit never crashes: succeeds with a finite answer or fails clearly");
    }
    if (!failures) std::cout << "All DC solver tests passed.\n";
    return failures ? 1 : 0;
}
