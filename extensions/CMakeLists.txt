file(GLOB EXT_DIRS LIST_DIRECTORIES true RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *)

foreach(EXT_DIR ${EXT_DIRS})
    if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${EXT_DIR})
        if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${EXT_DIR}/CMakeLists.txt)
            add_subdirectory(${EXT_DIR})
        endif()
    endif()
endforeach()