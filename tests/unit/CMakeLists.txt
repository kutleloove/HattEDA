add_executable(hatt-ui-shell-tests MainWindowTests.cpp)
target_link_libraries(hatt-ui-shell-tests PRIVATE HattEDA::UiShell Qt6::Test)
add_test(NAME hatt-ui-shell-tests COMMAND hatt-ui-shell-tests)

add_executable(hatt-design-canvas-tests DesignCanvasTests.cpp)
target_link_libraries(hatt-design-canvas-tests PRIVATE HattEDA::UiShell Qt6::Test)
add_test(NAME hatt-design-canvas-tests COMMAND hatt-design-canvas-tests)
