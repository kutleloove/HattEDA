#pragma once

#include "hatt/ui/ProjectFile.hpp"
#include "hatt/ui/Units.hpp"

#include <QHash>
#include <QMainWindow>
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

class DesignCanvas;

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

protected:
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    enum class ToolMode { Select, Component, Connect, Terminal, Probe, Draw, Measure };

    void createActions();
    void createMenus();
    QWidget* createEditor();
    QWidget* createWelcomePage();
    [[nodiscard]] DesignCanvas* editingCanvas() const;
    void openToolWorkspace(const QString& stableId, const QString& title, QWidget* content);
    void activateProject(const QString& projectPath, const ProjectData& project);
    bool writeProject(const QString& path);
    // Asks to save unsaved changes; false when the user cancels or saving fails.
    bool maybeSaveChanges();
    [[nodiscard]] bool hasUnsavedChanges() const;
    void updateProjectState();
    void addRecentProject(const QString& path);
    void refreshRecentProjects();

    void activateToolMode(ToolMode mode);
    void rebuildObjectSelector();
    void applyObjectSelection();
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
    QListWidget* recentProjects_ = nullptr;
    QList<QPushButton*> snapToggles_;
    QActionGroup* gridActions_ = nullptr;
    QPushButton* gridStepButton_ = nullptr;
    int gridLevel_ = 2;
    QActionGroup* unitActions_ = nullptr;
    LengthUnit boardUnit_ = LengthUnit::Millimetre;
    QString projectPath_;
    QString projectName_;
    QLabel* coordinateLabel_ = nullptr;
    QLabel* zoomLabel_ = nullptr;
};

} // namespace hatt::ui
