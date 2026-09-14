#pragma once

#include <atomic>
#include <string>
#include <vector>

namespace hatt::electrical {

// Transient millimetre snapshots, never the persistent document format.
struct Point { double x = 0; double y = 0; };
struct Pin { std::string component; std::string number; Point position; };
struct Wire { std::vector<Point> points; };
struct NamedNode { Point position; std::string name; };
struct ConnectivityInput {
    std::vector<Pin> pins;
    std::vector<Wire> wires;
    std::vector<Point> junctions;
    std::vector<NamedNode> names; // "0" is ground; equal names join globally.
};
struct Net { std::string name; std::vector<int> pins; };
struct ConnectivityResult {
    std::vector<Net> nets;
    std::vector<int> pinNets; // Index into nets for each input pin.
    std::vector<std::string> errors;
};
ConnectivityResult buildConnectivity(const ConnectivityInput& input);
// Points where a junction dot must be drawn: a wire end that joins three or more conductor
// branches (wire ends, wires passing through, pins). Plain crossings never qualify, matching
// buildConnectivity. Explicit junctions are excluded; they are drawn by their own symbol.
std::vector<Point> junctionPoints(const ConnectivityInput& input);

// DC operating point models: a capacitor is open (no current), an inductor is a short (a 0 V
// source whose current is reported). Nets reached from ground only through capacitors get a
// 1e-12 S tie to ground (SPICE GMIN) so they solve instead of being singular.
enum class DcKind { Resistor, VoltageSource, Capacitor, Inductor };
struct DcElement {
    std::string reference;
    DcKind kind = DcKind::Resistor;
    int positive = -1; // Net indices; current positive -> negative.
    int negative = -1;
    double value = 0; // ohms, volts, farads or henries, SI. Unused by the DC models of C and L.
};
struct DcCircuit {
    int netCount = 0;
    int ground = -1;
    std::vector<DcElement> elements;
};
struct DcResult {
    bool success = false;
    std::vector<double> voltages; // Indexed by net.
    std::vector<double> currents; // Indexed by element.
    std::string error;
};
DcResult solveDc(const DcCircuit& circuit, const std::atomic_bool* cancelled = nullptr);
bool parseSpiceValue(const std::string& text, double& value);

} // namespace hatt::electrical
