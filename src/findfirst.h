/**
 * findfirst.h -- _findfirst-like directory search
 *
 * Copyright (c) 2025, Jeroen Koekkoek
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef FINDFIRST_H
#define FINDFIRST_H

// Porting glob to Windows is more complicated than implementing _findfirst
// for POSIX, which should provide a sufficiently sophisticated solution.

#include <stddef.h>
#include <stdint.h>

#include "errors.h"
#include "macros.h"

typedef struct find_handle find_handle_t;
struct find_handle; // purposely opaque

#define FIND_REGULAR (0x8u)
#define FIND_DIRECTORY (0x4u)
#define FIND_UNKNOWN (0x00u)

typedef struct find_data find_data_t;
struct find_data {
  /** File attributes of a file */
  uint32_t type;
  /** Null-terminated filename (valid until next call to find_first_file) */
  const char *name;
};

nodiscard nonnull_all
int32_t find_file(
  find_handle_t **handle,
  const char *filespec,
  find_data_t *file_info);

//  0 on end of search
//  1 if a file is found
// <0 on error
nodiscard nonnull_all
int32_t find_next_file(
  find_handle_t *handle,
  find_data_t *file_info);

void find_close(
  find_handle_t *handle);

#endif // FINDFIRST_H
