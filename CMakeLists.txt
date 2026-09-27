cmake_minimum_required(VERSION 3.25)
if(POLICY CMP0184)
  cmake_policy(SET CMP0184 NEW)
endif()
project(anime_vault VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
option(WARNINGS_AS_ERRORS "Treat project warnings as errors" OFF)
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(ENABLE_LTO "Enable link-time optimization" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Sanitizers)

add_subdirectory(backend)
