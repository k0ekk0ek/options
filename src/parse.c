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

int main(void)
{
  const char str[] = "foo: \"foo bar\"\nbar: baz";
  int32_t code = parse_options_string(
    &OPTIONS(options), str, sizeof(str) - 1, NULL);
  printf("return code: %d\n", code);
  return 0;
}
