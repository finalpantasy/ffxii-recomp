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

if(EXISTS "${FFXII_GENERATED_DIR}/table.cpp")
    recomp_add_lifted(ffxii_lifted "${FFXII_GENERATED_DIR}" src/mods.cpp src/mod_index.cpp src/probe.cpp src/frame_rate.cpp src/watch.cpp src/cbscan.cpp src/direct_hooks.cpp)
    add_executable(ffxii_recomp src/main.cpp src/native_mods.cpp)
    target_include_directories(ffxii_recomp PRIVATE include)
    target_link_libraries(ffxii_recomp PRIVATE ffxii_lifted recomp_win32 recomp_ucrt recomp_dinput8 recomp_xinput recomp_dbghelp recomp_steam recomp_dxgi recomp_d3d11 recomp_d3dcompiler)
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
