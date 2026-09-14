#include "hatt/ui/GerberExport.hpp"

#include "hatt/ui/StrokeFont.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRectF>
#include <QSaveFile>
#include <QSet>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <map>

namespace hatt::ui {
namespace {

constexpr double Pi = 3.14159265358979323846;

QPointF camPoint(QPointF editor) { return {editor.x(), -editor.y()}; }

QVector<QPointF> camPoints(const QVector<QPointF>& editor) {
    QVector<QPointF> result;
    result.reserve(editor.size());
    for (const QPointF& point : editor) result.append(camPoint(point));
    return result;
}

CamAperture circle(double diameter) {
    return {CamApertureShape::Circle, std::max(0.001, diameter), 0.0};
}

CamAperture padAperture(const PlacedPad& pad, double expansion) {
    const double width = pad.width + 2.0 * expansion;
    const double height = pad.height + 2.0 * expansion;
    switch (pad.shape) {
    case PadShape::Round: return circle(width);
    case PadShape::Rect:
        return {CamApertureShape::Rectangle, std::max(0.001, width), std::max(0.001, height)};
    case PadShape::Oval:
        if (qFuzzyCompare(width, height)) return circle(width);
        return {CamApertureShape::Obround, std::max(0.001, width), std::max(0.001, height)};
    }
    return circle(width);
}

CamLayerKind copperLayer(bool bottom) { return bottom ? CamLayerKind::BottomCopper : CamLayerKind::TopCopper; }

// Editor outline of a drawn graphic, and whether it is closed.
QVector<QPointF> graphicOutline(const SketchItem& item, bool& closed) {
    closed = false;
    switch (item.kind) {
    case SketchItem::Kind::Line:
    case SketchItem::Kind::Polyline:
        closed = item.closed && item.points.size() > 2;
        return item.points;
    case SketchItem::Kind::Rectangle: {
        const QRectF rect = QRectF(item.points.value(0), item.points.value(1)).normalized();
        closed = true;
        return {rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft()};
    }
    case SketchItem::Kind::Circle: {
        const QPointF center = item.points.value(0);
        const double radius = QLineF(center, item.points.value(1)).length();
        QVector<QPointF> points;
        for (int i = 0; i < 72; ++i) {
            const double angle = 2 * Pi * i / 72;
            points.append(center + QPointF(radius * std::cos(angle), radius * std::sin(angle)));
        }
        closed = true;
        return points;
    }
    case SketchItem::Kind::Arc:
        if (item.points.size() < 3) return {};
        return arcSamples(item.points.value(0), item.points.value(1), item.points.value(2), 48);
    default:
        return {};
    }
}

CamLayerKind camLayerFor(BoardLayer layer) {
    switch (layer) {
    case BoardLayer::TopCopper: return CamLayerKind::TopCopper;
    case BoardLayer::BottomCopper: return CamLayerKind::BottomCopper;
    case BoardLayer::TopSilk: return CamLayerKind::TopSilk;
    case BoardLayer::BottomSilk: return CamLayerKind::BottomSilk;
    case BoardLayer::TopResist: return CamLayerKind::TopMask;
    case BoardLayer::BottomResist: return CamLayerKind::BottomMask;
    case BoardLayer::TopPaste: return CamLayerKind::TopPaste;
    case BoardLayer::BottomPaste: return CamLayerKind::BottomPaste;
    case BoardLayer::BoardEdge: return CamLayerKind::Outline;
    }
    return CamLayerKind::TopSilk;
}

double strokeWidthFor(CamLayerKind kind) {
    switch (kind) {
    case CamLayerKind::TopSilk:
    case CamLayerKind::BottomSilk: return CamSilkLineWidth;
    case CamLayerKind::Outline: return CamOutlineLineWidth;
    default: return CamCopperLineWidth;
    }
}

struct LayerInfo {
    const char* function;
    const char* suffix;
};

LayerInfo layerInfo(CamLayerKind kind) {
    switch (kind) {
    case CamLayerKind::TopCopper: return {"Copper,L1,Top", "F_Cu.gtl"};
    case CamLayerKind::BottomCopper: return {"Copper,L2,Bot", "B_Cu.gbl"};
    case CamLayerKind::TopSilk: return {"Legend,Top", "F_SilkS.gto"};
    case CamLayerKind::BottomSilk: return {"Legend,Bot", "B_SilkS.gbo"};
    case CamLayerKind::TopMask: return {"Soldermask,Top", "F_Mask.gts"};
    case CamLayerKind::BottomMask: return {"Soldermask,Bot", "B_Mask.gbs"};
    case CamLayerKind::TopPaste: return {"Paste,Top", "F_Paste.gtp"};
    case CamLayerKind::BottomPaste: return {"Paste,Bot", "B_Paste.gbp"};
    case CamLayerKind::Outline: return {"Profile,NP", "Edge_Cuts.gm1"};
    }
    return {"Other,Unknown", "unknown.gbr"};
}

QByteArray number(double value) { return QByteArray::number(value, 'f', 6); }

QByteArray coordinate(QPointF point) {
    return "X" + QByteArray::number(qRound64(point.x() * 1e6)) + "Y" + QByteArray::number(qRound64(point.y() * 1e6));
}

QByteArray apertureTemplate(const CamAperture& aperture) {
    switch (aperture.shape) {
    case CamApertureShape::Circle: return "C," + number(aperture.width);
    case CamApertureShape::Rectangle: return "R," + number(aperture.width) + "X" + number(aperture.height);
    case CamApertureShape::Obround: return "O," + number(aperture.width) + "X" + number(aperture.height);
    }
    return "C," + number(aperture.width);
}

} // namespace

QString camLayerName(CamLayerKind kind) {
    switch (kind) {
    case CamLayerKind::TopCopper: return boardLayerName(BoardLayer::TopCopper);
    case CamLayerKind::BottomCopper: return boardLayerName(BoardLayer::BottomCopper);
    case CamLayerKind::TopSilk: return boardLayerName(BoardLayer::TopSilk);
    case CamLayerKind::BottomSilk: return boardLayerName(BoardLayer::BottomSilk);
    case CamLayerKind::TopMask: return boardLayerName(BoardLayer::TopResist);
    case CamLayerKind::BottomMask: return boardLayerName(BoardLayer::BottomResist);
    case CamLayerKind::TopPaste: return boardLayerName(BoardLayer::TopPaste);
    case CamLayerKind::BottomPaste: return boardLayerName(BoardLayer::BottomPaste);
    case CamLayerKind::Outline: return boardLayerName(BoardLayer::BoardEdge);
    }
    return {};
}

CamOutput buildCamOutput(const SketchDocument& board, const CamOptions& options) {
    CamOutput output;
    for (int kind = 0; kind <= static_cast<int>(CamLayerKind::Outline); ++kind) {
        output.layers.append({static_cast<CamLayerKind>(kind), {}});
    }
    auto layer = [&output](CamLayerKind kind) -> QVector<CamPrimitive>& {
        return output.layers[static_cast<int>(kind)].primitives;
    };
    auto stroke = [&](CamLayerKind kind, const QVector<QPointF>& editor, bool closed, double width) {
        if (editor.size() < 2) return;
        CamPrimitive primitive{CamPrimitive::Kind::Stroke, circle(width), camPoints(editor)};
        if (closed) primitive.points.append(primitive.points.first());
        layer(kind).append(primitive);
    };
    auto region = [&](CamLayerKind kind, const QVector<QPointF>& editor) {
        if (editor.size() < 3) return;
        layer(kind).append({CamPrimitive::Kind::Region, {}, camPoints(editor)});
    };
    auto flash = [&](CamLayerKind kind, const PlacedPad& pad, double expansion) {
        layer(kind).append({CamPrimitive::Kind::Flash, padAperture(pad, expansion), {camPoint(pad.center)}});
    };
    auto addPads = [&](const SketchItem& item, bool via) {
        for (const PlacedPad& pad : itemPads(item)) {
            const bool top = (pad.layers & layerBit(BoardLayer::TopCopper)) != 0;
            const bool bottom = (pad.layers & layerBit(BoardLayer::BottomCopper)) != 0;
            for (const bool side : {false, true}) {
                if (!(side ? bottom : top)) continue;
                flash(copperLayer(side), pad, 0.0);
                if (via) continue; // vias are tented
                flash(side ? CamLayerKind::BottomMask : CamLayerKind::TopMask, pad, CamMaskExpansion);
                if (pad.drill <= 0.0) flash(side ? CamLayerKind::BottomPaste : CamLayerKind::TopPaste, pad, 0.0);
            }
            if (pad.drill > 0.0) output.drills.append({camPoint(pad.center), pad.drill});
        }
    };

    // Poured zones first: their clear-polarity knockouts must not erase tracks and pads.
    QSet<QString> poured;
    for (const ZoneFillResult& fill : options.zoneFills) {
        if (!isCopperLayer(fill.layer)) continue;
        poured.insert(fill.zoneId);
        for (const ZoneContour& contour : zoneContours(fill.fill)) {
            QVector<QPointF> points(contour.polygon.begin(), contour.polygon.end());
            if (points.size() > 3 && points.first() == points.last()) points.removeLast();
            if (points.size() < 3) continue;
            layer(camLayerFor(fill.layer))
                .append({CamPrimitive::Kind::Region, {}, camPoints(points), contour.depth % 2 == 1});
        }
    }

    for (const SketchItem& item : board) {
        if (item.points.isEmpty()) continue;
        switch (item.kind) {
        case SketchItem::Kind::Symbol: {
            const auto* symbol = findSymbol(item.variant);
            if (symbol == nullptr || symbol->workspace != Workspace::Board) break;
            const CamLayerKind silk = item.onBottom ? CamLayerKind::BottomSilk : CamLayerKind::TopSilk;
            for (const SymbolShape& shape : symbol->shapes) {
                if (shape.copper || shape.hole || shape.points.size() < 2) continue;
                QVector<QPointF> world;
                for (const QPointF& point : shape.points) world.append(symbolToWorld(item, point));
                if (shape.filled && shape.closed) {
                    region(silk, world);
                } else {
                    stroke(silk, world, shape.closed, CamSilkLineWidth);
                }
            }
            addPads(item, false);
            if (options.designators && !item.label.trimmed().isEmpty()) {
                // Same place as the canvas label (designatorPlacement); bottom-side text is mirrored
                // so it reads correctly when the board is turned over.
                const QString label = item.label.trimmed();
                const DesignatorPlacement placement =
                    designatorPlacement(item, CamDesignatorHeight, CamDesignatorGap);
                for (const QVector<QPointF>& line :
                     placedStrokeText(label, placement, CamDesignatorHeight, item.onBottom)) {
                    stroke(silk, line, false, CamSilkLineWidth);
                }
            }
            break;
        }
        case SketchItem::Kind::Pad:
            addPads(item, false);
            break;
        case SketchItem::Kind::Via:
            addPads(item, true);
            break;
        case SketchItem::Kind::Wire:
            if (isCopperLayer(item.layer)) {
                stroke(camLayerFor(item.layer), item.points, false, trackWidth(item));
            }
            break;
        case SketchItem::Kind::Text: {
            const CamLayerKind kind = camLayerFor(item.layer);
            if (item.label.trimmed().isEmpty() || kind == CamLayerKind::Outline) {
                if (!item.label.trimmed().isEmpty()) ++output.skippedTexts;
                break;
            }
            // Same box as the canvas: top-left anchor, TextHeightMm tall, vertically centred.
            const double height = TextHeightMm * 0.7;
            const QPointF topLeft = item.points.first() + QPointF(0.0, (TextHeightMm - height) / 2.0);
            const double lineWidth = isCopperLayer(item.layer) ? CamCopperLineWidth : CamSilkLineWidth;
            const bool mirror = isBottomLayer(item.layer);
            const double axis = topLeft.x() + strokeTextWidth(item.label, height) / 2.0;
            for (QVector<QPointF> line : strokeText(item.label, topLeft, height)) {
                if (mirror) {
                    for (QPointF& point : line) point.setX(2.0 * axis - point.x());
                }
                stroke(kind, line, false, lineWidth);
            }
            break;
        }
        case SketchItem::Kind::Line:
        case SketchItem::Kind::Polyline:
        case SketchItem::Kind::Rectangle:
        case SketchItem::Kind::Circle:
        case SketchItem::Kind::Arc: {
            bool closed = false;
            const QVector<QPointF> outline = graphicOutline(item, closed);
            if (item.variant == BoardOutlineVariant) {
                stroke(CamLayerKind::Outline, outline, true, CamOutlineLineWidth);
            } else if (item.variant == CopperZoneVariant) {
                if (poured.contains(item.id)) {
                    // Written above with its clearances.
                } else if (options.includeZones && isCopperLayer(item.layer)) {
                    region(camLayerFor(item.layer), outline);
                } else {
                    ++output.skippedZones;
                }
            } else {
                const CamLayerKind kind = camLayerFor(item.layer);
                stroke(kind, outline, closed, strokeWidthFor(kind));
            }
            break;
        }
        }
    }
    return output;
}

QByteArray gerberLayer(const CamLayer& layer, const QString& generator) {
    const LayerInfo info = layerInfo(layer.kind);
    std::map<QByteArray, int> apertures;
    QByteArray definitions;
    auto dcode = [&](const CamAperture& aperture) {
        const QByteArray key = apertureTemplate(aperture);
        const auto found = apertures.find(key);
        if (found != apertures.end()) return found->second;
        const int code = 10 + static_cast<int>(apertures.size());
        apertures.emplace(key, code);
        definitions += "%ADD" + QByteArray::number(code) + key + "*%\n";
        return code;
    };
    QByteArray body;
    int current = -1;
    bool clear = false;
    auto polarity = [&](bool wanted) {
        if (wanted == clear) return;
        body += wanted ? "%LPC*%\n" : "%LPD*%\n";
        clear = wanted;
    };
    auto select = [&](const CamAperture& aperture) {
        const int code = dcode(aperture);
        if (code != current) {
            body += "D" + QByteArray::number(code) + "*\n";
            current = code;
        }
    };
    for (const CamPrimitive& primitive : layer.primitives) {
        polarity(primitive.kind == CamPrimitive::Kind::Region && primitive.clear);
        switch (primitive.kind) {
        case CamPrimitive::Kind::Flash:
            select(primitive.aperture);
            body += coordinate(primitive.points.value(0)) + "D03*\n";
            break;
        case CamPrimitive::Kind::Stroke:
            select(primitive.aperture);
            body += coordinate(primitive.points.first()) + "D02*\n";
            for (qsizetype i = 1; i < primitive.points.size(); ++i) body += coordinate(primitive.points[i]) + "D01*\n";
            break;
        case CamPrimitive::Kind::Region:
            // Some viewers require an aperture even though it is ignored in region mode.
            select(circle(CamOutlineLineWidth));
            body += "G36*\n" + coordinate(primitive.points.first()) + "D02*\n";
            for (qsizetype i = 1; i < primitive.points.size(); ++i) body += coordinate(primitive.points[i]) + "D01*\n";
            body += coordinate(primitive.points.first()) + "D01*\nG37*\n";
            break;
        }
    }

    QByteArray result;
    result += "G04 " + generator.toUtf8() + " CAM output: " + camLayerName(layer.kind).toUtf8() + "*\n";
    result += "%TF.GenerationSoftware,HattEDA," + generator.toUtf8() + "*%\n";
    result += QByteArray("%TF.FileFunction,") + info.function + "*%\n";
    result += "%TF.FilePolarity,Positive*%\n";
    result += "%FSLAX46Y46*%\n%MOMM*%\n%LPD*%\n";
    result += definitions;
    result += "G01*\n";
    result += body;
    result += "M02*\n";
    return result;
}

QByteArray excellonDrill(const QVector<CamDrillHit>& drills) {
    // Tools are numbered by increasing diameter (rounded to 1 µm).
    std::map<qint64, QVector<QPointF>> tools;
    for (const CamDrillHit& hit : drills) tools[qRound64(hit.diameter * 1000.0)].append(hit.at);
    QByteArray result = "M48\n; #@! TF.FileFunction,Plated,1,2,PTH\nFMAT,2\nMETRIC,TZ\n";
    int tool = 1;
    for (const auto& entry : tools) {
        result += "T" + QByteArray::number(tool++) + "C" +
                  QByteArray::number(entry.first / 1000.0, 'f', 3) + "\n";
    }
    result += "%\nG90\nG05\n";
    tool = 1;
    for (const auto& [microns, hits] : tools) {
        result += "T" + QByteArray::number(tool++) + "\n";
        for (const QPointF& at : hits) {
            result += "X" + QByteArray::number(at.x(), 'f', 3) + "Y" + QByteArray::number(at.y(), 'f', 3) + "\n";
        }
    }
    result += "M30\n";
    return result;
}

QVector<CamFile> camFiles(const CamOutput& output, const QString& baseName, const QString& generator) {
    QVector<CamFile> files;
    for (const CamLayer& layer : output.layers) {
        files.append({baseName + QLatin1Char('-') + QLatin1String(layerInfo(layer.kind).suffix),
                      gerberLayer(layer, generator)});
    }
    files.append({baseName + QStringLiteral("-PTH.drl"), excellonDrill(output.drills)});
    return files;
}

QString writeCamFiles(const QVector<CamFile>& files, const QString& directory) {
    QDir target(directory);
    if (!target.exists()) {
        return QCoreApplication::translate("hatt::ui::GerberExport",
                                           "The output folder does not exist: %1")
            .arg(QDir::toNativeSeparators(directory));
    }
    for (const CamFile& file : files) {
        if (file.fileName.isEmpty() || QFileInfo(file.fileName).fileName() != file.fileName) {
            return QCoreApplication::translate("hatt::ui::GerberExport",
                                               "Invalid fabrication file name: %1")
                .arg(file.fileName);
        }
        const QString path = target.filePath(file.fileName);
        QSaveFile output(path);
        if (!output.open(QIODevice::WriteOnly) || output.write(file.content) != file.content.size() ||
            !output.commit()) {
            return QCoreApplication::translate("hatt::ui::GerberExport", "Could not write %1: %2")
                .arg(QDir::toNativeSeparators(path), output.errorString());
        }
    }
    return {};
}

} // namespace hatt::ui
