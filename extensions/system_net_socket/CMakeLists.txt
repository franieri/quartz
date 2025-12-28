file(GLOB SOURCES *.cpp)
file(GLOB_RECURSE HEADERS *.h *.hpp)

set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH "${CMAKE_BINARY_DIR}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=default")

# Set output directory for this extension
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/extensions/system_net_socket")

add_library(system_net_socket SHARED ${SOURCES})
target_include_directories(system_net_socket PRIVATE 
    ../../include/core 
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(system_net_socket PRIVATE qz-core)

# Platform-specific network libraries
if(WIN32)
    target_link_libraries(system_net_socket PRIVATE ws2_32 mswsock)
    target_compile_definitions(system_net_socket PRIVATE _WIN32_WINNT=0x0601)
elseif(APPLE)
    # macOS uses kqueue, included in system libraries
elseif(UNIX)
    # Linux uses epoll, included in libc
    # For older systems, may need -lpthread
    find_package(Threads REQUIRED)
    target_link_libraries(system_net_socket PRIVATE Threads::Threads)
endif()
