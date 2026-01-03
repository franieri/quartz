add_library(system_time SHARED
    system_time.cpp
    time_functions.cpp
)

target_include_directories(system_time PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(system_time PRIVATE qz-core)

set_target_properties(system_time PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/extensions/system_time"
)
