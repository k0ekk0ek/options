/*
 * parse.c
 *
 * Copyright (c) 2025, Jeroen Koekkoek
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#include <stdint.h>
#include <stdio.h>

#include "options.h"

static const option_t options[] = {
  OPTION("foo", NO_SUBOPTIONS),
  OPTION("bar", NO_SUBOPTIONS)
};

static const option_t section[] = {
  SECTION("baz", OPTIONS(options))
};

int main(void)
{
  const char str[] =
  "baz:\n"
  "  foo: \"foo bar\"\n"
  "  bar: baz";
  int32_t code = parse_options(
    section, 1, str, sizeof(str) - 1, NULL);
  printf("return code: %d\n", code);
  return 0;
}
