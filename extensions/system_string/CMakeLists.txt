add_library(system_string SHARED
    system_string.cpp
    string_functions.cpp
)

target_include_directories(system_string PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(system_string PRIVATE qz-core)

set_target_properties(system_string PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/extensions/system_string"
)
