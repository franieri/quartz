file(GLOB SOURCES *.cpp)
file(GLOB_RECURSE HEADERS *.h *.hpp)

set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH "${CMAKE_BINARY_DIR}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=default")

# Set output directory for this extension
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/extensions/system_json")

# nlohmann/json library configuration
set(JSON_VERSION "3.11.3")  # Update this to upgrade the library version
set(JSON_INCLUDE_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/include")
set(JSON_INCLUDE_DIR "${JSON_INCLUDE_BUILD_DIR}/nlohmann")
set(JSON_HEADER "${JSON_INCLUDE_DIR}/json.hpp")

# Download nlohmann/json if not present
if(NOT EXISTS ${JSON_HEADER})
    message(STATUS "Downloading nlohmann/json v${JSON_VERSION}...")
    file(MAKE_DIRECTORY ${JSON_INCLUDE_DIR})
    file(DOWNLOAD
        "https://github.com/nlohmann/json/releases/download/v${JSON_VERSION}/json.hpp"
        ${JSON_HEADER}
        SHOW_PROGRESS
        STATUS download_status
        TIMEOUT 30
    )
    list(GET download_status 0 status_code)
    list(GET download_status 1 status_message)
    if(NOT status_code EQUAL 0)
        message(FATAL_ERROR "Failed to download nlohmann/json: ${status_message}")
    endif()
    message(STATUS "Successfully downloaded nlohmann/json v${JSON_VERSION}")
else()
    message(STATUS "Using cached nlohmann/json v${JSON_VERSION}")
endif()

add_library(system_json SHARED ${SOURCES})
target_include_directories(system_json PRIVATE 
    ../../include/core 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${JSON_INCLUDE_BUILD_DIR}
)
target_link_libraries(system_json PRIVATE qz-core)
