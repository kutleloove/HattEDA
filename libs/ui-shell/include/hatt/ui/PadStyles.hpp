#pragma once

#include "hatt/ui/SketchModel.hpp"

#include <QString>
#include <QVector>

#include <optional>

class QWidget;

namespace hatt::ui {

// Pads offered by Kayra's pad mode (a light Proteus ARES padstack list): the built-in padStyles()
// followed by the user's own pad styles. User styles are an application preference (QSettings
// `editor/board/customPadStyles`, maps of {id, name, shape, width, height, drill}); placed pads
// copy the definition, so editing or deleting a style never changes a document.
struct PadStyleEntry {
    QString id; // built-in "pad.round", user "pad.user.<uuid>"
    QString name;
    PadDefinition pad;
    bool builtIn = false;
};

inline const QString UserPadStylePrefix = QStringLiteral("pad.user.");

[[nodiscard]] QVector<PadStyleEntry> padStyleEntries();
[[nodiscard]] QVector<PadStyleEntry> customPadStyles();
void setCustomPadStyles(const QVector<PadStyleEntry>& styles);
// Pad of a built-in or user style id; nullopt for unknown ids (e.g. "via").
[[nodiscard]] std::optional<PadStyleEntry> findPadStyleEntry(const QString& id);
// Validation message for a style being saved, empty when valid.
[[nodiscard]] QString padStyleProblem(const PadStyleEntry& style);

// Modal editor for a new or existing user pad style (objectName "PadStyleDialog": "PadStyleName",
// "PadStyleShape", "PadStyleWidth", "PadStyleHeight", "PadStyleDrill"). A new style gets an id.
bool editPadStyleDialog(QWidget* parent, PadStyleEntry& style);

} // namespace hatt::ui
