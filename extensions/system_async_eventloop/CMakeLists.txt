file(GLOB SOURCES *.cpp)
file(GLOB_RECURSE HEADERS *.h *.hpp)

set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH "${CMAKE_BINARY_DIR}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=default")

# Set output directory for this extension
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/extensions/system_async_eventloop")

add_library(system_async_eventloop SHARED ${SOURCES})
target_include_directories(system_async_eventloop PRIVATE 
    ../../include/core 
    ../system_net_socket
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(system_async_eventloop PRIVATE qz-core)

# Platform-specific libraries
if(WIN32)
    target_link_libraries(system_async_eventloop PRIVATE ws2_32)
elseif(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(system_async_eventloop PRIVATE Threads::Threads)
endif()
