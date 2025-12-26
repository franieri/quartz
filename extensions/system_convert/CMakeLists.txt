add_library(system_convert SHARED
    system_convert.cpp
    convert_functions.cpp
)

target_include_directories(system_convert PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(system_convert PRIVATE qz-core)

set_target_properties(system_convert PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/extensions/system_convert"
)
