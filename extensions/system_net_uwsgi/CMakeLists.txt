file(GLOB SOURCES *.cpp)
file(GLOB_RECURSE HEADERS *.h *.hpp)

set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH "${CMAKE_BINARY_DIR}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=default")

# Set output directory for this extension
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/extensions/system_net_uwsgi")

add_library(system_net_uwsgi SHARED ${SOURCES})
target_include_directories(system_net_uwsgi PRIVATE 
    ../../include/core 
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(system_net_uwsgi PRIVATE qz-core)

# Platform-specific settings
if(WIN32)
    target_link_libraries(system_net_uwsgi PRIVATE ws2_32)
    target_compile_definitions(system_net_uwsgi PRIVATE _WIN32_WINNT=0x0601)
elseif(APPLE)
    # macOS uses kqueue, included in system libraries
    # Enable modern C++ features
    target_compile_options(system_net_uwsgi PRIVATE -std=c++17)
elseif(UNIX)
    # Linux uses epoll, included in libc
    find_package(Threads REQUIRED)
    target_link_libraries(system_net_uwsgi PRIVATE Threads::Threads)
endif()

# Compiler optimizations for performance
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(system_net_uwsgi PRIVATE -O3 -march=native)
endif()
