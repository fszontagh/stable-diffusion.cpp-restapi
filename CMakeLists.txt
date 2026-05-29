cmake_minimum_required(VERSION 3.20)

# Read version from VERSION file (single source of truth)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" SDCPP_VERSION_RAW)
string(STRIP "${SDCPP_VERSION_RAW}" SDCPP_VERSION)
message(STATUS "sdcpp-restapi version: ${SDCPP_VERSION}")

project(sdcpp-restapi VERSION ${SDCPP_VERSION} LANGUAGES CXX C)

# Get git commit hash for build info
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE SDCPP_GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT SDCPP_GIT_COMMIT)
    set(SDCPP_GIT_COMMIT "unknown")
endif()
message(STATUS "Git commit: ${SDCPP_GIT_COMMIT}")

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
option(SDCPP_ASSISTANT "Build LLM Assistant feature" ON)
option(SD_EXPERIMENTAL_OFFLOAD "Enable experimental dynamic tensor offloading (requires forked sd.cpp)" OFF)
# Selects which fork branch the experimental build pulls. Only meaningful when
# SD_EXPERIMENTAL_OFFLOAD=ON. Two fork variants exist with incompatible offload
# APIs — see CMakeLists.txt FetchContent block + #ifdef SDCPP_UNIFIED_STREAMING
# in model_manager.cpp for which code paths are gated by this.
#   OFF (default): feature/vram-offloading-v2 — legacy sd_offload_config_t API,
#                  per-component offload modes (none/cond_only/aggressive/
#                  layer_streaming), full streaming knobs.
#   ON:            feature/unified-streaming — new minimal API with a single
#                  stream_layers bool on top of max_vram. Removes the entire
#                  sd_offload_config_t struct and the sd_offload_to_cpu/
#                  sd_reload_to_gpu helpers.
option(SD_UNIFIED_STREAMING "Use the unified-streaming fork branch instead of vram-offloading-v2 (requires SD_EXPERIMENTAL_OFFLOAD=ON)" OFF)
# Local-build optimization: when ON, ask nvcc to compile only for the GPU
# physically present in this machine (uses CMAKE_CUDA_ARCHITECTURES=native).
# Cuts a clean CUDA build by ~40-60% because nvcc skips the ~6 other arches
# it normally emits. NEVER turn this on for CI / release builds — the
# resulting binary won't run on any other GPU model.
option(SD_CUDA_ARCH_NATIVE "Compile CUDA only for the local GPU (faster local builds, non-portable binary)" OFF)
option(SDCPP_MCP "Build MCP (Model Context Protocol) server support" ON)
set(SDCPP_LOCAL_SOURCE "" CACHE PATH "Path to local stable-diffusion.cpp source (for development/testing)")

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

# libcurl for model downloads (replaces cpp-httplib client which had TLS issues
# with HuggingFace's CloudFront CDN — "Failed to read connection" at byte 0).
find_package(CURL REQUIRED)

# CUDA runtime for setting device scheduling flags (reduces CPU usage during generation)
if(SD_CUDA)
    find_package(CUDAToolkit REQUIRED)
    message(STATUS "CUDA toolkit found: ${CUDAToolkit_VERSION}")
    # Local-build-only: pin CUDA arch to the host GPU. Must be set BEFORE
    # the sd.cpp subdirectory is added, otherwise sd.cpp's ggml-cuda target
    # has already snapshotted the default arch list. The `native` value tells
    # nvcc to probe the local device and emit code only for its compute
    # capability — for a single-GPU workstation this is roughly a 4× speedup
    # on the clean ggml-cuda kernel compile.
    if(SD_CUDA_ARCH_NATIVE)
        set(CMAKE_CUDA_ARCHITECTURES "native" CACHE STRING "CUDA arches" FORCE)
        message(STATUS "CUDA arch:       native (local GPU only — binary NOT portable)")
    endif()
endif()

#
# External dependencies via FetchContent
#
include(FetchContent)

# Allow FetchContent_Populate for stable-diffusion.cpp (we need to patch between
# populate and add_subdirectory, which FetchContent_MakeAvailable doesn't support)
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

# cpp-httplib v0.36.0 (header-only HTTP library with WebSocket support)
# We fetch content but DON'T use MakeAvailable/httplib::httplib target because
# sd.cpp's thirdparty bundles an older httplib without WebSocket support, and CMake's
# include path deduplication causes the old version to take priority.
# Instead, we add the include path manually with BEFORE to ensure correct version.
#
# PATCH: cpp-httplib v0.36.0 rejects unknown HTTP methods at parse_request_line()
# (httplib.h ~line 10717 has a closed allow-list of GET/HEAD/POST/PUT/DELETE/CONNECT/
# OPTIONS/TRACE/PATCH/PRI). For WebDAV we need PROPFIND/MKCOL/MOVE/COPY/LOCK/UNLOCK
# to reach our pre-routing handler. The patch_command appends those to the set.
# This is idempotent — re-running CMake on an already-patched tree is a no-op.
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.36.0
    GIT_SHALLOW    TRUE
    PATCH_COMMAND  ${CMAKE_COMMAND} -DHTTPLIB_HEADER=<SOURCE_DIR>/httplib.h
                   -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/PatchHttplibWebDAV.cmake
    UPDATE_DISCONNECTED 1
)
FetchContent_MakeAvailable(httplib)

# nlohmann/json (single header download - much faster than cloning repo)
set(JSON_HEADER_DIR "${CMAKE_BINARY_DIR}/_deps/json-header")
set(JSON_HEADER_FILE "${JSON_HEADER_DIR}/nlohmann/json.hpp")
if(NOT EXISTS "${JSON_HEADER_FILE}")
    message(STATUS "Downloading nlohmann/json single header...")
    file(MAKE_DIRECTORY "${JSON_HEADER_DIR}/nlohmann")
    file(DOWNLOAD
        "https://github.com/nlohmann/json/releases/download/v3.12.0/json.hpp"
        "${JSON_HEADER_FILE}"
        STATUS JSON_DOWNLOAD_STATUS
    )
    list(GET JSON_DOWNLOAD_STATUS 0 JSON_DOWNLOAD_ERROR)
    if(JSON_DOWNLOAD_ERROR)
        message(FATAL_ERROR "Failed to download nlohmann/json: ${JSON_DOWNLOAD_STATUS}")
    endif()
endif()
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE "${JSON_HEADER_DIR}")

# WebSocket support is now built-in to cpp-httplib v0.33+
# No additional dependencies needed for WebSocket

#
# stable-diffusion.cpp via FetchContent or local source
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

# Check if using local source or fetching from GitHub
if(SDCPP_LOCAL_SOURCE)
    message(STATUS "Using local stable-diffusion.cpp source: ${SDCPP_LOCAL_SOURCE}")
    if(NOT EXISTS "${SDCPP_LOCAL_SOURCE}/CMakeLists.txt")
        message(FATAL_ERROR "SDCPP_LOCAL_SOURCE does not contain a valid stable-diffusion.cpp source: ${SDCPP_LOCAL_SOURCE}")
    endif()
    set(stable-diffusion_SOURCE_DIR "${SDCPP_LOCAL_SOURCE}")
    set(stable-diffusion_BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/stable-diffusion-build")
    set(SDCPP_USING_LOCAL_SOURCE TRUE)
    # Add the local source subdirectory directly (no patching needed for local dev)
    add_subdirectory(${stable-diffusion_SOURCE_DIR} ${stable-diffusion_BINARY_DIR})
else()
    # Only the submodules we actually compile against. The bundled
    # examples/server/frontend submodule sometimes points at deleted commits and
    # we don't link the example server, so we skip it.
    set(SDCPP_SUBMODULES "ggml" "thirdparty/libwebm" "thirdparty/libwebp")
    if(SD_EXPERIMENTAL_OFFLOAD)
        # Pick which fork branch supplies the experimental offload code. Both
        # branches are on the same fork repo; only the public offload API and
        # streaming implementation differ. See SD_UNIFIED_STREAMING option doc
        # for the full distinction.
        if(SD_UNIFIED_STREAMING)
            set(SDCPP_FORK_BRANCH "feature/unified-streaming")
            message(STATUS "Experimental offload: ENABLED (unified-streaming) - Using forked sd.cpp feature/unified-streaming branch")
        else()
            set(SDCPP_FORK_BRANCH "feature/vram-offloading-v2")
            message(STATUS "Experimental offload: ENABLED (vram-offloading-v2) - Using forked sd.cpp feature/vram-offloading-v2 branch")
        endif()
        # Track the branch tip so user-pushed improvements (e.g. the May 4
        # streaming speedup wave) are pulled automatically on rebuild.
        # GIT_SHALLOW works with branch names — and would NOT work if we pinned
        # to a SHA here (CMake's `git clone --depth 1 --branch <ref>` only
        # accepts branch/tag names, not commit hashes).
        FetchContent_Declare(
            stable-diffusion
            GIT_REPOSITORY https://github.com/fszontagh/stable-diffusion.cpp.git
            GIT_TAG        ${SDCPP_FORK_BRANCH}
            GIT_SHALLOW    TRUE
            GIT_SUBMODULES ${SDCPP_SUBMODULES}
            GIT_SUBMODULES_RECURSE TRUE
        )
    else()
        # Pinned to the upstream commit that the fork's feature/vram-offloading-v2
        # branch most recently merged from, so the public API surface (samplers,
        # ctx_params fields, gen_params fields) matches between OFFLOAD=ON/OFF.
        # 1ceb5bd is leejet/master at the time of the last fork resync — includes
        # Longcat-Image (#1053), LTXAV/LTX 2.3 (#1463, #1505), Gradient Estimation
        # sampler (#1484), `--max-vram -1` auto-detect (#1498), and the breaking
        # generate_video/new_upscaler_ctx signature changes.
        # GIT_SHALLOW must be FALSE because GIT_TAG is a commit SHA — shallow
        # clone with a SHA fails (`fatal: reference is not a tree`). Full clone
        # adds ~25 MB to the build context but is reliable.
        FetchContent_Declare(
            stable-diffusion
            GIT_REPOSITORY https://github.com/leejet/stable-diffusion.cpp.git
            GIT_TAG        1ceb5bd9df7784bcdf67dd9ed8bf0198b542ebc9
            GIT_SHALLOW    FALSE
            GIT_SUBMODULES ${SDCPP_SUBMODULES}
            GIT_SUBMODULES_RECURSE TRUE
        )
    endif()
    # First populate to get source directory
    FetchContent_GetProperties(stable-diffusion)
endif()

if(NOT SDCPP_USING_LOCAL_SOURCE AND NOT stable-diffusion_POPULATED)
    FetchContent_Populate(stable-diffusion)

    # Apply sd_get_model_version_name patch directly. The guard inspects the
    # actual source content rather than a marker file — a marker file is too
    # easy to fool when developers switch fork branches via `git reset --hard`
    # in the _deps tree (the marker is untracked, survives the reset, then
    # CMake skips patching even though the function is no longer present).
    # Locate header + impl up-front; both paths moved around sd.cpp 28ef93c.
    if(EXISTS "${stable-diffusion_SOURCE_DIR}/include/stable-diffusion.h")
        set(SD_HEADER_PATH "${stable-diffusion_SOURCE_DIR}/include/stable-diffusion.h")
    else()
        set(SD_HEADER_PATH "${stable-diffusion_SOURCE_DIR}/stable-diffusion.h")
    endif()
    if(EXISTS "${stable-diffusion_SOURCE_DIR}/src/stable-diffusion.cpp")
        set(SD_CPP_PATH "${stable-diffusion_SOURCE_DIR}/src/stable-diffusion.cpp")
    else()
        set(SD_CPP_PATH "${stable-diffusion_SOURCE_DIR}/stable-diffusion.cpp")
    endif()

    # Patch header — add function declaration if not already present.
    file(READ "${SD_HEADER_PATH}" SD_HEADER_CONTENT)
    string(FIND "${SD_HEADER_CONTENT}" "sd_get_model_version_name" SD_HEADER_PATCHED)
    if(SD_HEADER_PATCHED EQUAL -1)
        message(STATUS "Patching ${SD_HEADER_PATH} — adding sd_get_model_version_name declaration")
        string(REPLACE
            "SD_API enum scheduler_t sd_get_default_scheduler(const sd_ctx_t* sd_ctx, enum sample_method_t sample_method);"
            "SD_API enum scheduler_t sd_get_default_scheduler(const sd_ctx_t* sd_ctx, enum sample_method_t sample_method);\n\n// Get the model architecture/version name (e.g., \"SD 1.x\", \"SDXL\", \"Flux\", \"Z-Image\", etc.)\nSD_API const char* sd_get_model_version_name(const sd_ctx_t* sd_ctx);"
            SD_HEADER_CONTENT "${SD_HEADER_CONTENT}")
        file(WRITE "${SD_HEADER_PATH}" "${SD_HEADER_CONTENT}")
    endif()

    # Patch impl — add implementation if not already present.
    file(READ "${SD_CPP_PATH}" SD_CPP_CONTENT)
    string(FIND "${SD_CPP_CONTENT}" "sd_get_model_version_name" SD_CPP_PATCHED)
    if(SD_CPP_PATCHED EQUAL -1)
        message(STATUS "Patching ${SD_CPP_PATH} — adding sd_get_model_version_name impl")
        string(REPLACE
            "    return DISCRETE_SCHEDULER;\n}"
            "    return DISCRETE_SCHEDULER;\n}\n\nconst char* sd_get_model_version_name(const sd_ctx_t* sd_ctx) {\n    if (sd_ctx != nullptr && sd_ctx->sd != nullptr) {\n        if (sd_ctx->sd->version < VERSION_COUNT) {\n            return model_version_to_str[sd_ctx->sd->version];\n        }\n    }\n    return \"Unknown\";\n}"
            SD_CPP_CONTENT "${SD_CPP_CONTENT}")
        file(WRITE "${SD_CPP_PATH}" "${SD_CPP_CONTENT}")
    endif()

    # Now add the subdirectory
    add_subdirectory(${stable-diffusion_SOURCE_DIR} ${stable-diffusion_BINARY_DIR})
endif()

# Add CUDA include directories to stable-diffusion target if CUDA is enabled
# This is needed for the forked sd.cpp with experimental offload which includes <cuda_runtime.h>
if(SD_CUDA AND TARGET stable-diffusion)
    target_include_directories(stable-diffusion PRIVATE ${CUDAToolkit_INCLUDE_DIRS})
    message(STATUS "Added CUDA include directories to stable-diffusion target")
endif()

# Suppress noisy warnings from third-party headers vendored by sd.cpp. These
# are well-known signal-free patterns: stb_image* (single-header libs with
# conditional dead code), darts.h (T5 tokenizer using old C++ enum
# arithmetic), and util.cpp's std::wstring_convert (deprecated in C++17 but
# still widely used). Scoped to the stable-diffusion target so restapi
# sources keep every warning enabled. The flags use $<COMPILE_LANGUAGE:CXX>
# so nvcc's command line stays untouched (nvcc rejects these flags).
if(TARGET stable-diffusion)
    target_compile_options(stable-diffusion PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-unused-value>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-unused-parameter>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-unused-variable>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-missing-field-initializers>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-enum-enum-conversion>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-declarations>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-stringop-overflow>
    )
endif()

# httplib's own SSL wrappers reference its deprecated SSLClient::ssl_context /
# get_verify_result / set_server_certificate_verifier APIs — 3 warnings per
# TU that pulls in httplib.h. We can't suppress them cleanly: making httplib's
# include SYSTEM (the usual fix) causes CMake's include dedup to lose priority
# to sd.cpp's older bundled httplib (which lacks WebSocket), and per-target
# -Wno-deprecated-declarations would hide real deprecations in our own code.
# Accept the 3 warnings as residue; revisit if httplib pushes a fix or we
# switch off httplib.

#
# Main executable
#
set(SDCPP_SOURCES
    src/main.cpp
    src/config.cpp
    src/utils.cpp
    src/auth_manager.cpp
    src/model_manager.cpp
    src/queue_manager.cpp
    src/sd_wrapper.cpp
    src/request_handlers.cpp
    src/api_registry.cpp
    src/stb_impl.cpp
    src/memory_utils.cpp
    src/architecture_manager.cpp
    src/download_manager.cpp
    src/settings_manager.cpp
    src/cuda_arch_check.cpp
    src/docs_index.cpp
    src/prompt_template.cpp
    src/url_utils.cpp
)

# Add assistant sources only if enabled
if(SDCPP_ASSISTANT)
    list(APPEND SDCPP_SOURCES src/assistant_client.cpp src/tool_executor.cpp)
endif()

# Add WebSocket server source only if enabled
if(SDCPP_WEBSOCKET)
    list(APPEND SDCPP_SOURCES src/websocket_server.cpp)
endif()

# Add MCP server source only if enabled
if(SDCPP_MCP)
    list(APPEND SDCPP_SOURCES src/mcp_server.cpp)
endif()

add_executable(sdcpp-restapi ${SDCPP_SOURCES})

target_include_directories(sdcpp-restapi PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${stable-diffusion_SOURCE_DIR}
    ${stable-diffusion_SOURCE_DIR}/include
    ${stable-diffusion_SOURCE_DIR}/src
)
# Mark sd.cpp's thirdparty/ as a SYSTEM include — that's where the noisy
# single-header libraries live (stb_image*, stb_image_resize, stb_image_write,
# darts.h). GCC suppresses warnings from anything found under a -isystem path,
# so the ~95 stb/darts warnings emitted while compiling our src/stb_impl.cpp
# (and any other restapi TU that pulls in those headers) go away.
# Doesn't affect the bundled httplib priority dance below — that uses a
# separate non-system BEFORE include for httplib_SOURCE_DIR, and httplib.h is
# never accessed via this thirdparty path.
target_include_directories(sdcpp-restapi SYSTEM PRIVATE
    ${stable-diffusion_SOURCE_DIR}/thirdparty
)

# Ensure our httplib v0.36.0 (with WebSocket support) takes priority over
# stable-diffusion.cpp's bundled httplib v0.28.0 (without WebSocket).
# sd.cpp's thirdparty/ propagates an old httplib.h via PUBLIC includes.
# httplib::httplib adds its path as -isystem, which CMake deduplicates against -I.
# Solution: strip httplib's system include from the target and add it as a BEFORE
# non-system include on our target. This way our path comes first in the -I list,
# so our newer httplib (with WebSocket) wins over sd.cpp's bundled older copy.
# Trade-off: keeping this as non-system means GCC emits warnings from httplib.h
# itself (3 SSL deprecations — see note above the suppression block for why
# we don't fix those). Switching to SYSTEM would lose the priority over the
# bundled httplib and break WebSocket compilation.
set_target_properties(httplib PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "")
set_target_properties(httplib PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "")
target_include_directories(sdcpp-restapi BEFORE PRIVATE ${httplib_SOURCE_DIR})

# WebSocket includes are provided by httplib (built-in since v0.33)

target_link_libraries(sdcpp-restapi PRIVATE
    stable-diffusion
    httplib::httplib
    nlohmann_json
    Threads::Threads
    OpenSSL::Crypto
    CURL::libcurl
)

# WebSocket support is built into httplib - no additional link libraries needed

# Link CUDA runtime and set compile definition for scheduling fix
if(SD_CUDA)
    target_link_libraries(sdcpp-restapi PRIVATE CUDA::cudart)
    target_compile_definitions(sdcpp-restapi PRIVATE SDCPP_USE_CUDA=1)
    # Bake the compiled CUDA arch list into the binary so the startup sanity
    # check can detect arch mismatches (e.g. binary built for sm_89 but
    # running on sm_86) before the first kernel dispatch crashes the server.
    # NOTE: CMake list values use ';' as separator; passing one through to a
    # -DFOO="..." compile flag breaks shell parsing because ';' is a command
    # separator. Convert to commas — cuda_arch_check.cpp's parser handles
    # both ';' and ',' as delimiters.
    string(REPLACE ";" "," _SDCPP_CUDA_ARCHS_STR "${CMAKE_CUDA_ARCHITECTURES}")
    target_compile_definitions(sdcpp-restapi PRIVATE
        SDCPP_CUDA_ARCHS="${_SDCPP_CUDA_ARCHS_STR}"
    )
endif()

# Version information compile definitions (from VERSION file)
target_compile_definitions(sdcpp-restapi PRIVATE
    SDCPP_VERSION="${SDCPP_VERSION}"
    SDCPP_GIT_COMMIT="${SDCPP_GIT_COMMIT}"
)

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

# Assistant compile definition
if(SDCPP_ASSISTANT)
    target_compile_definitions(sdcpp-restapi PRIVATE SDCPP_ASSISTANT_ENABLED=1)
endif()

# Experimental offload compile definitions. Two flags travel together — code
# that gates on SDCPP_UNIFIED_STREAMING always also gates on
# SDCPP_EXPERIMENTAL_OFFLOAD (the unified-streaming branch can't be active
# without the experimental fork being selected).
if(SD_EXPERIMENTAL_OFFLOAD)
    target_compile_definitions(sdcpp-restapi PRIVATE SDCPP_EXPERIMENTAL_OFFLOAD=1)
    if(SD_UNIFIED_STREAMING)
        target_compile_definitions(sdcpp-restapi PRIVATE SDCPP_UNIFIED_STREAMING=1)
    endif()
elseif(SD_UNIFIED_STREAMING)
    message(WARNING "SD_UNIFIED_STREAMING=ON has no effect without SD_EXPERIMENTAL_OFFLOAD=ON")
endif()

# MCP compile definition
if(SDCPP_MCP)
    target_compile_definitions(sdcpp-restapi PRIVATE SDCPP_MCP_ENABLED=1)
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

#
# Documentation files (copy to build dir for serving)
#
set(SDCPP_DOCS_PATH "${CMAKE_BINARY_DIR}/docs")
add_custom_target(docs-copy ALL
    COMMENT "Copying documentation files..."
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/docs ${SDCPP_DOCS_PATH}
)
target_compile_definitions(sdcpp-restapi PRIVATE
    SDCPP_DOCS_DEFAULT_PATH="${SDCPP_DOCS_PATH}"
)
add_dependencies(sdcpp-restapi docs-copy)

# Installation
install(TARGETS sdcpp-restapi RUNTIME DESTINATION bin)
install(FILES config.example.json DESTINATION etc/sdcpp-restapi OPTIONAL)
install(DIRECTORY ${SDCPP_DOCS_PATH}/
        DESTINATION share/sdcpp-restapi/docs
        OPTIONAL)

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
message(STATUS "LLM Assistant:   ${SDCPP_ASSISTANT}")
message(STATUS "Exp. Offload:    ${SD_EXPERIMENTAL_OFFLOAD}")
if(SD_EXPERIMENTAL_OFFLOAD)
    if(SD_UNIFIED_STREAMING)
        message(STATUS "  └─ variant:    unified-streaming (fork branch: feature/unified-streaming)")
    else()
        message(STATUS "  └─ variant:    vram-offloading-v2 (fork branch: feature/vram-offloading-v2)")
    endif()
endif()
message(STATUS "MCP Server:      ${SDCPP_MCP}")
message(STATUS "===================================")
message(STATUS "")
