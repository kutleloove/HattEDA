#pragma once

#include "hatt/ui/DesignChecks.hpp"
#include "hatt/ui/SketchModel.hpp"

#include <QByteArray>
#include <QString>

namespace hatt::ui {

// Interim `.hatt` project file (ADR-0004, ADR-0006): versioned UTF-8 JSON holding the schematic
// and board sketch documents in floating-point millimetres. Format version 2 adds BoardLayer,
// Pad/Via item kinds, onBottom/excludeFromBoard flags, and a project library. Version 3 adds the
// optional simulation model id of custom devices. Version 4 (ADR-0012) adds keepout and area zones
// and the zone fill style; it is written only by projects that use them (requiredFormatVersion), so
// other projects still open in older builds. Readers reject newer files; older files are silently
// upgraded with additive defaults.

inline constexpr int ProjectFormatVersion = 4;     // newest version this build reads and writes
inline constexpr int ProjectBaseFormatVersion = 3; // written when no version 4 feature is used
inline const QString ProjectFormatName = QStringLiteral("hatteda-project");

struct ProjectData;
// The version written for `project`: ProjectFormatVersion when the board has keepout or area zones
// or a zone that is not Solid, which version 3 readers would misread as copper; otherwise
// ProjectBaseFormatVersion.
[[nodiscard]] int requiredFormatVersion(const ProjectData& project);

struct ProjectData {
    QString name;
    SketchDocument schematic;
    SketchDocument board;
    ProjectLibrary library; // v2: picked devices (component mode)
    DesignRules rules;      // DRC rules (ADR-0008)
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
