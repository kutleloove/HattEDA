#pragma once

#include <atomic>
#include <string>
#include <vector>

namespace hatt::electrical {

// Transient millimetre snapshots, never the persistent document format.
struct Point { double x = 0; double y = 0; };
// Conductor layer bit masks: two conductors only join when their masks share a bit. The default
// (every bit) is a single-layer drawing such as a schematic; boards use one bit per copper layer.
inline constexpr unsigned AllLayers = ~0u;
struct Pin { std::string component; std::string number; Point position; unsigned layers = AllLayers; };
struct Wire { std::vector<Point> points; unsigned layers = AllLayers; };
struct NamedNode { Point position; std::string name; };
struct ConnectivityInput {
    std::vector<Pin> pins;
    std::vector<Wire> wires;
    std::vector<Point> junctions;
    // Layers of each junction, parallel to `junctions`; missing entries mean AllLayers. A via is a
    // junction on several copper layers.
    std::vector<unsigned> junctionLayers;
    std::vector<NamedNode> names; // "0" is ground; equal names join globally.
};
struct Net { std::string name; std::vector<int> pins; };
struct ConnectivityResult {
    std::vector<Net> nets;
    std::vector<int> pinNets; // Index into nets for each input pin.
    std::vector<int> wireNets; // Index into nets for each input wire (issue #47: wire net labels).
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
enum class DcKind { Resistor, VoltageSource, CurrentSource, Capacitor, Inductor };
struct DcElement {
    std::string reference;
    DcKind kind = DcKind::Resistor;
    int positive = -1; // Net indices; current positive -> negative.
    int negative = -1;
    double value = 0; // ohms, volts, farads or henries, SI. Unused by the DC models of C and L.
};

// Nonlinear DC operating-point models (#62), solved by Newton-Raphson alongside the linear
// elements above in the same MNA system. `nets` gives terminal net indices in the order documented
// per kind; `parameters` gives SI values in the order documented per kind.
enum class NonlinearKind { Diode, Zener, Led };
struct NonlinearElement {
    std::string reference;
    NonlinearKind kind = NonlinearKind::Diode;
    // Diode/Zener/Led: nets = {anode, cathode}.
    std::vector<int> nets;
    // Diode: {Is (A), n, Rs (ohm, may be 0)}.
    // Zener: {Is, n, Rs, Vz (breakdown voltage, positive), Rz (breakdown slope resistance, ohm)}.
    // Led: {Is, n, Rs, ratedCurrent (A, for the brightness output; 0 disables brightness)}.
    std::vector<double> parameters;
};

struct DcCircuit {
    int netCount = 0;
    int ground = -1;
    std::vector<DcElement> elements;
    std::vector<NonlinearElement> nonlinear;
};
struct DcResult {
    bool success = false;
    std::vector<double> voltages; // Indexed by net.
    std::vector<double> currents; // Indexed by element.
    // Indexed by `DcCircuit::nonlinear`: current into the first listed terminal (e.g. anode).
    std::vector<double> nonlinearCurrents;
    // Indexed by `DcCircuit::nonlinear`: kind-specific secondary output, 0 where unused.
    // Led: forward current / ratedCurrent, clamped to [0, 1] (0 when ratedCurrent is 0).
    std::vector<double> nonlinearAux;
    std::string error;
};
DcResult solveDc(const DcCircuit& circuit, const std::atomic_bool* cancelled = nullptr);
bool parseSpiceValue(const std::string& text, double& value);

} // namespace hatt::electrical
