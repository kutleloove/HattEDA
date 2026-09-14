#include "hatt/ui/StrokeFont.hpp"

#include <QHash>
#include <QStringList>

namespace hatt::ui {
namespace {

constexpr double GlyphUnits = 6.0;  // glyph height in grid units
constexpr double AdvanceUnits = 6.0; // 4 units of glyph plus a 2 unit gap

// Each glyph is a list of polylines separated by '|'; every point is two digits "xy" on the grid.
const QHash<QChar, const char*>& glyphs() {
    static const QHash<QChar, const char*> table = {
        {u'0', "0040460600|0640"},  {u'1', "112026|0646"},      {u'2', "004043030646"},
        {u'3', "00404606|0343"},    {u'4', "000343|4046"},      {u'5', "400003434606"},
        {u'6', "400006464303"},     {u'7', "004016"},           {u'8', "0040460600|0343"},
        {u'9', "4640000343"},       {u'A', "0602204246|0444"},  {u'B', "003041423303|334445360600"},
        {u'C', "40000646"},         {u'D', "00304145360600"},   {u'E', "40000646|0333"},
        {u'F', "400006|0333"},      {u'G', "400006464323"},     {u'H', "0006|4046|0343"},
        {u'I', "0040|2026|0646"},   {u'J', "4045361605"},       {u'K', "0006|400346"},
        {u'L', "000646"},           {u'M', "0600224046"},       {u'N', "06004640"},
        {u'O', "0040460600"},       {u'P', "0600404303"},       {u'Q', "0040460600|2446"},
        {u'R', "0600404303|2346"},  {u'S', "400003434606"},     {u'T', "0040|2026"},
        {u'U', "00064640"},         {u'V', "002640"},           {u'W', "0006244640"},
        {u'X', "0046|4006"},        {u'Y', "0023|4023|2326"},   {u'Z', "00400646"},
        {u'-', "0343"},             {u'_', "0646"},             {u'.', "2526"},
        {u'+', "0343|2224"},        {u'/', "0640"},             {u'?', "0040432324|2526"},
        {u':', "2122|2425"},        {u'(', "30212536"},         {u')', "10212516"},
        {u'=', "0242|0444"},        {u'#', "1016|3036|0242|0444"},
    };
    return table;
}

} // namespace

double strokeTextWidth(const QString& text, double height) {
    if (text.isEmpty()) return 0.0;
    const double unit = height / GlyphUnits;
    return (AdvanceUnits * text.size() - 2.0) * unit;
}

QVector<QVector<QPointF>> strokeText(const QString& text, QPointF topLeft, double height) {
    QVector<QVector<QPointF>> result;
    const double unit = height / GlyphUnits;
    for (qsizetype index = 0; index < text.size(); ++index) {
        const QChar character = text.at(index).toUpper();
        if (character.isSpace()) continue;
        const QPointF origin = topLeft + QPointF(AdvanceUnits * unit * static_cast<double>(index), 0.0);
        const char* glyph = glyphs().value(character, "0040460600");
        for (const QString& stroke : QString::fromLatin1(glyph).split(QLatin1Char('|'))) {
            QVector<QPointF> points;
            for (qsizetype i = 0; i + 1 < stroke.size(); i += 2) {
                const int x = stroke.at(i).digitValue();
                const int y = stroke.at(i + 1).digitValue();
                points.append(origin + QPointF(x * unit, y * unit));
            }
            if (points.size() >= 2) result.append(points);
        }
    }
    return result;
}

DesignatorPlacement designatorPlacement(const SketchItem& footprint, double textHeight, double gap) {
    const QRectF bounds = itemBounds(footprint);
    const bool vertical = ((footprint.quarterTurns % 4) + 4) % 2 == 1;
    if (vertical) return {QPointF(bounds.left() - gap - textHeight / 2.0, bounds.center().y()), true};
    return {QPointF(bounds.center().x(), bounds.top() - gap - textHeight / 2.0), false};
}

QVector<QVector<QPointF>> placedStrokeText(const QString& text, const DesignatorPlacement& placement, double height,
                                           bool mirrored) {
    const double width = strokeTextWidth(text, height);
    QVector<QVector<QPointF>> lines = strokeText(text, QPointF(-width / 2.0, -height / 2.0), height);
    for (QVector<QPointF>& line : lines) {
        for (QPointF& point : line) {
            // Y points down: a 90° counter-clockwise turn maps the reading direction +X to -Y.
            if (placement.vertical) point = QPointF(point.y(), -point.x());
            if (mirrored) point.setX(-point.x());
            point += placement.centre;
        }
    }
    return lines;
}

} // namespace hatt::ui
