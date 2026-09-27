cmake_minimum_required(VERSION 3.25)
project(anime_vault_utf8_policy LANGUAGES CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../CompilerWarnings.cmake")
add_library(utf8_policy_probe STATIC probe.cpp)
anime_vault_apply_warnings(utf8_policy_probe)

if(MSVC)
  get_target_property(probe_options utf8_policy_probe COMPILE_OPTIONS)
  if(NOT "/utf-8" IN_LIST probe_options)
    message(FATAL_ERROR "MSVC project targets must compile UTF-8 sources with /utf-8")
  endif()
endif()
