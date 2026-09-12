add_executable(hatteda WIN32 main.cpp)
target_link_libraries(hatteda PRIVATE HattEDA::UiShell)

qt_add_translations(hatteda
    TS_FILES
        ${CMAKE_SOURCE_DIR}/translations/hatteda_tr.ts
    RESOURCE_PREFIX "/i18n"
)

if(MSVC)
    target_compile_options(hatteda PRIVATE /W4 /permissive-)
else()
    target_compile_options(hatteda PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS hatteda RUNTIME DESTINATION .)
