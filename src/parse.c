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

int main(void)
{
  const char str[] = "foo \"foo bar\" bar: baz";
  int32_t code = parse_options(str, sizeof(str) - 1);
  printf("code(main): %d\n", code);
  return 0;
}
