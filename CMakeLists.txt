cmake_minimum_required(VERSION 3.22)
project(claude-code-cpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type defaults to Release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Module options
option(BUILD_TESTS       "Build test suite"              ON)
option(BUILD_BENCHMARKS  "Build benchmarks"              OFF)
option(ENABLE_MCP        "Enable MCP protocol support"    ON)
option(ENABLE_LSP        "Enable LSP client support"      ON)
option(ENABLE_BRIDGE     "Enable IDE bridge support"      ON)
option(ENABLE_AGENT      "Enable agent swarm support"     ON)
option(ENABLE_PLUGINS    "Enable plugin system"           ON)
option(ENABLE_VOICE      "Enable voice input/output"      OFF)
option(ENABLE_SANDBOX    "Enable sandbox execution"       ON)

# Compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
    if(CMAKE_BUILD_TYPE MATCHES "Debug")
        add_compile_options(-g -O0 -fno-omit-frame-pointer)
    else()
        add_compile_options(-O2 -DNDEBUG)
    endif()
endif()

# Dependencies via FetchContent
include(FetchContent)

# === nlohmann/json ===
FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(json)

# === libcurl ===
find_package(CURL REQUIRED)

# === OpenSSL ===
find_package(OpenSSL REQUIRED)

# === Threads ===
find_package(Threads REQUIRED)

# === spdlog (logging) ===
FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.14.1
)
FetchContent_MakeAvailable(spdlog)

# === CLI11 ===
FetchContent_Declare(cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG        v2.4.2
)
FetchContent_MakeAvailable(cli11)

# === GoogleTest ===
if(BUILD_TESTS)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

# === Google Benchmark ===
if(BUILD_BENCHMARKS)
    FetchContent_Declare(benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG        v1.8.3
    )
    FetchContent_MakeAvailable(benchmark)
endif()

# ============================================================
# Source files
# ============================================================

set(SOURCES
    src/api/anthropic_client.cpp
    src/api/sse_parser.cpp
    src/api/auth.cpp
    src/app/cli.cpp
    src/app/config.cpp
    src/app/lifecycle.cpp
    src/engine/query_engine.cpp
    src/engine/query_pipeline.cpp
    src/engine/context_builder.cpp
    src/engine/cost_tracker.cpp
    src/engine/stream_parser.cpp
    src/engine/message.cpp
    src/tools/tool_registry.cpp
    src/tools/bash_tool.cpp
    src/tools/file_read_tool.cpp
    src/tools/file_write_tool.cpp
    src/tools/file_edit_tool.cpp
    src/tools/glob_tool.cpp
    src/tools/grep_tool.cpp
    src/tools/web_fetch_tool.cpp
    src/tools/web_search_tool.cpp
    src/tools/todo_write_tool.cpp
    src/commands/command_registry.cpp
    src/commands/command_system.cpp
    src/permissions/permission_manager.cpp
    src/state/app_state.cpp
    src/util/file_utils.cpp
    src/util/git_utils.cpp
    src/util/string_utils.cpp
    src/util/crypto_utils.cpp
    src/util/process_utils.cpp
    src/util/platform.cpp
)

set(HEADERS
    src/api/anthropic_client.h
    src/api/sse_parser.h
    src/api/auth.h
    src/engine/query_engine.h
    src/engine/query_pipeline.h
    src/engine/context_builder.h
    src/engine/cost_tracker.h
    src/engine/stream_parser.h
    src/engine/message.h
    src/tools/tool_base.h
    src/tools/tool_registry.h
    src/tools/bash_tool.h
    src/tools/file_read_tool.h
    src/tools/file_write_tool.h
    src/tools/file_edit_tool.h
    src/tools/glob_tool.h
    src/tools/grep_tool.h
    src/tools/web_fetch_tool.h
    src/tools/web_search_tool.h
    src/commands/command_registry.h
    src/permissions/permission_manager.h
    src/permissions/permission_mode.h
    src/permissions/sandbox.h
    src/state/app_state.h
    src/util/file_utils.h
    src/util/git_utils.h
    src/util/string_utils.h
    src/util/crypto_utils.h
    src/util/process_utils.h
    src/util/platform.h
)

# Conditional sources
if(ENABLE_MCP)
    list(APPEND SOURCES
        src/tools/mcp_tool.cpp
        src/protocol/mcp/mcp_client.cpp
        src/protocol/mcp/mcp_transport.cpp
        src/protocol/mcp/mcp_auth.cpp
        src/protocol/json_rpc.cpp
    )
    list(APPEND HEADERS
        src/tools/mcp_tool.h
        src/protocol/mcp/mcp_client.h
        src/protocol/mcp/mcp_transport.h
        src/protocol/mcp/mcp_auth.h
        src/protocol/mcp/mcp_types.h
        src/protocol/json_rpc.h
    )
endif()

if(ENABLE_LSP)
    list(APPEND SOURCES
        src/tools/lsp_tool.cpp
        src/protocol/lsp/lsp_client.cpp
    )
    list(APPEND HEADERS
        src/tools/lsp_tool.h
        src/protocol/lsp/lsp_client.h
        src/protocol/lsp/lsp_types.h
    )
endif()

if(ENABLE_AGENT)
    list(APPEND SOURCES
        src/tools/agent_tool.cpp
        src/agent/agent_manager.cpp
        src/agent/coordinator.cpp
        src/agent/team.cpp
        src/agent/message_bus.cpp
    )
    list(APPEND HEADERS
        src/tools/agent_tool.h
        src/agent/agent_manager.h
        src/agent/coordinator.h
        src/agent/team.h
        src/agent/message_bus.h
    )
endif()

if(ENABLE_BRIDGE)
    list(APPEND SOURCES
        src/bridge/bridge_server.cpp
        src/bridge/session_manager.cpp
    )
    list(APPEND HEADERS
        src/bridge/bridge_server.h
        src/bridge/session_manager.h
    )
endif()

if(ENABLE_PLUGINS)
    list(APPEND SOURCES src/plugins/plugin_loader.cpp)
    list(APPEND HEADERS src/plugins/plugin_loader.h src/plugins/plugin_api.h)
endif()

if(ENABLE_SANDBOX)
    list(APPEND SOURCES src/permissions/sandbox.cpp)
endif()

# UI
list(APPEND SOURCES
    src/ui/terminal.cpp
    src/ui/output_renderer.cpp
)

# P4 Commands
list(APPEND SOURCES
    src/commands/command_system.cpp
)

# P5 Agent
list(APPEND SOURCES
    src/agent/coordinator.cpp
)

# P7 Plugins
list(APPEND SOURCES
    src/plugins/plugin_api.cpp
)

# P8 Bridge
list(APPEND SOURCES
    src/bridge/bridge_server.cpp
)

# ============================================================
# Main library
# ============================================================
add_library(claude_core STATIC ${SOURCES} ${HEADERS})

target_include_directories(claude_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(claude_core PUBLIC
    nlohmann_json::nlohmann_json
    CURL::libcurl
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    spdlog::spdlog
    CLI11::CLI11
)

# ============================================================
# Main executable
# ============================================================
add_executable(claude-code src/main.cpp)
target_link_libraries(claude-code PRIVATE claude_core)

# ============================================================
# Tests
# ============================================================
if(BUILD_TESTS)
    enable_testing()

    # Collect test sources
    file(GLOB_RECURSE TEST_SOURCES tests/unit/*.cpp)
    list(FILTER TEST_SOURCES EXCLUDE REGEX "main.cpp$")

    foreach(test_file ${TEST_SOURCES})
        get_filename_component(test_name ${test_file} NAME_WE)
        add_executable(test_${test_name} ${test_file})
        target_link_libraries(test_${test_name} PRIVATE claude_core GTest::gtest GTest::gtest_main)
        add_test(NAME test_${test_name} COMMAND test_${test_name})
    endforeach()

    # Integration tests
    file(GLOB_RECURSE INTEG_SOURCES tests/integration/*.cpp)
    foreach(test_file ${INTEG_SOURCES})
        get_filename_component(test_name ${test_file} NAME_WE)
        add_executable(integ_${test_name} ${test_file})
        target_link_libraries(integ_${test_name} PRIVATE claude_core GTest::gtest GTest::gtest_main)
        add_test(NAME integ_${test_name} COMMAND integ_${test_name})
    endforeach()
endif()

# ============================================================
# Benchmarks
# ============================================================
if(BUILD_BENCHMARKS)
    file(GLOB_RECURSE BENCH_SOURCES tests/benchmark/*.cpp)
    foreach(bench_file ${BENCH_SOURCES})
        get_filename_component(bench_name ${bench_file} NAME_WE)
        add_executable(bench_${bench_name} ${bench_file})
        target_link_libraries(bench_${bench_name} PRIVATE claude_core benchmark::benchmark)
    endforeach()
endif()
