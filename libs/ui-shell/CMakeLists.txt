add_library(hatt-ui-shell STATIC
    include/hatt/ui/DesignCanvas.hpp
    include/hatt/ui/MainWindow.hpp
    include/hatt/ui/ProjectFile.hpp
    include/hatt/ui/SketchModel.hpp
    include/hatt/ui/Theme.hpp
    include/hatt/ui/Units.hpp
    src/DesignCanvas.cpp
    src/MainWindow.cpp
    src/ProjectFile.cpp
    include/hatt/ui/ProjectSafety.hpp
    src/ProjectSafety.cpp
    src/SketchModel.cpp
    src/SketchCircuit.cpp
    include/hatt/ui/CircuitWorkflow.hpp
    src/CircuitWorkflow.cpp
    include/hatt/ui/ComponentLibrary.hpp
    src/ComponentLibrary.cpp
    include/hatt/ui/LibraryDialogs.hpp
    src/LibraryDialogs.cpp
    include/hatt/ui/DesignChecks.hpp
    src/DesignChecks.cpp
    include/hatt/ui/ChecksReport.hpp
    src/ChecksReport.cpp
    src/Theme.cpp
    src/Units.cpp
    include/hatt/ui/BoardLayerPanel.hpp
    src/BoardLayerPanel.cpp
    include/hatt/ui/RoutingStyles.hpp
    src/RoutingStyles.cpp
    include/hatt/ui/PackageFromSelection.hpp
    src/PackageFromSelection.cpp
    include/hatt/ui/PadStyles.hpp
    src/PadStyles.cpp
    include/hatt/ui/LayerColors.hpp
    src/LayerColors.cpp
    include/hatt/ui/GerberExport.hpp
    src/GerberExport.cpp
    include/hatt/ui/StrokeFont.hpp
    src/StrokeFont.cpp
)

add_library(HattEDA::UiShell ALIAS hatt-ui-shell)

target_include_directories(hatt-ui-shell PUBLIC include)
target_link_libraries(hatt-ui-shell PUBLIC Qt6::Widgets HattEDA::Electrical)

if(MSVC)
    target_compile_options(hatt-ui-shell PRIVATE /W4 /permissive-)
else()
    target_compile_options(hatt-ui-shell PRIVATE -Wall -Wextra -Wpedantic)
endif()
