#include "hatt/electrical/Circuit.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <set>
#include <utility>

namespace hatt::electrical {
namespace {
constexpr long double tolerance = 1e-6L;

bool finite(Point p) { return std::isfinite(p.x) && std::isfinite(p.y); }
bool near(Point a, Point b) {
    return std::hypot(static_cast<long double>(a.x) - b.x,
                      static_cast<long double>(a.y) - b.y) <= tolerance;
}
bool onSegment(Point p, Point a, Point b) {
    const long double dx = static_cast<long double>(b.x) - a.x;
    const long double dy = static_cast<long double>(b.y) - a.y;
    const long double length = std::hypot(dx, dy);
    if (length <= tolerance) return near(p, a);
    const long double px = static_cast<long double>(p.x) - a.x;
    const long double py = static_cast<long double>(p.y) - a.y;
    const long double along = (px * dx + py * dy) / length;
    return along >= -tolerance && along <= length + tolerance &&
           std::abs(px * dy - py * dx) / length <= tolerance;
}
bool onWire(Point p, const Wire& wire) {
    for (std::size_t i = 1; i < wire.points.size(); ++i)
        if (onSegment(p, wire.points[i - 1], wire.points[i])) return true;
    return false;
}
bool overlap(Point a, Point b, Point c, Point d) {
    const long double dx = static_cast<long double>(b.x) - a.x;
    const long double dy = static_cast<long double>(b.y) - a.y;
    const long double length = std::hypot(dx, dy);
    // Repeated internal vertices must not turn an unmarked crossing into a join.
    if (length <= tolerance || near(c, d)) return false;
    auto distance = [&](Point p) {
        return std::abs((static_cast<long double>(p.x) - a.x) * dy -
                        (static_cast<long double>(p.y) - a.y) * dx) / length;
    };
    return distance(c) <= tolerance && distance(d) <= tolerance &&
           (onSegment(a, c, d) || onSegment(b, c, d) ||
            onSegment(c, a, b) || onSegment(d, a, b));
}
struct Sets {
    std::vector<int> parent;
    explicit Sets(std::size_t count) : parent(count) {
        std::iota(parent.begin(), parent.end(), 0);
    }
    int root(int i) {
        if (parent[i] != i) parent[i] = root(parent[i]);
        return parent[i];
    }
    void join(int a, int b) {
        a = root(a); b = root(b);
        if (a != b) parent[std::max(a, b)] = std::min(a, b);
    }
};
} // namespace

ConnectivityResult buildConnectivity(const ConnectivityInput& input) {
    ConnectivityResult result;
    result.pinNets.assign(input.pins.size(), -1);
    std::set<std::pair<std::string, std::string>> identities;
    for (const auto& pin : input.pins) {
        if (!finite(pin.position)) result.errors.push_back("Non-finite pin coordinates");
        if (!identities.emplace(pin.component, pin.number).second)
            result.errors.push_back("Duplicate pin: " + pin.component + "." + pin.number);
    }
    for (const auto& wire : input.wires) {
        if (wire.points.size() < 2) result.errors.push_back("Wire requires at least two points");
        for (Point point : wire.points)
            if (!finite(point)) result.errors.push_back("Non-finite wire coordinates");
    }
    for (Point point : input.junctions)
        if (!finite(point)) result.errors.push_back("Non-finite junction coordinates");
    for (const auto& name : input.names) {
        if (!finite(name.position)) result.errors.push_back("Non-finite named node coordinates");
        if (name.name.empty()) result.errors.push_back("Empty net name");
    }
    if (!result.errors.empty()) return result;

    // Every polyline is one conductor. Only its two terminal points create T joins;
    // an incidental crossing at an internal bend still needs an explicit anchor.
    std::vector<Point> anchors;
    std::vector<unsigned> anchorLayers;
    for (const auto& pin : input.pins) {
        anchors.push_back(pin.position);
        anchorLayers.push_back(pin.layers);
    }
    for (std::size_t i = 0; i < input.junctions.size(); ++i) {
        anchors.push_back(input.junctions[i]);
        anchorLayers.push_back(i < input.junctionLayers.size() ? input.junctionLayers[i] : AllLayers);
    }
    const int nameOffset = static_cast<int>(anchors.size());
    for (const auto& name : input.names) {
        anchors.push_back(name.position);
        anchorLayers.push_back(AllLayers);
    }
    const int wireOffset = static_cast<int>(anchors.size());
    Sets sets(anchors.size() + input.wires.size());
    for (int i = 0; i < wireOffset; ++i) {
        for (int j = 0; j < i; ++j)
            if ((anchorLayers[i] & anchorLayers[j]) != 0 && near(anchors[i], anchors[j])) sets.join(i, j);
        for (std::size_t w = 0; w < input.wires.size(); ++w)
            if ((anchorLayers[i] & input.wires[w].layers) != 0 && onWire(anchors[i], input.wires[w]))
                sets.join(i, wireOffset + static_cast<int>(w));
    }
    for (std::size_t i = 0; i < input.wires.size(); ++i) {
        const auto& a = input.wires[i];
        for (std::size_t j = 0; j < i; ++j) {
            const auto& b = input.wires[j];
            if ((a.layers & b.layers) == 0) continue;
            bool connected = onWire(a.points.front(), b) || onWire(a.points.back(), b) ||
                             onWire(b.points.front(), a) || onWire(b.points.back(), a);
            for (std::size_t ai = 1; !connected && ai < a.points.size(); ++ai)
                for (std::size_t bi = 1; !connected && bi < b.points.size(); ++bi)
                    connected = overlap(a.points[ai - 1], a.points[ai],
                                        b.points[bi - 1], b.points[bi]);
            if (connected) sets.join(wireOffset + static_cast<int>(i), wireOffset + static_cast<int>(j));
        }
    }
    std::map<std::string, int> firstName;
    for (std::size_t i = 0; i < input.names.size(); ++i) {
        const int index = nameOffset + static_cast<int>(i);
        const auto [found, inserted] = firstName.emplace(input.names[i].name, index);
        if (!inserted) sets.join(index, found->second);
    }
    std::map<int, std::set<std::string>> namesByRoot;
    for (std::size_t i = 0; i < input.names.size(); ++i)
        namesByRoot[sets.root(nameOffset + static_cast<int>(i))].insert(input.names[i].name);
    for (const auto& [root, names] : namesByRoot) {
        (void)root;
        if (names.size() > 1) {
            std::string error = "Conflicting net names:";
            for (const auto& name : names) error += " " + name;
            result.errors.push_back(std::move(error));
        }
    }
    std::map<int, int> netByRoot;
    int generated = 1;
    for (int i = 0; i < static_cast<int>(sets.parent.size()); ++i) {
        const int root = sets.root(i);
        if (netByRoot.contains(root)) continue;
        const auto& names = namesByRoot[root];
        std::string name;
        if (!names.empty()) name = *names.begin();
        else {
            do { name = "N" + std::to_string(generated++); } while (firstName.contains(name));
        }
        netByRoot[root] = static_cast<int>(result.nets.size());
        result.nets.push_back({name, {}});
    }
    for (std::size_t i = 0; i < input.pins.size(); ++i) {
        const int net = netByRoot.at(sets.root(static_cast<int>(i)));
        result.pinNets[i] = net;
        result.nets[net].pins.push_back(static_cast<int>(i));
    }
    result.wireNets.assign(input.wires.size(), -1);
    for (std::size_t i = 0; i < input.wires.size(); ++i) {
        result.wireNets[i] = netByRoot.at(sets.root(wireOffset + static_cast<int>(i)));
    }
    return result;
}

std::vector<Point> junctionPoints(const ConnectivityInput& input) {
    std::vector<Point> result;
    auto seen = [&](Point p) {
        return std::any_of(result.begin(), result.end(), [&](Point q) { return near(p, q); }) ||
               std::any_of(input.junctions.begin(), input.junctions.end(),
                           [&](Point q) { return near(p, q); });
    };
    for (const auto& candidate : input.wires) {
        if (candidate.points.size() < 2) continue;
        for (Point p : {candidate.points.front(), candidate.points.back()}) {
            if (!finite(p) || seen(p)) continue;
            int branches = 0;
            for (const auto& wire : input.wires) {
                if (wire.points.size() < 2) continue;
                const int ends = int(near(p, wire.points.front())) + int(near(p, wire.points.back()));
                if (ends > 0) branches += ends;
                else if (onWire(p, wire)) branches += 2;
            }
            for (const auto& pin : input.pins)
                if (near(p, pin.position)) ++branches;
            if (branches >= 3) result.push_back(p);
        }
    }
    return result;
}
} // namespace hatt::electrical
