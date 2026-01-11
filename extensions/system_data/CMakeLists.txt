add_library(system_data SHARED
    system_data.cpp
    dataframe.cpp
    series.cpp
    data_functions.cpp
    io_functions.cpp
)

target_include_directories(system_data PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(system_data PRIVATE qz-core)

# Enable SIMD optimizations for performance
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(system_data PRIVATE -O3 -march=native -ffast-math)
endif()

set_target_properties(system_data PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/extensions/system_data"
)
