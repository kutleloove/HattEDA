# QtTest output does not reach CTest when stdout is redirected on Windows (--output-on-failure shows
# nothing), so each QtTest executable writes a plain-text log to <build>/test-logs/<test>.txt.
# CI prints and uploads that directory when tests fail. Running a test exe directly is unaffected.
set(HATTEDA_TEST_LOG_DIR "${CMAKE_BINARY_DIR}/test-logs")
file(MAKE_DIRECTORY "${HATTEDA_TEST_LOG_DIR}")

function(hatt_add_qt_test name)
    add_test(NAME ${name} COMMAND ${name} -o "${HATTEDA_TEST_LOG_DIR}/${name}.txt,txt")
endfunction()

add_executable(hatt-ui-shell-tests MainWindowTests.cpp)
target_link_libraries(hatt-ui-shell-tests PRIVATE HattEDA::UiShell Qt6::Test)
hatt_add_qt_test(hatt-ui-shell-tests)

add_executable(hatt-design-canvas-tests DesignCanvasTests.cpp)
target_link_libraries(hatt-design-canvas-tests PRIVATE HattEDA::UiShell Qt6::Test)
hatt_add_qt_test(hatt-design-canvas-tests)

add_executable(hatt-project-file-tests ProjectFileTests.cpp)
target_link_libraries(hatt-project-file-tests PRIVATE HattEDA::UiShell Qt6::Test)
hatt_add_qt_test(hatt-project-file-tests)

add_executable(hatt-component-library-tests ComponentLibraryTests.cpp)
target_link_libraries(hatt-component-library-tests PRIVATE HattEDA::UiShell Qt6::Test)
hatt_add_qt_test(hatt-component-library-tests)

add_executable(hatt-design-checks-tests DesignChecksTests.cpp)
target_link_libraries(hatt-design-checks-tests PRIVATE HattEDA::UiShell Qt6::Test)
hatt_add_qt_test(hatt-design-checks-tests)

add_executable(hatt-mvp-flow-tests MvpFlowTests.cpp)
target_link_libraries(hatt-mvp-flow-tests PRIVATE HattEDA::UiShell Qt6::Test)
hatt_add_qt_test(hatt-mvp-flow-tests)

add_executable(hatt-package-from-selection-tests PackageFromSelectionTests.cpp)
target_link_libraries(hatt-package-from-selection-tests PRIVATE HattEDA::UiShell Qt6::Test)
hatt_add_qt_test(hatt-package-from-selection-tests)

add_executable(hatt-gerber-export-tests GerberExportTests.cpp)
target_link_libraries(hatt-gerber-export-tests PRIVATE HattEDA::UiShell Qt6::Test)
hatt_add_qt_test(hatt-gerber-export-tests)

add_executable(hatt-project-safety-tests ProjectSafetyTests.cpp)
target_link_libraries(hatt-project-safety-tests PRIVATE HattEDA::UiShell Qt6::Test)
add_test(NAME hatt-project-safety-tests COMMAND hatt-project-safety-tests)

add_executable(hatt-connectivity-tests ConnectivityTests.cpp)
target_link_libraries(hatt-connectivity-tests PRIVATE HattEDA::Electrical Qt6::Test)
hatt_add_qt_test(hatt-connectivity-tests)

# Plain C++ test (no QtTest): its stdout/stderr is only captured by CTest.
add_executable(hatt-dc-solver-tests DcSolverTests.cpp)
set_target_properties(hatt-dc-solver-tests PROPERTIES AUTOMOC OFF)
target_link_libraries(hatt-dc-solver-tests PRIVATE HattEDA::Electrical)
add_test(NAME hatt-dc-solver-tests COMMAND hatt-dc-solver-tests)

add_executable(hatt-sketch-circuit-tests SketchCircuitTests.cpp)
target_link_libraries(hatt-sketch-circuit-tests PRIVATE HattEDA::UiShell Qt6::Test)
hatt_add_qt_test(hatt-sketch-circuit-tests)
