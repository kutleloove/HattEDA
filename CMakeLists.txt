cmake_minimum_required(VERSION 3.25)

project(HattEDA VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

# CI turns this on (see .github/workflows/ci.yml); local builds keep warnings non-fatal.
option(HATTEDA_WERROR "Treat compiler warnings as errors" OFF)
if(HATTEDA_WERROR)
    set(CMAKE_COMPILE_WARNING_AS_ERROR ON)
endif()

include(CTest)
# PrintSupport: Kayra print layout to printers (ADR-0011); part of Qt Base, no extra install.
# Network: agentic MCP local server transport (#37/ADR-0013).
find_package(Qt6 6.11 REQUIRED COMPONENTS Widgets PrintSupport Network LinguistTools)

add_subdirectory(libs/electrical)
add_subdirectory(libs/agentic-mcp)
add_subdirectory(libs/ui-shell)
add_subdirectory(apps/hatteda-desktop)
add_subdirectory(apps/hatteda-embedded-agent)

if(BUILD_TESTING)
    find_package(Qt6 6.11 REQUIRED COMPONENTS Test)
    add_subdirectory(tests/unit)
endif()
