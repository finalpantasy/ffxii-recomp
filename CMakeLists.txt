cmake_minimum_required(VERSION 3.25)
project(ffxii_recomp LANGUAGES C CXX)

enable_testing()
set(SWARC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ref/SwARc" CACHE PATH "SwARc source directory")
set(FFXII_EXE "" CACHE FILEPATH "Owner-supplied FFXII_TZA.exe")
set(FFXII_SYMBOLS "${CMAKE_CURRENT_SOURCE_DIR}/analysis/maps" CACHE PATH "FFXII lifter symbol directory")
set(CMAKE_LINK_DEPENDS_NO_SHARED ON)
add_subdirectory("${SWARC_DIR}" swarc)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/full/table.cpp")
    recomp_add_lifted(ffxii_lifted "${CMAKE_CURRENT_SOURCE_DIR}/generated/full")
endif()

if(FFXII_EXE)
    recomp_add_lift_tests(
        EXE "${FFXII_EXE}"
        SYMBOLS "${FFXII_SYMBOLS}"
        MANIFEST "${FFXII_SYMBOLS}/lifter.manifest"
        X64
    )
endif()
