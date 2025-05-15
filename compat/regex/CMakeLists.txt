#
# Copyright (c) 2025, Jeroen Koekkoek
#
# SPDX-License-Identifier: BSD-3-Clause
#
include(CheckIncludeFile)

check_include_file("regex.h" HAVE_REGEX_H)

if(NOT HAVE_REGEX_H)
  add_library(regex OBJECT)
  target_include_directories(regex PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_sources(regex PRIVATE regcomp.c regerror.c regexec.c regfree.c)
endif()
