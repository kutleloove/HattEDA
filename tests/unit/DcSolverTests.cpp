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
    // #62 part 2: BJT (Ebers-Moll) and MOSFET (level-1).
    {
        // NPN common-emitter, fixed base bias: Vcc -[Rb]- Base, Vcc -[Rc]- Collector, Emitter =
        // ground. Independent reference: bisect Vbe so Ib = (Is/BF)*(exp(Vbe/Vt)-1) matches
        // (Vcc-Vbe)/Rb, ignoring the reverse (Vbc) term - valid whenever Vbc ends up clearly
        // negative (deep active region), checked below rather than assumed.
        const double vcc = 10.0, rb = 470000.0, rc = 1000.0, is = 1e-15, bf = 100.0, br = 1.0;
        double lo = 0.0, hi = 0.85;
        for (int iter = 0; iter < 200; ++iter) {
            const double mid = (lo + hi) / 2;
            const double ib = (is / bf) * (std::exp(mid / Vt) - 1.0);
            if (ib < (vcc - mid) / rb) lo = mid; else hi = mid;
        }
        const double vbeRef = (lo + hi) / 2;
        const double icRef = is * std::exp(vbeRef / Vt); // ignoring the negligible -is and Vbc terms
        const double vcRef = vcc - icRef * rc;
        check(vbeRef - vcRef < -3.0, "NPN reference circuit is deep in the active region (Vbc << 0)");
        const auto result = solveDc({4, 0,
                                     {{"V1", DcKind::VoltageSource, 1, 0, vcc}, {"Rb", DcKind::Resistor, 1, 2, rb},
                                      {"Rc", DcKind::Resistor, 1, 3, rc}},
                                     {{"Q1", NonlinearKind::BjtNpn, {3, 2, 0}, {is, bf, br}}}});
        check(result.success, "NPN common-emitter solves");
        if (result.success) {
            check(std::abs(result.nonlinearCurrents[0] - icRef) < icRef * 0.02,
                  "NPN collector current matches independent reference within 2%");
            check(std::abs(result.voltages[3] - vcRef) < 0.05, "NPN collector voltage matches independent reference");
            check(std::abs(result.voltages[2] - vbeRef) < 1e-6, "NPN base voltage (=Vbe) matches independent reference");
        }
    }
    {
        // PNP common-emitter mirror: Emitter tied to Vcc, base pulled toward ground through Rb
        // (PNP sources Ib out of its base into Rb), collector pulled toward ground through Rc
        // (PNP sources Ic out of its collector into Rc). Same bisection idea, mirrored.
        const double vcc = 10.0, rb = 470000.0, rc = 1000.0, is = 1e-15, bf = 100.0, br = 1.0;
        double lo = 0.0, hi = 0.85;
        for (int iter = 0; iter < 200; ++iter) {
            const double mid = (lo + hi) / 2; // mid = Veb candidate
            const double ib = (is / bf) * (std::exp(mid / Vt) - 1.0);
            const double vb = vcc - mid;
            if (ib * rb < vb) lo = mid; else hi = mid;
        }
        const double vebRef = (lo + hi) / 2;
        const double vbRef = vcc - vebRef;
        const double icRef = is * std::exp(vebRef / Vt); // Isc analogue, ignoring the Vcb term
        const double vcRef = icRef * rc; // collector sources current into Rc toward ground
        check(vcRef - vbRef < -3.0, "PNP reference circuit is deep in the active region (Vcb << 0, i.e. Vc << Vb)");
        const auto result = solveDc({4, 0,
                                     {{"V1", DcKind::VoltageSource, 1, 0, vcc}, {"Rb", DcKind::Resistor, 2, 0, rb},
                                      {"Rc", DcKind::Resistor, 3, 0, rc}},
                                     {{"Q1", NonlinearKind::BjtPnp, {3, 2, 1}, {is, bf, br}}}});
        check(result.success, "PNP common-emitter solves");
        if (result.success) {
            check(std::abs(result.nonlinearCurrents[0] + icRef) < icRef * 0.02,
                  "PNP collector current (into collector, negative when sourcing) matches reference within 2%");
            check(std::abs(result.voltages[3] - vcRef) < 0.05, "PNP collector voltage matches independent reference");
            check(std::abs(result.voltages[2] - vbRef) < 1e-6, "PNP base voltage matches independent reference");
        }
    }
    // Independent, region-aware reference for an NMOS/PMOS switch: bisects Id so that
    // Vds = Vdd - Id*Rd is self-consistent with the level-1 square law in whichever region it
    // lands in (mirrors solveDc's own model, but computed with plain bisection, not Newton).
    const auto mosfetReferenceId = [](double vdd, double rd, double vgs, double vto, double k) {
        const double vov = vgs - vto;
        if (vov <= 0) return 0.0;
        double lo = 0.0, hi = vdd / rd;
        for (int iter = 0; iter < 200; ++iter) {
            const double mid = (lo + hi) / 2;
            const double vds = vdd - mid * rd;
            const double idModel = vds < vov ? k * (2.0 * vov * vds - vds * vds) : k * vov * vov;
            if (idModel > mid) lo = mid; else hi = mid;
        }
        return (lo + hi) / 2;
    };
    {
        // NMOS switch, saturation: Vdd=10 V, Rd=1 kohm, gate driven directly to 5 V (Vgs=5V since
        // source is grounded), Vto=2 V, K=0.5 mA/V^2 -> Id = K*(Vgs-Vto)^2 = 4.5 mA exactly (chosen
        // so Vds stays above the overdrive voltage, confirmed below), no iteration needed for the
        // reference at all.
        const double vdd = 10.0, rd = 1000.0, vgs = 5.0, vto = 2.0, k = 0.0005;
        const double idRef = k * (vgs - vto) * (vgs - vto);
        const double vdRef = vdd - idRef * rd;
        check(vdRef >= vgs - vto, "NMOS reference circuit is in saturation");
        const auto result = solveDc({4, 0,
                                     {{"Vdd", DcKind::VoltageSource, 1, 0, vdd}, {"Rd", DcKind::Resistor, 1, 2, rd},
                                      {"Vg", DcKind::VoltageSource, 3, 0, vgs}},
                                     {{"M1", NonlinearKind::NMosfet, {2, 3, 0}, {vto, k}}}});
        check(result.success, "NMOS saturation switch solves");
        if (result.success) {
            check(std::abs(result.nonlinearCurrents[0] - idRef) < idRef * 0.02,
                  "NMOS drain current matches independent reference within 2%");
            check(std::abs(result.voltages[2] - vdRef) < 0.05, "NMOS drain voltage matches independent reference");
        }
    }
    {
        // NMOS switch, triode (logic-level, low Rds-on): Vdd=5V, gate tied straight to Vdd
        // (Vgs=5V), Vto=1V, K=10 mA/V^2, Rd=100 ohm -> small Vds, needs the bisection reference
        // since Id and Vds are mutually dependent in this region.
        const double vdd = 5.0, rd = 100.0, vgs = 5.0, vto = 1.0, k = 0.01;
        const double idRef = mosfetReferenceId(vdd, rd, vgs, vto, k);
        const double vdRef = vdd - idRef * rd;
        check(vdRef < vgs - vto, "NMOS reference circuit is in triode");
        const auto result = solveDc({3, 0,
                                     {{"Vdd", DcKind::VoltageSource, 1, 0, vdd}, {"Rd", DcKind::Resistor, 1, 2, rd}},
                                     {{"M1", NonlinearKind::NMosfet, {2, 1, 0}, {vto, k}}}});
        check(result.success, "NMOS triode switch solves");
        if (result.success) {
            check(std::abs(result.nonlinearCurrents[0] - idRef) < std::max(idRef * 0.02, 1e-6),
                  "NMOS (triode) drain current matches independent reference within 2%");
            check(std::abs(result.voltages[2] - vdRef) < 0.05, "NMOS (triode) drain voltage matches independent reference");
        }
    }
    {
        // PMOS high-side switch mirror: Vdd=10V at the source, gate driven to 5V (Vsg=5V), Vto=-2V
        // (K's magnitude the same 0.5 mA/V^2 as the NMOS test), Rd from drain to ground - mirrors
        // the NMOS saturation test exactly (same overdrive, same Id/Vd numbers).
        const double vdd = 10.0, rd = 1000.0, vg = 5.0, vto = -2.0, k = 0.0005;
        const double vov = (vdd - vg) - std::abs(vto); // Vsg - |Vto|
        const double idRef = k * vov * vov;
        const double vdRef = idRef * rd; // drain sources current into Rd toward ground
        check(vdd - vdRef >= vov, "PMOS reference circuit is in saturation (Vsd >= overdrive)");
        const auto result = solveDc({4, 0,
                                     {{"Vdd", DcKind::VoltageSource, 1, 0, vdd}, {"Rd", DcKind::Resistor, 2, 0, rd},
                                      {"Vg", DcKind::VoltageSource, 3, 0, vg}},
                                     {{"M1", NonlinearKind::PMosfet, {2, 3, 1}, {vto, k}}}});
        check(result.success, "PMOS saturation switch solves");
        if (result.success) {
            check(std::abs(result.nonlinearCurrents[0] + idRef) < idRef * 0.02,
                  "PMOS drain current (into drain, negative when sourcing) matches reference within 2%");
            check(std::abs(result.voltages[2] - vdRef) < 0.05, "PMOS drain voltage matches independent reference");
        }
    }
    {
        // Parameter/topology validation for BJT and MOSFET.
        const auto bjtBadTerminals = solveDc({3, 0, {}, {{"Q1", NonlinearKind::BjtNpn, {0, 1}, {1e-15, 100, 1}}}});
        check(!bjtBadTerminals.success, "BJT needs exactly 3 terminals");
        const auto bjtBadParamCount =
            solveDc({3, 0, {}, {{"Q1", NonlinearKind::BjtNpn, {0, 1, 2}, {1e-15, 100}}}});
        check(!bjtBadParamCount.success, "BJT needs exactly 3 parameters");
        const auto bjtZeroIs = solveDc({3, 0, {}, {{"Q1", NonlinearKind::BjtNpn, {0, 1, 2}, {0, 100, 1}}}});
        check(!bjtZeroIs.success, "BJT saturation current must be positive");
        const auto bjtZeroBf = solveDc({3, 0, {}, {{"Q1", NonlinearKind::BjtNpn, {0, 1, 2}, {1e-15, 0, 1}}}});
        check(!bjtZeroBf.success, "BJT forward beta must be positive");
        const auto bjtZeroBr = solveDc({3, 0, {}, {{"Q1", NonlinearKind::BjtNpn, {0, 1, 2}, {1e-15, 100, 0}}}});
        check(!bjtZeroBr.success, "BJT reverse beta must be positive");
        const auto mosBadTerminals = solveDc({3, 0, {}, {{"M1", NonlinearKind::NMosfet, {0, 1}, {2.0, 0.001}}}});
        check(!mosBadTerminals.success, "MOSFET needs exactly 3 terminals");
        const auto mosBadParamCount = solveDc({3, 0, {}, {{"M1", NonlinearKind::NMosfet, {0, 1, 2}, {2.0}}}});
        check(!mosBadParamCount.success, "MOSFET needs exactly 2 parameters");
        const auto mosZeroK = solveDc({3, 0, {}, {{"M1", NonlinearKind::NMosfet, {0, 1, 2}, {2.0, 0}}}});
        check(!mosZeroK.success, "MOSFET K must be positive");
    }
    {
        // Never crashes: an NMOS with its gate left unconnected to anything else is a floating
        // net (no gate current is ever modelled), reported as a clear error, not a crash.
        const auto floatingGate = solveDc({4, 0, {{"Vdd", DcKind::VoltageSource, 1, 0, 10.0}, {"Rd", DcKind::Resistor, 1, 2, 1000}},
                                           {{"M1", NonlinearKind::NMosfet, {2, 3, 0}, {2.0, 0.0005}}}});
        check(!floatingGate.success && floatingGate.error.find("loating") != std::string::npos,
              "MOSFET with an unbiased gate is reported as a floating net, not a crash");
    }
    // #62 part 3: op-amp, logic gates, and a bistable circuit built from them (warm start).
    {
        // Non-inverting amplifier: Vin=1V -> op-amp non-inverting input; feedback divider
        // (R2=9k from output to inverting input, R1=1k from inverting input to ground) sets
        // gain = 1 + R2/R1 = 10, well within the +-15 V rails. Independent reference: the
        // standard ideal-op-amp virtual-short formula, not derived from this solver.
        const double vin = 1.0, r1 = 1000.0, r2 = 9000.0, gain = 1e5, gOut = 1000.0, vpos = 15.0, vneg = -15.0;
        const double expectedVout = vin * (1.0 + r2 / r1);
        const auto result = solveDc({4, 0,
                                     {{"Vin", DcKind::VoltageSource, 1, 0, vin}, {"Rfb", DcKind::Resistor, 2, 3, r2},
                                      {"Rg", DcKind::Resistor, 3, 0, r1}},
                                     {{"U1", NonlinearKind::OpAmp, {1, 3, 2}, {gain, gOut, vpos, vneg}}}});
        check(result.success, "op-amp non-inverting amplifier solves");
        if (result.success) {
            // Finite gain (1e5) leaves a small, expected error of about Vout/gain.
            check(std::abs(result.voltages[2] - expectedVout) < 1e-2,
                  "op-amp output matches the ideal gain formula (1 + R2/R1)");
            check(std::abs(result.voltages[3] - vin) < 1e-2, "op-amp virtual short: Vminus tracks Vplus");
        }
    }
    {
        // Same amplifier, but Vin=5V would need Vout=50V: the output must saturate at +Vpos
        // instead (supply-rail-limited output).
        const double vin = 5.0, r1 = 1000.0, r2 = 9000.0, gain = 1e5, gOut = 1000.0, vpos = 15.0, vneg = -15.0;
        const auto result = solveDc({4, 0,
                                     {{"Vin", DcKind::VoltageSource, 1, 0, vin}, {"Rfb", DcKind::Resistor, 2, 3, r2},
                                      {"Rg", DcKind::Resistor, 3, 0, r1}},
                                     {{"U1", NonlinearKind::OpAmp, {1, 3, 2}, {gain, gOut, vpos, vneg}}}});
        check(result.success, "op-amp saturated amplifier solves");
        if (result.success)
            // A small symmetry-breaking nudge (see the solver's comment near "gigaohm") can push
            // a fully saturated output a hair past the rail; allow a generous margin for it.
            check(result.voltages[2] > 14.0 && result.voltages[2] < vpos + 0.01,
                  "op-amp output saturates near +Vpos");
    }
    {
        // Parameter/topology validation for the op-amp.
        const auto badParamCount = solveDc({3, 0, {}, {{"U1", NonlinearKind::OpAmp, {0, 1, 2}, {1e5, 1000}}}});
        check(!badParamCount.success, "op-amp needs exactly 4 parameters");
        const auto badGain = solveDc({3, 0, {}, {{"U1", NonlinearKind::OpAmp, {0, 1, 2}, {0, 1000, 15, -15}}}});
        check(!badGain.success, "op-amp gain must be positive");
        const auto badRails = solveDc({3, 0, {}, {{"U1", NonlinearKind::OpAmp, {0, 1, 2}, {1e5, 1000, -15, 15}}}});
        check(!badRails.success, "op-amp Vpos must exceed Vneg");
    }
    // Shared logic gate parameters used by every gate test below: 0/5 V logic, threshold at
    // mid-rail, a sharp (but smooth) transition, and a driver conductance that dominates any
    // stray load with margin (the ideal inputs draw no current at all, so nothing else competes).
    constexpr double Vth = 2.5, Steepness = 50.0, Vol = 0.0, Voh = 5.0, GOut = 0.1;
    const auto gate = [&](const char* ref, int output, int inputA, int inputB, LogicFunction fn) {
        return NonlinearElement{ref, NonlinearKind::LogicGate, {output, inputA, inputB},
                                {static_cast<double>(fn), Vth, Steepness, Vol, Voh, GOut}};
    };
    {
        // AND: both inputs high -> output high; otherwise low. Nets: 0=ground, 1=A, 2=B, 3=out.
        for (const auto& combo : {std::pair{0.0, 0.0}, {0.0, Voh}, {Voh, 0.0}, {Voh, Voh}}) {
            const auto result = solveDc({4, 0,
                                         {{"VA", DcKind::VoltageSource, 1, 0, combo.first},
                                          {"VB", DcKind::VoltageSource, 2, 0, combo.second}},
                                         {gate("G1", 3, 1, 2, LogicFunction::And)}});
            check(result.success, "AND gate solves");
            if (result.success) {
                const bool expectHigh = combo.first > Vth && combo.second > Vth;
                check((result.voltages[3] > Voh / 2) == expectHigh, "AND gate output matches truth table");
                check(result.nonlinearAux[0] >= 0.0 && result.nonlinearAux[0] <= 1.0,
                      "AND gate digital level is normalized to [0, 1]");
            }
        }
    }
    {
        // NOT: nets = {output, input, unused} - wire "unused" to the input itself.
        for (double vin : {0.0, Voh}) {
            const auto result = solveDc({3, 0, {{"VA", DcKind::VoltageSource, 1, 0, vin}},
                                         {gate("G1", 2, 1, 1, LogicFunction::Not)}});
            check(result.success, "NOT gate solves");
            if (result.success) check((result.voltages[2] > Voh / 2) == (vin <= Vth), "NOT gate output matches truth table");
        }
    }
    {
        // XOR: high only when inputs differ.
        for (const auto& combo : {std::pair{0.0, 0.0}, {0.0, Voh}, {Voh, 0.0}, {Voh, Voh}}) {
            const auto result = solveDc({4, 0,
                                         {{"VA", DcKind::VoltageSource, 1, 0, combo.first},
                                          {"VB", DcKind::VoltageSource, 2, 0, combo.second}},
                                         {gate("G1", 3, 1, 2, LogicFunction::Xor)}});
            check(result.success, "XOR gate solves");
            if (result.success) {
                const bool expectHigh = (combo.first > Vth) != (combo.second > Vth);
                check((result.voltages[3] > Voh / 2) == expectHigh, "XOR gate output matches truth table");
            }
        }
    }
    {
        // Parameter/topology validation for logic gates.
        const auto badParamCount = solveDc({4, 0, {}, {{"G1", NonlinearKind::LogicGate, {0, 1, 2}, {1, 2.5, 50, 0, 5}}}});
        check(!badParamCount.success, "logic gate needs exactly 6 parameters");
        const auto badFunction =
            solveDc({4, 0, {}, {{"G1", NonlinearKind::LogicGate, {0, 1, 2}, {99, 2.5, 50, 0, 5, 0.1}}}});
        check(!badFunction.success, "logic gate function id must be a valid LogicFunction");
        const auto badSwing =
            solveDc({4, 0, {}, {{"G1", NonlinearKind::LogicGate, {0, 1, 2}, {1, 2.5, 50, 5, 0, 0.1}}}});
        check(!badSwing.success, "logic gate Voh must exceed Vol");
    }
    {
        // SR latch from two cross-coupled NAND gates (active-low set/reset) - the classic
        // "school" bistable circuit (#66). Nets: 0=ground, 1=Sbar, 2=Rbar, 3=Q1, 4=Q2.
        const auto srLatch = [&](double sbar, double rbar, const std::vector<double>* seed) {
            DcCircuit circuit{5, 0,
                              {{"VS", DcKind::VoltageSource, 1, 0, sbar}, {"VR", DcKind::VoltageSource, 2, 0, rbar}},
                              {gate("G1", 3, 1, 4, LogicFunction::Nand), gate("G2", 4, 2, 3, LogicFunction::Nand)}};
            if (seed) circuit.initialVoltages = *seed;
            return solveDc(circuit);
        };
        const auto set = srLatch(0.0, Voh, nullptr); // Sbar active (low) sets Q1 high
        check(set.success && set.voltages[3] > Voh / 2 && set.voltages[4] < Voh / 2,
              "SR latch: set drives Q1 high and Q2 low");
        if (set.success) {
            const auto hold = srLatch(Voh, Voh, &set.voltages); // both inactive: warm-started from "set"
            check(hold.success && hold.voltages[3] > Voh / 2 && hold.voltages[4] < Voh / 2,
                  "SR latch: warm-started hold preserves the set state");
            const auto reset = srLatch(Voh, 0.0, &hold.voltages); // Rbar active (low) resets Q1 low
            check(reset.success && reset.voltages[3] < Voh / 2 && reset.voltages[4] > Voh / 2,
                  "SR latch: reset drives Q1 low and Q2 high");
        }
        // Never crashes without a warm start either. A perfectly symmetric hold condition with a
        // 0 V guess is a textbook hard case for Newton-Raphson (finding one side of an unstable
        // equilibrium's basin boundary): it may settle on either stable state, or - as it does
        // here - fail to converge within the iteration budget instead. Either outcome is
        // acceptable; a crash, hang, NaN or infinity is not. The warm-started path above is the
        // one #66's latch template actually relies on, and it converges cleanly.
        const auto holdNoSeed = srLatch(Voh, Voh, nullptr);
        check(holdNoSeed.success ? std::isfinite(holdNoSeed.voltages[3]) : !holdNoSeed.error.empty(),
              "SR latch hold without a warm start never crashes: succeeds or fails clearly");
    }
    if (!failures) std::cout << "All DC solver tests passed.\n";
    return failures ? 1 : 0;
}
