/*
 * options.h -- simple configuration parser
 *
 * Copyright (c) 2024, Jeroen Koekkoek
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#ifndef OPTIONS_H
#define OPTIONS_H

#include <stddef.h>
#include <stdint.h>

//
// section:
//   option: value  suboption=value other-suboption=value
//
// section:
//   option: value
//

int32_t parse_options(const char *str, size_t len);

#endif // OPTIONS_H
