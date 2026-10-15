# The weft runtime as one DLL for Unreal: weft's static libraries (its View, scene and DOM, QuickJS, Yoga, FreeType,
# HarfBuzz) linked behind the C ABI of include/weft, which is all the DLL exports. Unreal's own FreeType and HarfBuzz
# never meet weft's, and the plugin needs nothing but the headers, the import library and the DLL.
#
#   cmake -S Native -B <build> -DWEFT_SOURCE_DIR=<weft checkout>
#   cmake --build <build> --config Release
#
# Scripts/BuildWeft.ps1 does both and copies the result into Source/ThirdParty/WeftLibrary.
cmake_minimum_required(VERSION 3.25)
project(WeftUnreal LANGUAGES C CXX)

set(WEFT_SOURCE_DIR "" CACHE PATH "The weft checkout to build")
if(NOT EXISTS "${WEFT_SOURCE_DIR}/include/weft/weft.h")
    message(FATAL_ERROR "WEFT_SOURCE_DIR (${WEFT_SOURCE_DIR}) is not a weft checkout")
endif()

# Weft's libraries only: no tests, probes or samples.
set(WEFT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WEFT_BUILD_PROBES OFF CACHE BOOL "" FORCE)
set(WEFT_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${WEFT_SOURCE_DIR}" weft)

# The commit the DLL is built from, which the plugin logs when it loads the DLL.
execute_process(COMMAND git -C "${WEFT_SOURCE_DIR}" rev-parse HEAD OUTPUT_VARIABLE weft_commit
    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
execute_process(COMMAND git -C "${WEFT_SOURCE_DIR}" status --porcelain --untracked-files=no OUTPUT_VARIABLE weft_dirty
    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(weft_dirty)
    string(APPEND weft_commit "+changes")
endif()

# Every function the public headers declare is exported, by name (a module definition file written from the headers).
set(weft_headers weft.h render_backend.h render_external_texture.h)
set(weft_backends weft::scene_d3d11 weft::scene_d3d12)
list(APPEND weft_headers render_backend_d3d11.h render_backend_d3d12.h)
if(TARGET weft::scene_vulkan)
    list(APPEND weft_backends weft::scene_vulkan)
    list(APPEND weft_headers render_backend_vulkan.h)
endif()
set(exports "")
foreach(header ${weft_headers})
    file(READ "${WEFT_SOURCE_DIR}/include/weft/${header}" text)
    string(REGEX MATCHALL "[ *]weft_[a-z0-9_]+\\(" names "${text}")
    foreach(name ${names})
        string(REGEX REPLACE "^[ *]|\\($" "" name "${name}")
        list(APPEND exports "${name}")
    endforeach()
endforeach()
list(APPEND exports weft_unreal_build)
list(REMOVE_DUPLICATES exports)
list(SORT exports)
list(JOIN exports "\n    " export_lines)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/weft.def" "LIBRARY weft\nEXPORTS\n    ${export_lines}\n")

add_library(weft_unreal SHARED weft_unreal.cpp "${CMAKE_CURRENT_BINARY_DIR}/weft.def")
set_target_properties(weft_unreal PROPERTIES OUTPUT_NAME weft)
target_compile_features(weft_unreal PRIVATE cxx_std_20)
target_compile_definitions(weft_unreal PRIVATE WEFT_UNREAL_COMMIT="${weft_commit}")
target_link_libraries(weft_unreal PRIVATE weft::core weft::view ${weft_backends})
if(MSVC)
    target_link_options(weft_unreal PRIVATE /DEBUG /OPT:REF /OPT:ICF)
endif()
