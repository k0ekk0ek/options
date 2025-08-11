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


typedef struct position position_t;
struct position {
  size_t line, column;
};

typedef struct location location_t;
struct location {
  const char *file;
  position_t begin, end;
};

typedef struct lexeme lexeme_t;
struct lexeme {
  location_t location;
  struct {
    size_t length;
    const char *data;
  } string;
};

typedef struct option option_t;
struct option;

typedef int32_t(*enter_t)(
  const option_t *, const lexeme_t *, void *);
typedef int32_t(*exit_t)(
  const option_t *, const lexeme_t *, void *);
typedef int32_t(*accept_t)(
  const option_t *, const lexeme_t *, void *);

typedef struct option_tuple option_tuple_t;
struct option_tuple {
  size_t size;
  const option_t *data;
};

struct option {
  /** Type of option */
  int32_t code;
  /** Pattern to match identifiers */
  struct {
    size_t length;
    const char *data;
  } pattern;
  option_tuple_t options;
  // multiplicity?
  // default value?
  // offset and/or relative offset?
  /** Callback invoked when scope is entered */
  enter_t enter;
  /** Callback invoked when scope is exited */
  exit_t exit;
  /** Callback invoked to accept value */
  accept_t accept;
};

#define SUBOPTION(pattern_) \
  { 6, }

#define NO_SUBOPTIONS \
  (const option_tuple_t){ 0, NULL }
#define SUBOPTIONS(suboptions_) \
  (const option_tuple_t){ sizeof(suboptions_)/sizeof(suboptions_[0]), suboptions_ }


#define OPTION(pattern_, suboptions_) \
  { 4, { sizeof(pattern_) - 1, pattern_ }, suboptions_, 0, 0, 0 }

#define NO_OPTIONS \
  (const option_tuple_t){ 0, NULL }
#define OPTIONS(options_) \
  (const option_tuple_t){ sizeof(options_)/sizeof(options_[0]), options_ }


#define SECTION(pattern_, options_) \
  { 5, { sizeof(pattern_) - 1, pattern_ }, options_, 0, 0, 0 }

//
// section:
//   option: value  suboption=value other-suboption=value
//
// section:
//   option: value
//

// error codes
#define SYNTAX_ERROR (-1)
#define OUT_OF_MEMORY (-2)

int32_t parse_options_string(
  const option_tuple_t *options,
  const char *string,
  size_t length,
  void *user_data);

#endif // OPTIONS_H
