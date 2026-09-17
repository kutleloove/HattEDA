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
