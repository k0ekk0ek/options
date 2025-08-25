/**
 * match.h -- DOS wildcard style expression matcher
 *
 * Copyright (c) 2024, Jeroen Koekkoek
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "macros.h"

nodiscard nonnull_all
static bool match_mask(const char *name, const char *mask)
{
  for (; *name && *mask; name++, mask++) {
    if (*mask == '*') {
      for (++mask; *name; name++)
        if (match_mask(name, mask))
          return true;
      return !*mask;
    } else if (*name != *mask && *mask != '?') {
      return false;
    }
  }

  for (; *mask == '*'; mask++) ;
  return !*name && !*mask;
}
