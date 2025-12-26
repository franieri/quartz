file(GLOB SOURCES *.cpp)
file(GLOB_RECURSE HEADERS *.h *.hpp)

set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH "${CMAKE_BINARY_DIR}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=default")

# Set output directory for this extension
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/extensions/system_error")

add_library(system_error SHARED ${SOURCES})
target_include_directories(system_error PRIVATE ../../include/core ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(system_error PRIVATE qz-core)
