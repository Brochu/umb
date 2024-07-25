cmake_minimum_required (VERSION 3.10)

project (umb C)
set (CMAKE_EXPORT_COMPILE_COMMANDS ON)

set (DEBUG_OPTIONS /Zi /Od)
set (RELEASE_OPTIONS /O2)

set (headers
)

set (source
    ${CMAKE_CURRENT_LIST_DIR}/main.c
)

set (libs
    raylib.lib
    WinMM.lib
)

add_executable (umb
    ${source}
    ${headers}
)

target_compile_options(umb PUBLIC "$<$<CONFIG:DEBUG>:${DEBUG_OPTIONS}>")
target_compile_options(umb PUBLIC "$<$<CONFIG:RELEASE>:${RELEASE_OPTIONS}>")

target_include_directories(umb
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/include
        ${CMAKE_CURRENT_LIST_DIR}/include/raylib
)
target_link_directories(umb
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/libs
)
target_link_libraries(umb ${libs})

set_target_properties(umb
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY       "${CMAKE_CURRENT_LIST_DIR}/"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_LIST_DIR}/"
)
