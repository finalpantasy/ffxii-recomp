cmake_minimum_required(VERSION 3.25)
project(ffxii_recomp LANGUAGES C CXX)

enable_testing()
set(SWARC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ref/SwARc" CACHE PATH "SwARc source directory")
set(FFXII_EXE "" CACHE FILEPATH "Owner-supplied FFXII_TZA.exe")
set(FFXII_SYMBOLS "${CMAKE_CURRENT_SOURCE_DIR}/analysis/maps" CACHE PATH "FFXII lifter symbol directory")
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
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/full/table.cpp")
    recomp_add_lifted(ffxii_lifted "${CMAKE_CURRENT_SOURCE_DIR}/generated/full" src/mods.cpp src/mod_index.cpp src/probe.cpp src/frame_rate.cpp src/watch.cpp src/cbscan.cpp)
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
