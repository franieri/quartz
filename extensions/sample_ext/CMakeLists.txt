file(GLOB SOURCES *.cpp)
file(GLOB_RECURSE HEADERS *.h *.hpp)

# Set output directory for this extension
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/extensions/sample_ext")

add_library(sample_ext SHARED ${SOURCES})
target_include_directories(sample_ext PRIVATE ../../include/core ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sample_ext PRIVATE qz-core)