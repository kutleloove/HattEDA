add_executable(hatteda WIN32 main.cpp)
target_link_libraries(hatteda PRIVATE HattEDA::UiShell)

# Optional release bundle: callers provide a pinned Freerouting JAR and matching Java runtime.
# Development builds remain dependency-free when these cache paths are empty.
set(HATTEDA_FREEROUTING_JAR "" CACHE FILEPATH "Freerouting JAR bundled with HattEDA")
set(HATTEDA_JAVA_RUNTIME "" CACHE PATH "Java runtime directory bundled with HattEDA")
if(HATTEDA_FREEROUTING_JAR AND HATTEDA_JAVA_RUNTIME)
    add_custom_command(TARGET hatteda POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:hatteda>/autorouter"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${HATTEDA_FREEROUTING_JAR}"
                "$<TARGET_FILE_DIR:hatteda>/autorouter/freerouting.jar"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${HATTEDA_JAVA_RUNTIME}"
                "$<TARGET_FILE_DIR:hatteda>/autorouter/runtime"
        COMMENT "Bundling Freerouting and its Java runtime")
endif()

qt_add_translations(hatteda
    TS_FILES
        ${CMAKE_SOURCE_DIR}/translations/hatteda_tr.ts
    RESOURCE_PREFIX "/i18n"
    SOURCE_TARGETS hatteda hatt-ui-shell
)

if(MSVC)
    target_compile_options(hatteda PRIVATE /W4 /permissive-)
else()
    target_compile_options(hatteda PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS hatteda RUNTIME DESTINATION .)

# Bundles the Qt/MinGW runtime and plugins hatteda needs (windeployqt under the hood on Windows),
# so `cmake --install` produces a folder that runs without Qt or MinGW on PATH (issue #5).
# The deploy script runs as its own `cmake -P` at install time, so QT_DEPLOY_BIN_DIR (default
# "bin") must be set via install(CODE ...) rather than a configure-time variable. Keep it "."
# to match the existing flat RUNTIME DESTINATION "." so hatteda.exe finds its DLLs via Windows'
# default same-directory search, instead of a "bin" subfolder the exe would not look in.
install(CODE "set(QT_DEPLOY_BIN_DIR \".\")")
qt_generate_deploy_app_script(
    TARGET hatteda
    OUTPUT_SCRIPT hatteda_deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${hatteda_deploy_script})
