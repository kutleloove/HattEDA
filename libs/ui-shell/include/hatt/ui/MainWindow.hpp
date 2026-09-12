#pragma once

#include <QHash>
#include <QMainWindow>
#include <QString>

class QAction;
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

public slots:
    void showMergenWorkspace();
    void showKayraWorkspace();
    void openDiagnosticsWorkspace();
    void createNewProject();
    void openProject();

protected:
    void changeEvent(QEvent* event) override;

private:
    enum class ToolMode { Select, Component, Connect, Terminal, Probe, Draw, Measure };

    void createActions();
    void createMenus();
    QWidget* createEditor();
    QWidget* createWelcomePage();
    [[nodiscard]] DesignCanvas* editingCanvas() const;
    void openToolWorkspace(const QString& stableId, const QString& title, QWidget* content);
    void activateProject(const QString& projectName, const QString& projectPath);
    void refreshRecentProjects();

    void activateToolMode(ToolMode mode);
    void rebuildObjectSelector();
    void applyObjectSelection();
    void workspaceChanged();
    void applySnapSettings();
    void updateEditActions();
    void refreshIcons();
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
    QLabel* coordinateLabel_ = nullptr;
    QLabel* zoomLabel_ = nullptr;
};

} // namespace hatt::ui
