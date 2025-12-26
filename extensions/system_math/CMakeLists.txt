add_library(system_math SHARED
    system_math.cpp
    math_functions.cpp
)

target_include_directories(system_math PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(system_math PRIVATE qz-core)

set_target_properties(system_math PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/extensions/system_math"
)
