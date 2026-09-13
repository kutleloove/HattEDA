#include "hatt/electrical/Circuit.hpp"
#include <QtTest>
#include <limits>

using namespace hatt::electrical;

class ConnectivityTests : public QObject {
    Q_OBJECT
private slots:
    void crossingNeedsJunction() {
        ConnectivityInput input;
        input.pins = {{"R1", "1", {-2, 0}}, {"R2", "1", {0, -2}}};
        input.wires = {{{{-2, 0}, {2, 0}}}, {{{0, -2}, {0, 2}}}};
        auto result = buildConnectivity(input);
        QVERIFY(result.errors.empty());
        QVERIFY(result.pinNets[0] != result.pinNets[1]);
        input.junctions = {{0, 0}};
        result = buildConnectivity(input);
        QCOMPARE(result.pinNets[0], result.pinNets[1]);
    }
    void teeAndEntirePolylineConduct() {
        ConnectivityInput input;
        input.pins = {{"R1", "1", {-4, 0}}, {"R2", "1", {0, 3}}};
        input.wires = {{{{-4, 0}, {4, 0}, {4, 3}}}, {{{0, 0}, {0, 3}}}};
        const auto result = buildConnectivity(input);
        QVERIFY(result.errors.empty());
        QCOMPARE(result.nets.size(), std::size_t(1));
        QCOMPARE(result.pinNets[0], result.pinNets[1]);
    }
    void internalCollinearOverlapConducts() {
        ConnectivityInput input;
        input.pins = {{"A", "1", {0, -2}}, {"B", "1", {2, 2}}};
        input.wires = {{{{0, -2}, {0, 0}, {4, 0}, {4, -2}}},
                       {{{2, 2}, {2, 0}, {6, 0}, {6, 2}}}};
        QCOMPARE(buildConnectivity(input).nets.size(), std::size_t(1));
    }
    void crossingAtInternalVertexRemainsSeparate() {
        ConnectivityInput input;
        input.pins = {{"A", "1", {-2, 0}}, {"B", "1", {0, -2}}};
        input.wires = {{{{-2, 0}, {0, 0}, {2, 0}}}, {{{0, -2}, {0, 2}}}};
        QCOMPARE(buildConnectivity(input).nets.size(), std::size_t(2));
    }
    void pinAndNamedNodeJoinCrossing() {
        ConnectivityInput input;
        input.pins = {{"A", "1", {-2, 0}}, {"B", "1", {0, -2}}, {"C", "1", {0, 0}}};
        input.wires = {{{{-2, 0}, {2, 0}}}, {{{0, -2}, {0, 2}}}};
        QCOMPARE(buildConnectivity(input).nets.size(), std::size_t(1));
        input.pins.pop_back();
        input.names = {{{0, 0}, "0"}};
        const auto result = buildConnectivity(input);
        QCOMPARE(result.nets.size(), std::size_t(1));
        QCOMPARE(result.nets.front().name, std::string("0"));
    }
    void globalNamesAndConflicts() {
        ConnectivityInput input;
        input.pins = {{"A", "1", {1, 1}}, {"B", "1", {20, 20}}};
        input.names = {{{1, 1}, "VCC"}, {{20, 20}, "VCC"}};
        auto result = buildConnectivity(input);
        QVERIFY(result.errors.empty());
        QCOMPARE(result.nets.size(), std::size_t(1));
        QCOMPARE(result.nets.front().name, std::string("VCC"));
        input.names.push_back({{20, 20}, "0"});
        result = buildConnectivity(input);
        QCOMPARE(result.errors.size(), std::size_t(1));
        QVERIFY(result.errors.front().find("VCC") != std::string::npos);
    }
    void transformedCoordinatesAndDisconnectedPins() {
        ConnectivityInput input;
        // Coordinates already transformed by the UI: a vertical, rotated component.
        input.pins = {{"R1", "1", {10, 7.46}}, {"R1", "2", {10, 12.54}},
                      {"R2", "1", {14, 7.46}}, {"R2", "2", {14, 12.54}}};
        input.wires = {{{{10, 7.46}, {14, 7.46}}}};
        const auto result = buildConnectivity(input);
        QVERIFY(result.errors.empty());
        QCOMPARE(result.nets.size(), std::size_t(3));
        QCOMPARE(result.pinNets[0], result.pinNets[2]);
        QVERIFY(result.pinNets[1] != result.pinNets[3]);
        const auto repeated = buildConnectivity(input);
        QCOMPARE(result.pinNets, repeated.pinNets);
        for (std::size_t i = 0; i < result.nets.size(); ++i)
            QCOMPARE(result.nets[i].name, repeated.nets[i].name);
    }
    void toleranceAndGeneratedNameCollision() {
        ConnectivityInput input;
        input.pins = {{"A", "1", {0, 0.0000005}}, {"B", "1", {1, 0}},
                      {"C", "1", {2, 0.000002}}};
        input.wires = {{{{0, 0}, {2, 0}}}};
        input.names = {{{1, 0}, "N1"}};
        const auto result = buildConnectivity(input);
        QCOMPARE(result.pinNets[0], result.pinNets[1]);
        QVERIFY(result.pinNets[2] != result.pinNets[1]);
        QVERIFY(result.nets[result.pinNets[2]].name != "N1");
    }
    void rejectsInvalidInput() {
        ConnectivityInput input;
        input.pins = {{"A", "1", {0, 0}}, {"A", "1", {1, 1}}};
        QVERIFY(!buildConnectivity(input).errors.empty());
        input.pins.clear();
        const double nan = std::numeric_limits<double>::quiet_NaN();
        input.wires = {{{{0, 0}, {nan, 0}}}};
        QVERIFY(!buildConnectivity(input).errors.empty());
        input.wires.clear();
        input.junctions = {{0, nan}};
        QVERIFY(!buildConnectivity(input).errors.empty());
        input.junctions.clear();
        input.names = {{{nan, 0}, "0"}};
        QVERIFY(!buildConnectivity(input).errors.empty());
        input.names.clear();
        input.pins = {{"A", "1", {nan, 0}}};
        const auto result = buildConnectivity(input);
        QVERIFY(!result.errors.empty());
        QCOMPARE(result.pinNets[0], -1);
        QVERIFY(result.nets.empty());
    }
    void emptyInput() {
        const auto result = buildConnectivity({});
        QVERIFY(result.nets.empty());
        QVERIFY(result.errors.empty());
    }
};
QTEST_APPLESS_MAIN(ConnectivityTests)
#include "ConnectivityTests.moc"
