add_library(hatt-ui-shell STATIC
    include/hatt/ui/DesignCanvas.hpp
    include/hatt/ui/MainWindow.hpp
    include/hatt/ui/SketchModel.hpp
    include/hatt/ui/Theme.hpp
    src/DesignCanvas.cpp
    src/MainWindow.cpp
    src/SketchModel.cpp
    src/Theme.cpp
)

add_library(HattEDA::UiShell ALIAS hatt-ui-shell)

target_include_directories(hatt-ui-shell PUBLIC include)
target_link_libraries(hatt-ui-shell PUBLIC Qt6::Widgets)

if(MSVC)
    target_compile_options(hatt-ui-shell PRIVATE /W4 /permissive-)
else()
    target_compile_options(hatt-ui-shell PRIVATE -Wall -Wextra -Wpedantic)
endif()
