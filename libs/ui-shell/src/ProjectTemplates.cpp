#include "hatt/ui/ProjectTemplates.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QUuid>

static void initializeProjectTemplates() {
    static const bool initialized = [] { Q_INIT_RESOURCE(project_templates); return true; }();
    (void)initialized;
}

namespace hatt::ui {
namespace {
QString tr(const char* text) { return QCoreApplication::translate("hatt::ui::ProjectTemplates", text); }
}

QVector<ProjectTemplate> projectTemplates() {
    return {{"divider", tr("Voltage divider"), tr("Two equal resistors divide 5 V into 2.5 V. Run DC to read the VOUT probe.")},
            {"led", tr("Light an LED"), tr("A 3.3 V source and 100 ohm resistor demonstrate current limiting with a generic LED model.")}};
}

ProjectLoad loadProjectTemplate(const QString& id) {
    initializeProjectTemplates();
    if (id.isEmpty()) return {};
    bool known = false;
    for (const auto& entry : projectTemplates()) if (entry.id == id) known = true;
    if (!known) { ProjectLoad result; result.error = tr("Unknown project template."); return result; }
    QFile file(QStringLiteral(":/templates/%1.hatt").arg(id));
    if (!file.open(QIODevice::ReadOnly)) {
        ProjectLoad result; result.error = tr("Cannot read the project template."); return result;
    }
    auto result = parseProject(file.readAll());
    if (!result.ok()) return result;
    for (auto& item : result.project.schematic) {
        item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (item.kind == SketchItem::Kind::Text)
            item.label = id == QLatin1String("led")
                ? tr("R1 limits LED current.\nGeneric LED model; VOUT is the forward voltage.")
                : tr("Equal resistors halve the supply.\nVOUT = 5 V / 2 = 2.5 V.");
    }
    return result;
}

} // namespace hatt::ui
