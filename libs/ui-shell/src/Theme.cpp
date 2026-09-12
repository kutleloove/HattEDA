#include "hatt/ui/Theme.hpp"

#include <QApplication>
#include <QFont>
#include <QPalette>

namespace hatt::ui {

void Theme::apply(QApplication& application, ThemeMode mode) {
    QFont font(QStringLiteral("Segoe UI"));
    font.setPointSize(10);
    application.setFont(font);
    application.setStyle(QStringLiteral("Fusion"));

    QPalette palette;
    palette.setColor(QPalette::Window, QColor(QStringLiteral("#11161D")));
    palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#E8EDF2")));
    palette.setColor(QPalette::Base, QColor(QStringLiteral("#0C1117")));
    palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#151B23")));
    palette.setColor(QPalette::Text, QColor(QStringLiteral("#DCE3EA")));
    palette.setColor(QPalette::Button, QColor(QStringLiteral("#19212B")));
    palette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#DCE3EA")));
    palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#18B6A4")));
    palette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#07110F")));
    palette.setColor(QPalette::PlaceholderText, QColor(QStringLiteral("#768391")));
    if (mode == ThemeMode::Light) {
        palette.setColor(QPalette::Window, QColor(QStringLiteral("#F3F5F7")));
        palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#1F2A33")));
        palette.setColor(QPalette::Base, QColor(QStringLiteral("#FFFFFF")));
        palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#E9EDF1")));
        palette.setColor(QPalette::Text, QColor(QStringLiteral("#26323D")));
        palette.setColor(QPalette::Button, QColor(QStringLiteral("#E6EBEF")));
        palette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#26323D")));
        palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#087F73")));
        palette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#FFFFFF")));
    }
    application.setPalette(palette);

    QString styleSheet = QStringLiteral(R"(
        * { outline: none; }
        QMainWindow { background: #0c1117; }
        QMenuBar { background: #11161d; color: #aeb9c5; border-bottom: 1px solid #222c37; padding: 3px 8px; spacing: 6px; }
        QMenuBar::item { padding: 5px 9px; border-radius: 4px; }
        QMenuBar::item:selected { background: #202a35; color: #ffffff; }
        QMenu { background: #18202a; color: #dce3ea; border: 1px solid #303b48; padding: 6px; }
        QMenu::item { padding: 7px 28px 7px 10px; border-radius: 4px; }
        QMenu::item:selected { background: #25313d; color: #ffffff; }
        #TopBar { background: #111820; border-bottom: 1px solid #26313d; }
        #BrandMark { color: #16c8b2; font-size: 16px; font-weight: 700; letter-spacing: 1px; }
        #ProjectTitle { color: #dfe6ed; font-size: 13px; font-weight: 600; }
        #ProjectMeta { color: #718090; font-size: 10px; }
        #WelcomePage { background: #0d1319; }
        #WelcomeTitle { color: #f3f6f9; font-size: 30px; font-weight: 650; }
        #WelcomeSubtitle { color: #8795a3; font-size: 13px; }
        #SectionLabel { color: #81909e; font-size: 10px; font-weight: 650; letter-spacing: 1px; }
        #ActiveModeLabel { color: #9ba8b4; font-size: 10px; font-weight: 650; letter-spacing: 1px; }
        #PrimaryAction { background: #18b6a4; color: #06110f; border: 0; border-radius: 7px; padding: 9px 16px; font-size: 13px; font-weight: 650; }
        #PrimaryAction:hover { background: #33c7b5; }
        #PrimaryAction:pressed { background: #119687; }
        #RecentProjects { background: #111820; border: 1px solid #26313d; border-radius: 8px; padding: 8px; }
        #RecentProjects::item { min-height: 42px; padding: 6px 10px; }
        #DocumentBar { background: #151b22; border-bottom: 1px solid #303943; }
        QPushButton[documentTab="true"] { background: transparent; color: #8e9aa5; border: 0; border-bottom: 2px solid transparent; padding: 8px 14px; text-align: left; }
        QPushButton[documentTab="true"]:hover { background: #1d252e; color: #dce3ea; }
        QPushButton[documentTab="true"]:checked { background: #212b34; color: #ffffff; border-bottom-color: #18b6a4; }
        #CompactProjectTitle { color: #aeb8c2; font-size: 11px; padding-right: 8px; }
        #CommandBar { background: #11171d; border-bottom: 1px solid #2b343e; }
        #AlignmentBar { background: #11171d; border-top: 1px solid #2b343e; }
        QPushButton[snap="true"] { background: transparent; color: #83919e; border: 1px solid #303a44; border-radius: 3px; padding: 4px 8px; }
        QPushButton[snap="true"]:checked { background: #153632; color: #55decb; border-color: #258f84; }
        QPushButton[snap="true"]:hover { background: #222c35; color: #ffffff; }
        QToolButton[command="true"] { background: transparent; color: #aeb9c4; border: 1px solid transparent; border-radius: 4px; min-height: 28px; padding: 2px 9px; }
        QToolButton[command="true"]:hover { background: #242d36; color: #ffffff; border-color: #34414d; }
        QToolButton[command="true"]:disabled { background: transparent; color: #4f5b67; border-color: transparent; }
        QToolButton[rail="true"] { min-width: 34px; min-height: 34px; padding: 0; font-weight: 700; border-left: 3px solid transparent; }
        QToolButton[rail="true"]:checked { background: #163431; color: #55decb; border-color: #238f83; border-left-color: #18b6a4; }
        #CommandDivider { color: #303943; }
        #ToolRail { background: #10151b; border-right: 1px solid #2a333d; }
        #ContextPanel { background: #151b22; border-right: 1px solid #303943; }
        #ToolPreview { background: #0d1217; border: 1px solid #303943; border-radius: 3px; }
        #ContextHint { color: #687682; }
        #DesignCanvas { border: 0; }
        QLineEdit { background: #0d1319; color: #e1e7ed; border: 1px solid #34414e; border-radius: 5px; padding: 8px; selection-background-color: #18b6a4; }
        QLineEdit:focus { border-color: #18b6a4; }
        #ModeRail { background: #10161d; border-right: 1px solid #26313d; }
        QPushButton[nav="true"] { background: transparent; color: #8997a6; border: 0; border-left: 3px solid transparent; padding: 12px 8px; text-align: left; font-weight: 600; }
        QPushButton[nav="true"]:hover { background: #18212a; color: #dfe7ef; }
        QPushButton[nav="true"]:checked { background: #17252a; color: #47d6c3; border-left-color: #18b6a4; }
        QPushButton[quiet="true"] { background: #18212a; color: #aeb9c5; border: 1px solid #2b3743; border-radius: 6px; padding: 7px 12px; }
        QPushButton[quiet="true"]:hover { background: #222d38; color: #ffffff; border-color: #3b4a58; }
        #WorkspaceHeader { background: #121920; border-bottom: 1px solid #232e39; }
        #WorkspaceTitle { color: #f0f4f8; font-size: 18px; font-weight: 650; }
        #WorkspaceSubtitle { color: #7f8d9a; }
        #CanvasSurface { background: #0b1016; border: 1px solid #26313d; border-radius: 8px; }
        #EmptyTitle { color: #dfe6ed; font-size: 16px; font-weight: 600; }
        #EmptyBody { color: #7f8d9a; }
        QDockWidget { color: #aeb9c5; font-weight: 600; titlebar-close-icon: none; titlebar-normal-icon: none; }
        QDockWidget::title { background: #151c24; border-top: 1px solid #26313d; border-bottom: 1px solid #26313d; padding: 8px 10px; text-align: left; }
        QDockWidget > QWidget { background: #111820; }
        QListWidget { background: #111820; border: 0; color: #93a0ad; padding: 6px; }
        QListWidget::item { padding: 7px; border-radius: 4px; border-left: 3px solid transparent; }
        QListWidget::item:selected { background: #1a302f; color: #55decb; border-left-color: #18b6a4; }
        QTabWidget::pane { border: 1px solid #26313d; background: #0d1319; }
        QTabBar::tab { background: #151c24; color: #8593a1; border-right: 1px solid #26313d; padding: 8px 14px; min-width: 140px; }
        QTabBar::tab:selected { background: #1b252e; color: #e7edf3; border-top: 2px solid #18b6a4; }
        QTabBar::tab:hover:!selected { background: #1a222b; color: #c5ced7; }
        QTextEdit { background: #0d1319; color: #b8c3ce; border: 0; padding: 12px; }
        QStatusBar { background: #0e141a; color: #718090; border-top: 1px solid #26313d; }
        QStatusBar::item { border: 0; }
        QToolTip { background: #202a35; color: #ffffff; border: 1px solid #3a4856; padding: 5px; }
    )");
    if (mode == ThemeMode::Light) {
        styleSheet += QStringLiteral(R"(
            QMainWindow, #WelcomePage { background: #f3f5f7; }
            QMenuBar, #DocumentBar, #CommandBar, #AlignmentBar { background: #eef1f4; color: #34404b; border-color: #cbd2d9; }
            QPushButton[snap="true"] { color: #53616c; border-color: #bcc6cf; }
            QPushButton[snap="true"]:checked { background: #d7eeea; color: #075f57; border-color: #55a79f; }
            QMenuBar::item:selected, QToolButton[command="true"]:hover { background: #dfe5ea; color: #101820; }
            QMenu { background: #ffffff; color: #26323d; border-color: #bfc8d0; }
            #ToolRail, #ContextPanel { background: #e9edf1; border-color: #c8d0d7; }
            #ToolPreview { background: #f8fafb; border-color: #c8d0d7; }
            #ContextHint, #ProjectMeta, #SectionLabel { color: #5e6b76; }
            #CompactProjectTitle, QToolButton[command="true"] { color: #34404b; }
            QToolButton[command="true"]:disabled { background: transparent; color: #9aa6b0; border-color: transparent; }
            QToolButton[rail="true"]:checked { background: #d7eeea; color: #075f57; border-color: #55a79f; border-left-color: #087f73; }
            QPushButton[documentTab="true"] { color: #53616c; }
            QPushButton[documentTab="true"]:checked { background: #ffffff; color: #111820; border-bottom-color: #087f73; }
            QListWidget { background: #f7f9fa; color: #34404b; }
            QListWidget::item:selected { background: #d7eeea; color: #075f57; border-left-color: #087f73; }
            QStatusBar { background: #e9edf1; color: #53616c; border-color: #c8d0d7; }
        )");
    }
    application.setStyleSheet(styleSheet);
}

} // namespace hatt::ui
