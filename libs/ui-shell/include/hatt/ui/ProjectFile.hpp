#pragma once

#include "hatt/ui/SketchModel.hpp"

#include <QByteArray>
#include <QString>

namespace hatt::ui {

// Interim `.hatt` project file (ADR-0004): versioned UTF-8 JSON holding the schematic and board
// sketch documents in floating-point millimetres. It will be migrated when the fixed-point domain
// model (HATT-003) lands; readers reject files written by a newer format version.

inline constexpr int ProjectFormatVersion = 1;
inline const QString ProjectFormatName = QStringLiteral("hatteda-project");

struct ProjectData {
    QString name;
    SketchDocument schematic;
    SketchDocument board;
};

struct ProjectLoad {
    ProjectData project;
    QString error; // Translated, user-facing; empty on success.
    [[nodiscard]] bool ok() const noexcept { return error.isEmpty(); }
};

[[nodiscard]] QByteArray serializeProject(const ProjectData& project);
[[nodiscard]] ProjectLoad parseProject(const QByteArray& bytes);

// Writes atomically (QSaveFile): a failed save leaves any existing file untouched.
// Returns a translated error, or an empty string on success.
[[nodiscard]] QString saveProjectFile(const QString& path, const ProjectData& project);
[[nodiscard]] ProjectLoad loadProjectFile(const QString& path);

} // namespace hatt::ui
