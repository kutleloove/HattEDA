#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

class QWidget;

namespace hatt::ui {

// Track and via styles offered by Kayra's connect and via modes (Proteus ARES style lists): the
// built-in T8..T100 and V24..V70 styles followed by the user's own styles. User styles are an
// application preference (QSettings `editor/board/customTrackStyles` and `customViaStyles`, lists
// of {name, width, drill} maps); placed tracks and vias store their own sizes, so editing or
// deleting a style never changes a document.
struct RoutingStyle {
    QString name;
    double width = 0.0; // track width or via outer diameter, mm
    double drill = 0.0; // via drill, mm; 0 for tracks
    bool builtIn = false;
};

enum class RoutingStyleKind { Track, Via };

[[nodiscard]] QVector<RoutingStyle> routingStyles(RoutingStyleKind kind);
[[nodiscard]] QVector<RoutingStyle> customRoutingStyles(RoutingStyleKind kind);
void setCustomRoutingStyles(RoutingStyleKind kind, const QVector<RoutingStyle>& styles);
// Style by name, or nullptr-like empty style (name empty) when unknown.
[[nodiscard]] RoutingStyle findRoutingStyle(RoutingStyleKind kind, const QString& name);
// Validation message for a style being saved, empty when valid. `previousName` is the name of the
// style being edited (empty when creating), so keeping its own name is allowed.
[[nodiscard]] QString routingStyleProblem(RoutingStyleKind kind, const RoutingStyle& style,
                                          const QString& previousName);

// Modal editor for a new or existing user style (objectName "RoutingStyleDialog": fields
// "RoutingStyleName", "RoutingStyleWidth", "RoutingStyleDrill", unit combo "RoutingStyleUnit").
// Returns false when cancelled.
bool editRoutingStyleDialog(QWidget* parent, RoutingStyleKind kind, RoutingStyle& style);

} // namespace hatt::ui
