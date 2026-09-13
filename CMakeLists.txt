cmake_minimum_required(VERSION 3.25)

project(HattEDA VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

include(CTest)
find_package(Qt6 6.11 REQUIRED COMPONENTS Widgets LinguistTools)

add_subdirectory(libs/electrical)
add_subdirectory(libs/ui-shell)
add_subdirectory(apps/hatteda-desktop)

if(BUILD_TESTING)
    find_package(Qt6 6.11 REQUIRED COMPONENTS Test)
    add_subdirectory(tests/unit)
endif()
