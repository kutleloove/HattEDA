#pragma once

#include "hatt/ui/ProjectFile.hpp"
#include "hatt/ui/Units.hpp"

#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QString>

class QAction;
class QCloseEvent;
class QActionGroup;
class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QTabWidget;
class QUndoGroup;

namespace hatt::ui {

class BoardLayerPanel;
class ChecksReport;
class DesignCanvas;
class ProjectGuard;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    [[nodiscard]] int primaryWorkspaceCount() const noexcept;
    [[nodiscard]] int toolWorkspaceCount() const noexcept;
    [[nodiscard]] DesignCanvas* activeCanvas() const;
    // Path of the open `.hatt` file, empty while no project is open.
    [[nodiscard]] QString projectPath() const { return projectPath_; }
    // Autosave, recovery, backup and lock handling of the open project (ProjectSafety.hpp).
    [[nodiscard]] ProjectGuard* projectGuard() const { return projectGuard_; }
    // Devices offered by schematic component mode (Proteus style pick list, projectDeviceList).
    [[nodiscard]] QStringList projectDevices() const;
    // Adds built-in schematic component ids to the pick list; unknown and listed ids are ignored.
    void addProjectDevices(const QStringList& ids);
    // Removes a device from the pick list; false while the schematic still uses it.
    bool removeProjectDevice(const QString& id);
    // The project's design rules used by the DRC (ADR-0008).
    [[nodiscard]] DesignRules designRules() const { return rules_; }

public slots:
    void showMergenWorkspace();
    void showKayraWorkspace();
    void openDiagnosticsWorkspace();
    void createNewProject();
    void openProject();
    // Loads a project file into both workspaces; shows the error and returns false on failure.
    bool openProjectFile(const QString& path);
    bool saveProject();
    bool saveProjectAs();
    // Library browser that adds devices to the project (PickDevicesDialog).
    void pickDevices();
    // DeviceEditorDialog: creates a project device (and footprints made from it) and picks it.
    void newDevice();
    // FootprintEditorDialog without a device: creates a project footprint.
    void newFootprint();
    // Generates Gerber X2 and Excellon files from the Kayra board.
    void exportFabricationFiles();
    // Runs ERC and DRC and shows the results in the `hatteda.tool.design-checks` workspace.
    void runDesignChecks();
    // DesignRulesDialog for the project's rules.
    void editDesignRules();
    // Assembly CSV files (ManufacturingExport.hpp). An empty path asks for one; false when cancelled
    // or writing failed (the error is shown).
    bool exportBom(const QString& path = {});
    bool exportPlacement(const QString& path = {});
    // Re-pours the board's copper zones (ZoneFill.hpp) and hands the result to the board canvas.
    void refreshZoneFills();

protected:
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    // Package, Via and Pad are Kayra only (Proteus ARES modes); Probe is Mergen only.
    enum class ToolMode { Select, Component, Connect, Terminal, Probe, Draw, Measure, Package, Via, Pad };

    void createActions();
    void createMenus();
    QWidget* createEditor();
    QWidget* createWelcomePage();
    [[nodiscard]] DesignCanvas* editingCanvas() const;
    void openToolWorkspace(const QString& stableId, const QString& title, QWidget* content);
    void activateProject(const QString& projectPath, const ProjectData& project);
    bool writeProject(const QString& path);
    // The project as it is saved to `path`; also the autosave snapshot.
    [[nodiscard]] ProjectData currentProjectData(const QString& path) const;
    // Asks to save unsaved changes; false when the user cancels or saving fails.
    bool maybeSaveChanges();
    [[nodiscard]] bool hasUnsavedChanges() const;
    void updateProjectState();
    void addRecentProject(const QString& path);
    void refreshRecentProjects();
    void openRecentProject(const QString& path);

    void activateToolMode(ToolMode mode);
    void rebuildObjectSelector();
    // Rebuilds component mode's list when placing, deleting or undoing changed its contents.
    void refreshComponentList();
    [[nodiscard]] QStringList componentListKeys() const;
    void removeSelectedDevice();
    void applyObjectSelection();
    // Kayra connect / via modes: the user's own track and via styles (RoutingStyles.hpp).
    void editRoutingStyle(bool create);
    void deleteRoutingStyle();
    // Kayra: Proteus ARES Make Package / Decompose on the board selection (PackageFromSelection.hpp).
    void makePackage();
    void decomposeSelection();
    void workspaceChanged();
    void applySnapSettings();
    void setGridLevel(int level);
    void setBoardUnit(LengthUnit unit);
    void applyLengthUnits();
    void updateGridActions();
    void updateEditActions();
    void refreshIcons();
    void showCanvasContextMenu(DesignCanvas* canvas, QPoint position, int index);
    void editItemProperties(DesignCanvas* canvas, int index);
    void showArrayDialog(DesignCanvas* canvas);
    QAction* makeAction(const QString& objectName, const QString& text, const QString& iconKind);
    bool writeAssemblyFile(QString path, const QString& title, const QString& suffix, const QByteArray& content);

    QStackedWidget* shellPages_ = nullptr;
    QStackedWidget* editorSurfaces_ = nullptr;
    QStackedWidget* primaryWorkspaces_ = nullptr;
    QTabWidget* toolWorkspaces_ = nullptr;
    QList<DesignCanvas*> canvases_;
    QUndoGroup* undoGroup_ = nullptr;
    QActionGroup* toolActions_ = nullptr;
    QHash<QString, QAction*> actions_;
    QHash<int, int> rememberedObjectRows_;
    ToolMode toolMode_ = ToolMode::Select;

    QPushButton* mergenTab_ = nullptr;
    QPushButton* kayraTab_ = nullptr;
    QLabel* projectTitle_ = nullptr;
    QLabel* activeModeLabel_ = nullptr;
    QLabel* objectsLabel_ = nullptr;
    QLabel* contextHint_ = nullptr;
    QWidget* objectPreview_ = nullptr;
    QListWidget* objectSelector_ = nullptr;
    QWidget* deviceBar_ = nullptr;
    QWidget* boardPartsBar_ = nullptr;
    // Kayra layer visibility and the active layer selector at the bottom left (BoardLayerPanel).
    BoardLayerPanel* boardLayerPanel_ = nullptr;
    QWidget* routingStyleBar_ = nullptr;
    QPushButton* editStyleButton_ = nullptr;
    QPushButton* deleteStyleButton_ = nullptr;
    QPushButton* removeDeviceButton_ = nullptr;
    QStringList componentKeys_;
    // Board component mode: footprints waiting for placement, indexed by the selector rows.
    SketchDocument boardParts_;
    QStringList boardPartProblems_;
    ProjectLibrary library_;
    bool libraryModified_ = false;
    QListWidget* recentProjects_ = nullptr;
    QList<QPushButton*> snapToggles_;
    QActionGroup* gridActions_ = nullptr;
    QPushButton* gridStepButton_ = nullptr;
    int gridLevel_ = 2;
    QActionGroup* unitActions_ = nullptr;
    LengthUnit boardUnit_ = LengthUnit::Millimetre;
    QString projectPath_;
    QString projectName_;
    ProjectGuard* projectGuard_ = nullptr;
    QLabel* coordinateLabel_ = nullptr;
    QLabel* zoomLabel_ = nullptr;
    DesignRules rules_;
    bool rulesModified_ = false;
    QPointer<ChecksReport> checksReport_;
};

} // namespace hatt::ui
