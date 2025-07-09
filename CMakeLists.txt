#
# Copyright (c) 2025, Jeroen Koekkoek
#
# SPDX-License-Identifier: BSD-3-Clause
#
cmake_minimum_required(VERSION 3.10)
project(options LANGUAGES C VERSION 0.1.0)

set(CMAKE_C_STANDARD 99)
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules)

if(CMAKE_VERSION VERSION_LESS 3.12)
  # GENERATE_EXPORT_HEADER requires a C++ compiler up to version 3.12
  enable_language(CXX)
endif()

add_subdirectory(compat/regex)

if(CMAKE_VERSION VERSION_LESS 3.24)
  # COMPILE_WARNING_AS_ERROR property was added in version 3.24. Mimic the
  # option in prior versions.
  option(CMAKE_COMPILE_WARNING_AS_ERROR "Treat compiler warnings as errors." OFF)
  if(CMAKE_COMPILE_WARNING_AS_ERROR)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU" OR CMAKE_C_COMPILER_ID MATCHES "Clang")
      add_compile_options(-Werror)
    elseif(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
      add_compile_options(/WX)
    endif()
  endif()
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU")
  add_compile_options(
    -Wall -Wextra -Wconversion -Wunused -Winfinite-recursion
    -Wmissing-prototypes -Wstrict-prototypes -Wshadow -Wformat=2
    -Wcast-align=strict -Wmissing-declarations -Wstrict-overflow=5)
  if(CMAKE_GENERATOR STREQUAL "Ninja")
    add_compile_options(-fdiagnostics-color=always)
  endif()
elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")
  add_compile_options(
    -Wall -Wextra -Wconversion -Wunused -Winfinite-recursion -Wassign-enum
    -Wcomma -Wdocumentation -Wmissing-prototypes -Wstrict-prototypes
    -Wconditional-uninitialized -Wshadow -Wformat=2 -Wcast-align=strict
    -Wmissing-declarations -Wstrict-overflow=5)
  if(CMAKE_GENERATOR STREQUAL "Ninja")
    add_compile_options(-Xclang -fcolor-diagnostics)
  endif()
elseif(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
  add_compile_options(/W3)
endif()
