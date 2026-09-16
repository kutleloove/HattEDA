#pragma once

#include "hatt/ui/SketchModel.hpp"

#include <QByteArray>
#include <QString>

namespace hatt::ui {

// Cut/copy/paste (#8) puts the selection on the system clipboard under this custom MIME type,
// alongside SketchDocument's built-in fields. This is interim UI-shell state (ADR-0002/0003), not
// a persistent format like `.hatt` (ADR-0004): it only needs to round-trip within one running
// HattEDA build, so it uses its own small JSON encoding rather than ProjectFile's versioned schema.
inline const QString SketchClipboardMimeType = QStringLiteral("application/x-hatteda-sketch");

[[nodiscard]] QByteArray encodeSketchClipboard(const SketchDocument& items, Workspace workspace);

struct SketchClipboardPayload {
    SketchDocument items;
    Workspace workspace = Workspace::Schematic;
    bool valid = false;
};
// Empty or malformed data decodes to `valid == false`.
[[nodiscard]] SketchClipboardPayload decodeSketchClipboard(const QByteArray& data);

} // namespace hatt::ui
