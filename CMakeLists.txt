cmake_minimum_required(VERSION 3.20)
project(sdcpp-restapi VERSION 1.0.0 LANGUAGES CXX C)

# C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

#
# Options
#
option(SD_CUDA    "Enable CUDA backend for stable-diffusion.cpp" OFF)
option(SD_VULKAN  "Enable Vulkan backend for stable-diffusion.cpp" OFF)
option(SD_METAL   "Enable Metal backend for stable-diffusion.cpp" OFF)
option(SD_HIPBLAS "Enable ROCm/HIP backend for stable-diffusion.cpp" OFF)
option(SD_OPENCL  "Enable OpenCL backend for stable-diffusion.cpp" OFF)
option(SDCPP_WEBUI "Build and include Web UI (requires Node.js/npm)" ON)
option(SDCPP_WEBSOCKET "Build WebSocket server for real-time updates" ON)
option(FORCE_WEBSOCKET "Force WebSocket even if WebUI is disabled" OFF)

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")

# Debug mode definition
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(SDCPP_DEBUG=1)
    message(STATUS "Debug mode: enabled (verbose SD logging)")
else()
    add_compile_definitions(SDCPP_DEBUG=0)
endif()

#
# Find required packages
#
find_package(Threads REQUIRED)

# OpenSSL for SHA256 hashing
find_package(OpenSSL REQUIRED)

# CUDA runtime for setting device scheduling flags (reduces CPU usage during generation)
if(SD_CUDA)
    find_package(CUDAToolkit REQUIRED)
    message(STATUS "CUDA toolkit found: ${CUDAToolkit_VERSION}")
endif()

#
# External dependencies via FetchContent
#
include(FetchContent)

# cpp-httplib (header-only HTTP library)
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.30.1
)
set(HTTPLIB_COMPILE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(httplib)

# nlohmann/json (JSON library)
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.12.0
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(json)

# uSockets and uWebSockets (only if WebSocket enabled)
if(SDCPP_WEBSOCKET)
    # uSockets (C library for uWebSockets)
    FetchContent_Declare(
        usockets
        GIT_REPOSITORY https://github.com/uNetworking/uSockets.git
        GIT_TAG        v0.8.8
    )
    FetchContent_MakeAvailable(usockets)

    # uWebSockets (header-only WebSocket library)
    FetchContent_Declare(
        uwebsockets
        GIT_REPOSITORY https://github.com/uNetworking/uWebSockets.git
        GIT_TAG        v20.74.0
    )
    FetchContent_MakeAvailable(uwebsockets)

    # Build uSockets as a static library (without SSL for simplicity)
    add_library(uSockets STATIC
        ${usockets_SOURCE_DIR}/src/eventing/epoll_kqueue.c
        ${usockets_SOURCE_DIR}/src/context.c
        ${usockets_SOURCE_DIR}/src/loop.c
        ${usockets_SOURCE_DIR}/src/socket.c
        ${usockets_SOURCE_DIR}/src/bsd.c
    )
    target_include_directories(uSockets PUBLIC ${usockets_SOURCE_DIR}/src)
    target_compile_definitions(uSockets PRIVATE LIBUS_NO_SSL=1)
endif()

# Find zlib for uWebSockets compression support (if WebSocket is enabled)
if(SDCPP_WEBSOCKET)
    find_package(ZLIB REQUIRED)
endif()

#
# stable-diffusion.cpp via FetchContent
#
# Pass backend options to sd.cpp
set(SD_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

if(SD_CUDA)
    set(SD_CUDA ON CACHE BOOL "" FORCE)
endif()
if(SD_VULKAN)
    set(SD_VULKAN ON CACHE BOOL "" FORCE)
endif()
if(SD_METAL)
    set(SD_METAL ON CACHE BOOL "" FORCE)
endif()
if(SD_HIPBLAS)
    set(SD_HIPBLAS ON CACHE BOOL "" FORCE)
endif()
if(SD_OPENCL)
    set(SD_OPENCL ON CACHE BOOL "" FORCE)
endif()

FetchContent_Declare(
    stable-diffusion
    GIT_REPOSITORY https://github.com/leejet/stable-diffusion.cpp.git
    GIT_TAG        f0f641a
)

# First populate to get source directory
FetchContent_GetProperties(stable-diffusion)
if(NOT stable-diffusion_POPULATED)
    FetchContent_Populate(stable-diffusion)

    # Apply sd_get_model_version_name patch directly
    set(SD_PATCH_MARKER "${stable-diffusion_SOURCE_DIR}/.sdcpp_restapi_patched")
    if(NOT EXISTS "${SD_PATCH_MARKER}")
        message(STATUS "Applying sdcpp-restapi patches to stable-diffusion.cpp...")

        # Patch stable-diffusion.h - add function declaration
        file(READ "${stable-diffusion_SOURCE_DIR}/stable-diffusion.h" SD_HEADER_CONTENT)
        string(FIND "${SD_HEADER_CONTENT}" "sd_get_model_version_name" SD_HEADER_PATCHED)
        if(SD_HEADER_PATCHED EQUAL -1)
            string(REPLACE
                "SD_API enum scheduler_t sd_get_default_scheduler(const sd_ctx_t* sd_ctx, enum sample_method_t sample_method);"
                "SD_API enum scheduler_t sd_get_default_scheduler(const sd_ctx_t* sd_ctx, enum sample_method_t sample_method);\n\n// Get the model architecture/version name (e.g., \"SD 1.x\", \"SDXL\", \"Flux\", \"Z-Image\", etc.)\nSD_API const char* sd_get_model_version_name(const sd_ctx_t* sd_ctx);"
                SD_HEADER_CONTENT "${SD_HEADER_CONTENT}")
            file(WRITE "${stable-diffusion_SOURCE_DIR}/stable-diffusion.h" "${SD_HEADER_CONTENT}")
            message(STATUS "  Patched stable-diffusion.h")
        endif()

        # Check if stable-diffusion.cpp already has the implementation (newer versions may include it)
        file(READ "${stable-diffusion_SOURCE_DIR}/stable-diffusion.cpp" SD_CPP_CONTENT)
        string(FIND "${SD_CPP_CONTENT}" "sd_get_model_version_name" SD_CPP_PATCHED)
        if(SD_CPP_PATCHED EQUAL -1)
            # Add implementation after sd_get_default_scheduler function
            string(REPLACE
                "    return DISCRETE_SCHEDULER;\n}"
                "    return DISCRETE_SCHEDULER;\n}\n\nconst char* sd_get_model_version_name(const sd_ctx_t* sd_ctx) {\n    if (sd_ctx != nullptr && sd_ctx->sd != nullptr) {\n        if (sd_ctx->sd->version < VERSION_COUNT) {\n            return model_version_to_str[sd_ctx->sd->version];\n        }\n    }\n    return \"Unknown\";\n}"
                SD_CPP_CONTENT "${SD_CPP_CONTENT}")
            file(WRITE "${stable-diffusion_SOURCE_DIR}/stable-diffusion.cpp" "${SD_CPP_CONTENT}")
            message(STATUS "  Patched stable-diffusion.cpp")
        else()
            message(STATUS "  stable-diffusion.cpp already has sd_get_model_version_name")
        endif()

        file(WRITE "${SD_PATCH_MARKER}" "patched by sdcpp-restapi")
        message(STATUS "Patches applied successfully")
    else()
        message(STATUS "stable-diffusion.cpp already patched")
    endif()

    # Now add the subdirectory
    add_subdirectory(${stable-diffusion_SOURCE_DIR} ${stable-diffusion_BINARY_DIR})
endif()

#
# Main executable
#
set(SDCPP_SOURCES
    src/main.cpp
    src/config.cpp
    src/utils.cpp
    src/model_manager.cpp
    src/queue_manager.cpp
    src/sd_wrapper.cpp
    src/request_handlers.cpp
    src/stb_impl.cpp
    src/ollama_client.cpp
    src/assistant_client.cpp
    src/architecture_manager.cpp
    src/download_manager.cpp
    src/settings_manager.cpp
)

# Add WebSocket server source only if enabled
if(SDCPP_WEBSOCKET)
    list(APPEND SDCPP_SOURCES src/websocket_server.cpp)
endif()

add_executable(sdcpp-restapi ${SDCPP_SOURCES})

target_include_directories(sdcpp-restapi PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${stable-diffusion_SOURCE_DIR}
    ${stable-diffusion_SOURCE_DIR}/thirdparty
)

# Add WebSocket includes only if enabled
if(SDCPP_WEBSOCKET)
    target_include_directories(sdcpp-restapi PRIVATE
        ${uwebsockets_SOURCE_DIR}/src
        ${usockets_SOURCE_DIR}/src
    )
endif()

target_link_libraries(sdcpp-restapi PRIVATE
    stable-diffusion
    httplib::httplib
    nlohmann_json::nlohmann_json
    Threads::Threads
    OpenSSL::Crypto
)

# Add WebSocket libraries only if enabled
if(SDCPP_WEBSOCKET)
    target_link_libraries(sdcpp-restapi PRIVATE
        uSockets
        ZLIB::ZLIB
    )
endif()

# Link CUDA runtime and set compile definition for scheduling fix
if(SD_CUDA)
    target_link_libraries(sdcpp-restapi PRIVATE CUDA::cudart)
    target_compile_definitions(sdcpp-restapi PRIVATE SDCPP_USE_CUDA=1)
endif()

# Compiler warnings
if(MSVC)
    target_compile_options(sdcpp-restapi PRIVATE /W4)
else()
    target_compile_options(sdcpp-restapi PRIVATE -Wall -Wextra -Wpedantic)
endif()

#
# WebSocket logic: Auto-disable if WebUI is disabled (unless forced)
#
if(NOT SDCPP_WEBUI AND NOT FORCE_WEBSOCKET)
    if(SDCPP_WEBSOCKET)
        message(STATUS "WebSocket: auto-disabled (WebUI is disabled, use -DFORCE_WEBSOCKET=ON to override)")
    endif()
    set(SDCPP_WEBSOCKET OFF CACHE BOOL "WebSocket disabled (WebUI disabled)" FORCE)
endif()

# WebSocket compile definition
if(SDCPP_WEBSOCKET)
    target_compile_definitions(sdcpp-restapi PRIVATE SDCPP_WEBSOCKET_ENABLED=1)
endif()

#
# Web UI (optional)
#
if(SDCPP_WEBUI)
    add_subdirectory(webui)

    # Define compile-time flag and default webui path
    target_compile_definitions(sdcpp-restapi PRIVATE
        SDCPP_WEBUI_ENABLED=1
        SDCPP_WEBUI_DEFAULT_PATH="${SDCPP_WEBUI_PATH}"
    )

    # Ensure webui is built before main executable
    add_dependencies(sdcpp-restapi webui-build)

    message(STATUS "Web UI: enabled (will be built at ${SDCPP_WEBUI_PATH})")
else()
    message(STATUS "Web UI: disabled")
endif()

# Installation
install(TARGETS sdcpp-restapi RUNTIME DESTINATION bin)
install(FILES config.example.json DESTINATION etc/sdcpp-restapi OPTIONAL)

# Print configuration summary
message(STATUS "")
message(STATUS "=== SDCPP-RESTAPI Configuration ===")
message(STATUS "CUDA backend:    ${SD_CUDA}")
message(STATUS "Vulkan backend:  ${SD_VULKAN}")
message(STATUS "Metal backend:   ${SD_METAL}")
message(STATUS "ROCm backend:    ${SD_HIPBLAS}")
message(STATUS "OpenCL backend:  ${SD_OPENCL}")
message(STATUS "Web UI:          ${SDCPP_WEBUI}")
message(STATUS "WebSocket:       ${SDCPP_WEBSOCKET}")
message(STATUS "===================================")
message(STATUS "")
