add_library(hatt-agentic-mcp STATIC
    include/hatt/agentic/AgenticActionGateway.hpp
    include/hatt/agentic/AgenticMcpServer.hpp
    src/AgenticMcpServer.cpp
    include/hatt/agentic/AgenticMcpClient.hpp
    src/AgenticMcpClient.cpp
)

add_library(HattEDA::AgenticMcp ALIAS hatt-agentic-mcp)

target_include_directories(hatt-agentic-mcp PUBLIC include)
# Qt Core only (QJsonDocument, QLocalServer/QLocalSocket live in Qt Network); never Qt Widgets, so
# this library stays usable without a GUI and without depending on hatt-ui-shell (ADR-0013).
target_link_libraries(hatt-agentic-mcp PUBLIC Qt6::Core Qt6::Network)

if(MSVC)
    target_compile_options(hatt-agentic-mcp PRIVATE /W4 /permissive-)
else()
    target_compile_options(hatt-agentic-mcp PRIVATE -Wall -Wextra -Wpedantic)
endif()
