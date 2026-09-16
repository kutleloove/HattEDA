#include "hatt/ui/SpecctraSes.hpp"

#include <QCoreApplication>
#include <QHash>
#include <QLineF>

#include <algorithm>
#include <cmath>

namespace hatt::ui {
namespace {

QString tr(const char* text) { return QCoreApplication::translate("hatt::ui::SpecctraSes", text); }

struct Node {
    QString atom;
    QVector<Node> children;
    bool list = false;
};

class Parser {
public:
    explicit Parser(QString text) : text_(std::move(text)) {}

    QVector<Node> parse() {
        QVector<Node> result;
        skipSpace();
        while (position_ < text_.size() && error_.isEmpty()) {
            result.append(node(0));
            skipSpace();
        }
        return result;
    }

    QString error() const { return error_; }

private:
    void skipSpace() {
        while (position_ < text_.size()) {
            if (text_[position_].isSpace()) {
                ++position_;
                continue;
            }
            if (text_[position_] == QLatin1Char('#')) {
                while (position_ < text_.size() && text_[position_] != QLatin1Char('\n')) ++position_;
                continue;
            }
            break;
        }
    }

    Node node(int depth) {
        if (depth > 128) {
            error_ = tr("The session file is nested too deeply.");
            return {};
        }
        skipSpace();
        if (position_ >= text_.size()) {
            error_ = tr("The session file ended unexpectedly.");
            return {};
        }
        if (text_[position_] != QLatin1Char('(')) return atom();
        ++position_;
        Node result;
        result.list = true;
        skipSpace();
        while (position_ < text_.size() && text_[position_] != QLatin1Char(')') && error_.isEmpty()) {
            result.children.append(node(depth + 1));
            skipSpace();
        }
        if (position_ >= text_.size()) {
            error_ = tr("The session file has an unclosed section.");
            return {};
        }
        ++position_;
        return result;
    }

    Node atom() {
        Node result;
        if (text_[position_] == QLatin1Char('"')) {
            ++position_;
            while (position_ < text_.size()) {
                const QChar character = text_[position_++];
                if (character == QLatin1Char('"')) return result;
                if (character == QLatin1Char('\\') && position_ < text_.size()) {
                    result.atom += text_[position_++];
                } else {
                    result.atom += character;
                }
            }
            error_ = tr("The session file has an unclosed quoted value.");
            return {};
        }
        const qsizetype start = position_;
        while (position_ < text_.size() && !text_[position_].isSpace() &&
               text_[position_] != QLatin1Char('(') && text_[position_] != QLatin1Char(')')) {
            ++position_;
        }
        result.atom = text_.mid(start, position_ - start);
        if (result.atom.isEmpty()) error_ = tr("The session file contains an invalid value.");
        return result;
    }

    QString text_;
    qsizetype position_ = 0;
    QString error_;
};

QString head(const Node& node) {
    if (!node.list || node.children.isEmpty() || node.children.first().list) return {};
    return node.children.first().atom;
}

const Node* child(const Node& node, const QString& name) {
    for (const auto& candidate : node.children) {
        if (head(candidate).compare(name, Qt::CaseInsensitive) == 0) return &candidate;
    }
    return nullptr;
}

bool number(const Node& node, double& result) {
    if (node.list) return false;
    bool ok = false;
    result = node.atom.toDouble(&ok);
    return ok && std::isfinite(result);
}

double unitMillimetres(const QString& unit) {
    if (unit.compare(QStringLiteral("um"), Qt::CaseInsensitive) == 0) return 0.001;
    if (unit.compare(QStringLiteral("mm"), Qt::CaseInsensitive) == 0) return 1.0;
    if (unit.compare(QStringLiteral("mil"), Qt::CaseInsensitive) == 0) return 0.0254;
    if (unit.compare(QStringLiteral("inch"), Qt::CaseInsensitive) == 0) return 25.4;
    return 0.0;
}

bool scaleFor(const Node& routes, double& scale) {
    const Node* resolution = child(routes, QStringLiteral("resolution"));
    if (resolution == nullptr || resolution->children.size() < 3 || resolution->children[1].list) return false;
    double divisor = 0.0;
    if (!number(resolution->children[2], divisor) || divisor <= 0.0) return false;
    const double millimetres = unitMillimetres(resolution->children[1].atom);
    if (millimetres <= 0.0) return false;
    scale = millimetres / divisor;
    return true;
}

bool layerFor(const QString& name, BoardLayer& layer) {
    if (name.compare(QStringLiteral("F.Cu"), Qt::CaseInsensitive) == 0 || name == QStringLiteral("1")) {
        layer = BoardLayer::TopCopper;
        return true;
    }
    if (name.compare(QStringLiteral("B.Cu"), Qt::CaseInsensitive) == 0 || name == QStringLiteral("2")) {
        layer = BoardLayer::BottomCopper;
        return true;
    }
    return false;
}

} // namespace

SpecctraSesResult importSpecctraSes(const QByteArray& data) {
    SpecctraSesResult result;
    if (data.trimmed().isEmpty()) {
        result.errors << tr("The autorouter session is empty.");
        return result;
    }

    Parser parser(QString::fromUtf8(data));
    const QVector<Node> roots = parser.parse();
    if (!parser.error().isEmpty()) {
        result.errors << parser.error();
        return result;
    }
    const Node* session = nullptr;
    for (const auto& root : roots) {
        if (head(root).compare(QStringLiteral("session"), Qt::CaseInsensitive) == 0) {
            session = &root;
            break;
        }
    }
    const Node* routes = session == nullptr ? nullptr : child(*session, QStringLiteral("routes"));
    const Node* network = routes == nullptr ? nullptr : child(*routes, QStringLiteral("network_out"));
    double scale = 0.0;
    if (routes == nullptr || network == nullptr || !scaleFor(*routes, scale)) {
        result.errors << tr("The autorouter session has no supported routing section or resolution.");
        return result;
    }

    QHash<QString, double> viaDiameters;
    if (const Node* library = child(*routes, QStringLiteral("library_out"))) {
        for (const auto& padstack : library->children) {
            if (head(padstack).compare(QStringLiteral("padstack"), Qt::CaseInsensitive) != 0 ||
                padstack.children.size() < 2 || padstack.children[1].list) continue;
            for (const auto& shape : padstack.children) {
                const Node* circle = head(shape).compare(QStringLiteral("shape"), Qt::CaseInsensitive) == 0
                                         ? child(shape, QStringLiteral("circle"))
                                         : nullptr;
                if (circle == nullptr && head(shape).compare(QStringLiteral("shape"), Qt::CaseInsensitive) == 0 &&
                    shape.children.size() > 1 && head(shape.children[1]).compare(QStringLiteral("circle"), Qt::CaseInsensitive) == 0) {
                    circle = &shape.children[1];
                }
                if (circle == nullptr || circle->children.size() < 3) continue;
                double diameter = 0.0;
                if (number(circle->children[2], diameter) && diameter > 0.0) {
                    viaDiameters.insert(padstack.children[1].atom, diameter * scale);
                    break;
                }
            }
        }
    }

    for (const auto& netNode : network->children) {
        if (head(netNode).compare(QStringLiteral("net"), Qt::CaseInsensitive) != 0 ||
            netNode.children.size() < 2 || netNode.children[1].list) continue;
        const QString netName = netNode.children[1].atom;
        for (const auto& route : netNode.children) {
            const QString routeKind = head(route);
            if (routeKind.compare(QStringLiteral("wire"), Qt::CaseInsensitive) == 0) {
                const Node* path = child(route, QStringLiteral("path"));
                if (path == nullptr || path->children.size() < 7 || path->children[1].list) {
                    result.errors << tr("A routed path for net %1 is malformed.").arg(netName);
                    continue;
                }
                BoardLayer layer;
                double width = 0.0;
                if (!layerFor(path->children[1].atom, layer) || !number(path->children[2], width) || width <= 0.0) {
                    result.errors << tr("A routed path for net %1 uses an unsupported layer or width.").arg(netName);
                    continue;
                }
                SketchItem item;
                item.kind = SketchItem::Kind::Wire;
                item.layer = layer;
                item.width = width * scale;
                item.net = netName;
                bool valid = (path->children.size() - 3) % 2 == 0;
                for (qsizetype index = 3; valid && index + 1 < path->children.size(); index += 2) {
                    double x = 0.0;
                    double y = 0.0;
                    valid = number(path->children[index], x) && number(path->children[index + 1], y);
                    if (valid) item.points.append(QPointF(x * scale, -y * scale));
                }
                if (!valid || item.points.size() < 2) {
                    result.errors << tr("A routed path for net %1 has invalid coordinates.").arg(netName);
                } else {
                    result.routing.append(item);
                }
            } else if (routeKind.compare(QStringLiteral("via"), Qt::CaseInsensitive) == 0) {
                if (route.children.size() < 4 || route.children[1].list) {
                    result.errors << tr("A via for net %1 is malformed.").arg(netName);
                    continue;
                }
                double x = 0.0;
                double y = 0.0;
                if (!number(route.children[2], x) || !number(route.children[3], y)) {
                    result.errors << tr("A via for net %1 has invalid coordinates.").arg(netName);
                    continue;
                }
                SketchItem item;
                item.kind = SketchItem::Kind::Via;
                item.points = {QPointF(x * scale, -y * scale)};
                item.width = viaDiameters.value(route.children[1].atom, DefaultViaDiameter);
                item.drillDiameter = DefaultViaDrill;
                item.net = netName;
                result.routing.append(item);
            }
        }
    }
    if (result.routing.isEmpty() && result.errors.isEmpty()) {
        result.errors << tr("The autorouter session contains no tracks or vias.");
    }
    return result;
}

SketchDocument snapSpecctraRoutingToPads(const SketchDocument& routing,
                                         const SketchDocument& board, double tolerance) {
    QVector<QPointF> pads;
    for (const auto& item : board) {
        for (const auto& pad : itemPads(item)) pads.append(pad.center);
    }
    SketchDocument result = routing;
    for (auto& item : result) {
        for (QPointF& point : item.points) {
            double nearest = std::max(0.0, tolerance);
            const QPointF* target = nullptr;
            for (const QPointF& pad : pads) {
                const double distance = QLineF(point, pad).length();
                if (distance <= nearest) {
                    nearest = distance;
                    target = &pad;
                }
            }
            if (target != nullptr) point = *target;
        }
    }
    return result;
}

} // namespace hatt::ui
