# Demonstrator MCP client (ADR-0013 "embedded agent uses the same surface as external agents"):
# a console-only binary (no Qt Widgets) that connects to a running HattEDA instance's local
# agentic MCP server, does the initialize handshake, lists actions, and exits. It is NOT the
# embedded, in-app agent itself -- there is no LLM reasoning, orchestration, or discovery of a
# running instance here; that is a separate follow-up (see ADR-0013, "Out of scope").
add_executable(hatteda-embedded-agent main.cpp)
target_link_libraries(hatteda-embedded-agent PRIVATE HattEDA::AgenticMcp)

if(MSVC)
    target_compile_options(hatteda-embedded-agent PRIVATE /W4 /permissive-)
else()
    target_compile_options(hatteda-embedded-agent PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS hatteda-embedded-agent RUNTIME DESTINATION .)
