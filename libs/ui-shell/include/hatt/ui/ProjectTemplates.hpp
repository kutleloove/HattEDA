#pragma once

#include "hatt/ui/ProjectFile.hpp"

namespace hatt::ui {

struct ProjectTemplate {
    QString id;
    QString title;
    QString description;
};

// Resource projects are immutable. Every load returns a new project with fresh item identities.
[[nodiscard]] QVector<ProjectTemplate> projectTemplates();
[[nodiscard]] ProjectLoad loadProjectTemplate(const QString& id);

} // namespace hatt::ui
