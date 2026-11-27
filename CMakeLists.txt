cmake_minimum_required(VERSION 3.25)
project(ffxii_recomp LANGUAGES C CXX)

enable_testing()
set(SWARC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ref/SwARc" CACHE PATH "SwARc source directory")
set(FFXII_EXE "" CACHE FILEPATH "Owner-supplied FFXII_TZA.exe")
set(FFXII_SYMBOLS "${CMAKE_CURRENT_SOURCE_DIR}/analysis/maps" CACHE PATH "FFXII lifter symbol directory")
# Lifter output to build from (a second tree allows A/B builds of lifter options).
set(FFXII_GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generated/full" CACHE PATH "Generated lifted code directory")
set(CMAKE_LINK_DEPENDS_NO_SHARED ON)
add_subdirectory("${SWARC_DIR}" swarc)

# Host test for mod discovery and ordering (no game files needed).
add_executable(mod_index_test tests/mod_index_test.cpp src/mod_index.cpp)
add_test(NAME mod_index COMMAND mod_index_test)

# Game text formats and mod text merging (zlib reads the archive originals).
set(ZLIB_USE_STATIC_LIBS ON)
find_package(ZLIB REQUIRED)
set(FFXII_TEXT_SOURCES src/text_formats.cpp src/text_merge.cpp src/text_mods.cpp src/vbf_reader.cpp)
add_executable(text_formats_test tests/text_formats_test.cpp ${FFXII_TEXT_SOURCES} src/mod_index.cpp)
target_compile_features(text_formats_test PRIVATE cxx_std_20)
target_link_libraries(text_formats_test PRIVATE ZLIB::ZLIB)
add_test(NAME text_formats COMMAND text_formats_test)

# Sample native module (include/ffxii_mod.h): a mod DLL for tests.
if(WIN32)
    add_library(ffxii_sample_mod MODULE examples/native-sample/sample_mod.c)
    target_include_directories(ffxii_sample_mod PRIVATE include)
    set_target_properties(ffxii_sample_mod PROPERTIES PREFIX "" OUTPUT_NAME sample_mod)
    target_link_options(ffxii_sample_mod PRIVATE -static)
    # The same through the C++ conveniences (include/ffxii_mod.hpp).
    add_library(ffxii_axis_log_mod MODULE examples/native-cpp-sample/axis_log.cpp)
    target_include_directories(ffxii_axis_log_mod PRIVATE include)
    target_compile_features(ffxii_axis_log_mod PRIVATE cxx_std_20)
    set_target_properties(ffxii_axis_log_mod PROPERTIES PREFIX "" OUTPUT_NAME axis_log)
    target_link_options(ffxii_axis_log_mod PRIVATE -static)
endif()

# Luau (MIT) runs script mods (src/luau_mods.cpp): a local checkout in
# FFXII_LUAU_DIR, or the pinned release fetched at configure time.
set(FFXII_LUAU_DIR "" CACHE PATH "Luau source checkout (empty: fetch the pinned tag)")
set(LUAU_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_TESTS OFF CACHE BOOL "" FORCE)
if(FFXII_LUAU_DIR)
    add_subdirectory("${FFXII_LUAU_DIR}" luau EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(luau GIT_REPOSITORY https://github.com/luau-lang/luau.git GIT_TAG 0.745 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(luau)
endif()
add_library(ffxii_luau_mods STATIC src/luau_mods.cpp)
target_include_directories(ffxii_luau_mods PUBLIC include)
target_compile_features(ffxii_luau_mods PUBLIC cxx_std_20)
target_link_libraries(ffxii_luau_mods PUBLIC Luau.VM Luau.Compiler Luau.CodeGen)

# Wasmtime (Apache-2.0 WITH LLVM-exception) runs WebAssembly mods
# (src/wasm_mods.cpp): FFXII_WASMTIME_DIR is an unpacked release C API
# (include/, lib/libwasmtime.a). Empty: no WASM runtime, and declared
# modules are reported as not loaded.
set(FFXII_WASMTIME_DIR "" CACHE PATH "Wasmtime C API release folder (empty: build without WASM mods)")
if(FFXII_WASMTIME_DIR)
    add_library(wasmtime_c STATIC IMPORTED)
    set_target_properties(wasmtime_c PROPERTIES
        IMPORTED_LOCATION "${FFXII_WASMTIME_DIR}/lib/libwasmtime.a"
        INTERFACE_INCLUDE_DIRECTORIES "${FFXII_WASMTIME_DIR}/include"
        INTERFACE_COMPILE_DEFINITIONS "LIBWASM_STATIC")
    if(WIN32)
        set_property(TARGET wasmtime_c PROPERTY INTERFACE_LINK_LIBRARIES
            ws2_32 bcrypt advapi32 userenv ntdll ole32 oleaut32 shell32 synchronization)
    else()
        set_property(TARGET wasmtime_c PROPERTY INTERFACE_LINK_LIBRARIES pthread dl m)
    endif()
    add_library(ffxii_wasm_mods STATIC src/wasm_mods.cpp)
    target_include_directories(ffxii_wasm_mods PUBLIC include)
    target_compile_features(ffxii_wasm_mods PUBLIC cxx_std_20)
    target_link_libraries(ffxii_wasm_mods PUBLIC wasmtime_c)
    target_compile_definitions(ffxii_wasm_mods PUBLIC FFXII_HAVE_WASMTIME=1)
endif()

# Mod lab (src/mod_lab.h): the page, the runner and the lab mods in
# bin/modlab (examples/modlab: the same mod in C, C++ and Luau).
set(FFXII_LAB_SOURCES src/mod_lab.cpp src/mod_lab_server.cpp)
set(FFXII_LAB_DIR "${RECOMP_BUNDLE_DIR}/modlab")
add_executable(ffxii_modlab src/mod_lab_main.cpp ${FFXII_LAB_SOURCES} src/mod_index.cpp)
target_include_directories(ffxii_modlab PRIVATE include)
target_link_libraries(ffxii_modlab PRIVATE ffxii_luau_mods)
set_target_properties(ffxii_modlab PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${RECOMP_BUNDLE_DIR}")
if(WIN32)
    target_link_libraries(ffxii_modlab PRIVATE ws2_32 shell32)
    if(MINGW)
        target_link_options(ffxii_modlab PRIVATE -static)
    endif()
    add_library(ffxii_lab_c MODULE examples/modlab/lab-c/lab.c)
    add_library(ffxii_lab_cpp MODULE examples/modlab/lab-cpp/lab.cpp)
    target_compile_features(ffxii_lab_cpp PRIVATE cxx_std_20)
    foreach(t c cpp)
        target_include_directories(ffxii_lab_${t} PRIVATE include)
        target_link_options(ffxii_lab_${t} PRIVATE -static)
        set_target_properties(ffxii_lab_${t} PROPERTIES PREFIX "" OUTPUT_NAME lab_${t}
            LIBRARY_OUTPUT_DIRECTORY "${FFXII_LAB_DIR}/lab-${t}" RUNTIME_OUTPUT_DIRECTORY "${FFXII_LAB_DIR}/lab-${t}")
        add_dependencies(ffxii_modlab ffxii_lab_${t})
    endforeach()
endif()
add_custom_command(TARGET ffxii_modlab POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${FFXII_LAB_DIR}/lab-c" "${FFXII_LAB_DIR}/lab-cpp" "${FFXII_LAB_DIR}/lab-luau"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/examples/modlab/lab-c/mod.ini" "${FFXII_LAB_DIR}/lab-c/mod.ini"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/examples/modlab/lab-cpp/mod.ini" "${FFXII_LAB_DIR}/lab-cpp/mod.ini"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/examples/modlab/lab-luau/mod.ini" "${FFXII_LAB_DIR}/lab-luau/mod.ini"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/examples/modlab/lab-luau/lab.luau" "${FFXII_LAB_DIR}/lab-luau/lab.luau")
# The WebAssembly lab mod: lab.c built with clang's wasm32 target.
if(TARGET ffxii_wasm_mods AND CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_link_libraries(ffxii_modlab PRIVATE ffxii_wasm_mods)
    set(FFXII_LAB_WASM "${FFXII_LAB_DIR}/lab-wasm/lab.wasm")
    add_custom_command(OUTPUT "${FFXII_LAB_WASM}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${FFXII_LAB_DIR}/lab-wasm"
        COMMAND ${CMAKE_C_COMPILER} --target=wasm32 -O2 -nostdlib -Wl,--no-entry -Wl,--export-dynamic
            -I "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/examples/modlab/lab-wasm/lab.c" -o "${FFXII_LAB_WASM}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/examples/modlab/lab-wasm/mod.ini" "${FFXII_LAB_DIR}/lab-wasm/mod.ini"
        DEPENDS examples/modlab/lab-wasm/lab.c examples/modlab/lab-wasm/mod.ini include/ffxii_wasm.h examples/modlab/lab_targets.h)
    add_custom_target(ffxii_lab_wasm DEPENDS "${FFXII_LAB_WASM}")
    add_dependencies(ffxii_modlab ffxii_lab_wasm)
endif()

if(EXISTS "${FFXII_GENERATED_DIR}/table.cpp")
    recomp_add_lifted(ffxii_lifted "${FFXII_GENERATED_DIR}" src/mods.cpp src/fps_menu.cpp src/mod_index.cpp ${FFXII_TEXT_SOURCES} src/probe.cpp src/frame_rate.cpp src/watch.cpp src/cbscan.cpp src/direct_hooks.cpp)
    # MSYS2 clang 23's SLP vectorizer crashes (access violation) on many
    # lifted functions once XMM registers live in locals (2026-11-26:
    # f_007422F0, f_007AF740, f_007651F0 and others), so generated files
    # build without it.
    file(GLOB _ffxii_gen "${FFXII_GENERATED_DIR}/gen_*.cpp")
    set_source_files_properties(${_ffxii_gen} TARGET_DIRECTORY ffxii_lifted
        PROPERTIES COMPILE_OPTIONS "-fno-slp-vectorize")
    add_executable(ffxii_recomp src/main.cpp src/native_mods.cpp ${FFXII_LAB_SOURCES})
    add_dependencies(ffxii_recomp ffxii_modlab)
    target_include_directories(ffxii_recomp PRIVATE include)
    target_link_libraries(ffxii_recomp PRIVATE ffxii_lifted ZLIB::ZLIB recomp_win32 recomp_ucrt recomp_dinput8 recomp_xinput recomp_dbghelp recomp_steam recomp_dxgi recomp_d3d11 recomp_d3dcompiler ffxii_luau_mods)
    if(TARGET ffxii_wasm_mods)
        target_link_libraries(ffxii_recomp PRIVATE ffxii_wasm_mods)
    endif()
    if(WIN32)
        target_link_libraries(ffxii_recomp PRIVATE ws2_32 shell32)
    endif()
    recomp_bundle(ffxii_recomp)
    recomp_whole_archive(ffxii_recomp)
endif()

if(FFXII_EXE)
    recomp_add_lift_tests(
        EXE "${FFXII_EXE}"
        SYMBOLS "${FFXII_SYMBOLS}"
        MANIFEST "${FFXII_SYMBOLS}/lifter.manifest"
        X64
    )
endif()
