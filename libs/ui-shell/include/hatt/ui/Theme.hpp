#pragma once

class QApplication;

namespace hatt::ui {

enum class ThemeMode { Dark, Light };

class Theme final {
public:
    static void apply(QApplication& application, ThemeMode mode = ThemeMode::Dark);
};

} // namespace hatt::ui
