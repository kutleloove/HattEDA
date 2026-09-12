# Desktop shell architecture

The desktop executable is a composition root. Reusable shell behavior lives in the `hatt-ui-shell` CMake target. Domain libraries must not depend on this target.

`MainWindow` owns host-managed Qt chrome. Primary workspaces are persistent pages in a stacked widget. Tool workspaces are closable tabs. Dock creation and stable object names remain host responsibilities so layout persistence and future plugin disable/safe-mode flows can be controlled centrally.

The current code is a bootstrap vertical slice, not the public plugin API. External contributions must wait for `ContributionRegistry`; feature code must not call `QMainWindow::addDockWidget`, `addToolBar`, or mutate menus directly.
