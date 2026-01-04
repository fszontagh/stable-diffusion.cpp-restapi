# WebUI CMakeLists.txt
# Builds the Vue.js frontend using npm

find_program(NPM_EXECUTABLE npm)

if(NOT NPM_EXECUTABLE)
    message(WARNING "npm not found. WebUI will not be built. Install Node.js to build the WebUI.")
    return()
endif()

message(STATUS "Found npm: ${NPM_EXECUTABLE}")

set(WEBUI_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(WEBUI_BUILD_DIR ${CMAKE_BINARY_DIR}/webui)

# Custom target to build the webui
add_custom_target(webui-build ALL
    COMMENT "Building WebUI..."
    WORKING_DIRECTORY ${WEBUI_SOURCE_DIR}
    COMMAND ${NPM_EXECUTABLE} install --silent
    COMMAND ${NPM_EXECUTABLE} run build
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${WEBUI_SOURCE_DIR}/dist ${WEBUI_BUILD_DIR}
)

# Set the webui path for the main executable
set(SDCPP_WEBUI_PATH ${WEBUI_BUILD_DIR} PARENT_SCOPE)

# Installation
install(DIRECTORY ${WEBUI_BUILD_DIR}/
        DESTINATION share/sdcpp-restapi/webui
        OPTIONAL)
