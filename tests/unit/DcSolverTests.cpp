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
    if (!failures) std::cout << "All DC solver tests passed.\n";
    return failures ? 1 : 0;
}
