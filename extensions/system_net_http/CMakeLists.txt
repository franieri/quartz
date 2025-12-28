file(GLOB SOURCES *.cpp)
file(GLOB_RECURSE HEADERS *.h *.hpp)

set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH "${CMAKE_BINARY_DIR}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=default")

# Set output directory for this extension
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/extensions/system_net_http")

add_library(system_net_http SHARED ${SOURCES})
target_include_directories(system_net_http PRIVATE 
    ../../include/core 
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(system_net_http PRIVATE qz-core)

# Link against system networking libraries if needed
if(APPLE)
    # macOS: Use CFNetwork or Security frameworks if needed
    # For now, we'll use POSIX sockets
elseif(UNIX)
    # Linux: might need -lssl -lcrypto for HTTPS
endif()
