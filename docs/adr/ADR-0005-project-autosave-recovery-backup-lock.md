# ADR-0005: Project autosave, crash recovery, backup and lock files

Status: Accepted for the interim `.hatt` project file (ADR-0004); refs #26.

ADR-0004 made projects persistent, but a crash still lost all unsaved work, a bad save had no previous copy to fall back to, and two windows could edit the same file and overwrite each other. These safety mechanisms do not change the `.hatt` format.

## Decision

- Sidecar files sit next to the project and share its base name:

  | File | Content | Lifetime |
  | --- | --- | --- |
  | `<name>.hatt.autosave` | Recovery copy. It uses the same `.hatt` format, written with `serializeProject`/`saveProjectFile` (atomic `QSaveFile`) from the `ProjectData` that Save writes. | Written by autosave. Removed after a successful Save or Save as, when the user discards changes, when the window closes or switches projects, and when an undo returns to the saved state. |
  | `<name>.hatt.bak` | The previous project file, one generation. | Refreshed on every save that changes the file content. |
  | `<name>.hatt.lock` | `QLockFile` (PID, application, host). | Held while a window has the project open. |

  We chose the project folder over `AppLocalDataLocation`: recovery and backup files then move with the project and are easy to find. If the folder is not writable, autosave fails silently with a status-bar note and the project is opened without a lock.
- **Autosave:**
  - A `QTimer` runs while a project is open, every `projects/autosaveMinutes` minutes (default 2; 0 disables it).
  - A tick writes only while an undo stack is dirty and the serialized content differs from the last autosave.
  - It never writes the project file.
- **Crash recovery:**
  - `openProjectFile` (Open and Recent) loads the saved project first.
  - A recovery file is offered when all of these hold:
    - it is newer than the project;
    - its bytes differ from the project;
    - it parses;
    - no live process holds the project lock. A live holder's recovery file belongs to that session.
  - The dialog offers Restore, Open saved version or Cancel.
    - Restore loads the recovered documents and keeps the original path. It marks both undo stacks never-clean (`QUndoStack::resetClean`), so the window stays modified until Save writes the project.
    - Open saved version deletes the recovery file.
    - Cancel opens nothing.
  - A damaged or newer-format recovery file is ignored with a status note. It never blocks opening the saved project.
  - The app does not reopen projects at startup, so recovery is offered when the project is opened.
- **Backup on save:**
  - Before `QSaveFile` replaces an existing project whose content differs, the file is copied to `<name>.hatt.bak.partial` and then swapped into `.bak`.
  - The project file is only read. A failed copy leaves the project and the previous backup in place, adds a status note, and the save still happens atomically.
  - New project replacing an existing file also keeps a backup.
- **Single-instance lock:**
  - Before opening, the window probes `<name>.hatt.lock`. If another live process holds it, the user can choose Open anyway or Cancel.
  - A window that opened a project anyway holds no lock. It does not autosave and does not delete that project's recovery file.
  - Stale locks are handled by `QLockFile`: a dead PID on the same host, or an age beyond the default stale time, lets the lock be taken over. An open handle (Windows) or `flock` (Unix) keeps a live holder's lock from being removed.
  - The lock is released on close, when switching projects and after Save as, which locks the new path.
- **Host wiring:** `ProjectGuard` (`ProjectSafety.hpp/.cpp`, `hatt-ui-shell`) owns the timer, the lock and the dialogs. `MainWindow` supplies `currentProjectData(path)` and `hasUnsavedChanges()` and calls the guard from `openProjectFile`, `activateProject`, `writeProject`, `saveProjectAs`, `maybeSaveChanges` (Discard), `closeEvent` and `createNewProject`.
- **Recent projects:** a Recent entry whose file no longer exists shows a message and offers to remove the entry.
- **Unsaved-changes prompt:** File › Quit (`hatteda.action.quit`, Ctrl+Q) closes the window, so it gets the same Save / Discard / Cancel prompt as closing the window. A failed Save in the prompt cancels the close.

## Consequences

- Up to `autosaveMinutes` of work can still be lost in a crash.
- The sidecar files are visible next to projects. Version control should ignore `*.autosave`, `*.bak` and `*.lock`.
- Two windows that choose Open anyway can still overwrite each other. The lock is advisory.

## Validation

`hatt-project-safety-tests` (`tests/unit/ProjectSafetyTests.cpp`) covers:
- autosave writing only dirty, changed content, and Save removing the file;
- the settings interval (0 disables autosave);
- Discard on close removing recovery and lock;
- Restore, Cancel and Open saved version, and damaged and identical recovery files;
- the `.bak` on the second save, unchanged saves and a failed backup;
- the lock detecting a second window, stale locks, and missing Recent entries;
- a failed Save in the prompt cancelling Quit and close.
