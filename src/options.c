/*
 * options.c
 *
 * Copyright (c) 2025, Jeroen Koekkoek
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include "options.h"
#include "regex.h"
#include "macros.h"

typedef struct option option_t;
struct option {
  int32_t code;
  /** Pattern to match identifiers. */
  /** Useful to support options that embed sequence numbers (or similar). Use
      extended regular expressions (available on all Unices, use compat on
      Windows) */
  const char *pattern;
  /** Regular expression used to match identifier */
  regex_t regex;
  option_t *options;
  // Custom quote character(?) Might be useful for regular expressions.
  //char quote;
  // Parse function to use, e.g. parse_integer or custom parser.
  //parse_t parse;
};

typedef struct position position_t;
struct position {
  size_t line;
  size_t column;
};

typedef struct location location_t;
struct location {
  position_t begin, end;
};

typedef struct token token_t;
struct token {
  /** Type of token. e.g., SPACE, IDENTIFIER, etc. */
  int32_t code;
  location_t location;
  const char *data;
  size_t length;
};

// scope contains info specific to current scope.
typedef struct scope scope_t;
struct scope {
  /** Parent scope (section or file) */
  const scope_t *scope;
  /** Indent of current scope */
  const token_t *indent;
  const option_t *option;
  char escaped;
};

typedef struct file file_t;
struct file {
  file_t *includer;
  /** Filename in "include:" directive. */
  char *file;
  /** Absolute path. */
  char *path;
  FILE *handle;

  position_t position;

  /** Token stack. */
  struct {
    size_t bottom, top, size;
    token_t *tokens;
  } stack;

  // x. Register latest indent (indent for next option) to make finding
  //    appropriate scope more convenient.
  // x. Register start of line (to filter scope or otherwise blank space).
  // x. Keep state (per scope) to deny literals after suboptions etc.

  struct {
    size_t offset, length, size;
    union { const char *read; char *write; } data;
  } buffer;

};

typedef struct parser parser_t;
struct parser {
  file_t first, *file;
  const option_t *options;
};

/** End-of-file */
#define END_OF_FILE (0)
/** Whitespace characters (' ', '\t', '\r') */
#define SPACE (1)
/** Line feed character ('\n') */
#define LINE_FEED (2)
/** Comment line ('#') */
#define COMMENT (3)
/** Characters that may appear in identifiers (first must be alphabetic) */
#define IDENTIFIER (4)

#define SECTION (IDENTIFIER | 1)
#define OPTION (IDENTIFIER | 2)
#define SUBOPTION (IDENTIFIER | 3)

#define LITERAL (8)
#define QUOTED_LITERAL (LITERAL | 1)

static const int8_t table[256] = {
  -1, -1, -1, -1, -1, -1, -1, -1,    // 0x00 - 0x07
  // tab (0x09), line feed (0x10), carriage return (0x0b)
  -1,  1,  2, -1, -1,  1, -1, -1,    // 0x08 - 0x0f
  -1, -1, -1, -1, -1, -1, -1, -1,    // 0x10 - 0x17
  -1, -1, -1, -1, -1, -1, -1, -1,    // 0x10 - 0x1f
  // space (0x20)
   1,  8,  8,  3,  8,  8,  8,  8,    // 0x20 - 0x27
   8,  8,  8,  8,  8,  8,  8,  8,    // 0x20 - 0x2f
   4,  4,  4,  4,  4,  4,  4,  4,    // 0x30 - 0x37
   4,  4,  8,  8,  8,  8,  8,  8,    // 0x38 - 0x3f
   8,  4,  4,  4,  4,  4,  4,  4,    // 0x40 - 0x47
   4,  4,  4,  4,  4,  4,  4,  4,    // 0x48 - 0x4f
   4,  4,  4,  4,  4,  4,  4,  4,    // 0x50 - 0x57
   4,  4,  4,  8,  8,  8,  8,  8,    // 0x58 - 0x5f
   8,  4,  4,  4,  4,  4,  4,  4,    // 0x60 - 0x67
   4,  4,  4,  4,  4,  4,  4,  4,    // 0x68 - 0x6f
   4,  4,  4,  4,  4,  4,  4,  4,    // 0x70 - 0x77
   4,  4,  4,  8,  8,  8,  8, -1,    // 0x78 - 0x7f
   8,  8,  8,  8,  8,  8,  8,  8,    // 0x80 - 0x87
   8,  8,  8,  8,  8,  8,  8,  8,    // 0x88 - 0x8f
   8,  8,  8,  8,  8,  8,  8,  8,    // 0x90 - 0x97
   8,  8,  8,  8,  8,  8,  8,  8,    // 0x98 - 0x9f
   8,  8,  8,  8,  8,  8,  8,  8,    // 0xa0 - 0xa7
   8,  8,  8,  8,  8,  8,  8,  8,    // 0xa8 - 0xaf
   8,  8,  8,  8,  8,  8,  8,  8,    // 0xb0 - 0xb7
   8,  8,  8,  8,  8,  8,  8,  8,    // 0xb8 - 0xbf
   8,  8,  8,  8,  8,  8,  8,  8,    // 0xc0 - 0xc7
   8,  8,  8,  8,  8,  8,  8,  8,    // 0xc8 - 0xcf
   8,  8,  8,  8,  8,  8,  8,  8,    // 0xd0 - 0xd7
   8,  8,  8,  8,  8,  8,  8,  8,    // 0xd8 - 0xdf
   8,  8,  8,  8,  8,  8,  8,  8,    // 0xe0 - 0xe7
   8,  8,  8,  8,  8,  8,  8,  8,    // 0xe8 - 0xef
   8,  8,  8,  8,  8,  8,  8,  8,    // 0xf0 - 0xf7
   8,  8,  8,  8,  8,  8,  8,  8     // 0xf8 - 0xff
};

nodiscard nonnull_all
static always_inline int32_t have_char(const char *start, const char *end)
{
  return start < end ? table[(uint8_t)*start] : 0;
}

nodiscard nonnull_all
static always_inline const char *scan_space(
  maybe_unused const scope_t *scope, const char *start, const char *end)
{
  for (; have_char(start, end) == SPACE; start++) ;
  return start;
}

nodiscard nonnull_all
static always_inline const char *scan_comment(
  maybe_unused const scope_t *scope, const char *start, const char *end)
{
  for (; have_char(start, end) > 0 && *start != '\n'; start++) ;
  return start;
}

nodiscard nonnull_all
static always_inline const char *scan_identifier(
  maybe_unused const scope_t *scope, const char *start, const char *end)
{
  for (; have_char(start, end) == IDENTIFIER; start++) ;
  return start; // maybe ':' or '='
}

nodiscard nonnull_all
static always_inline const char *scan_literal(
  maybe_unused const scope_t *scope, const char *start, const char *end)
{
  for (; have_char(start, end) >= IDENTIFIER; start++) ;
  return start;
}

nodiscard nonnull_all
static always_inline const char *scan_quoted_literal(
  scope_t *scope, const char *start, const char *end)
{
  assert(start < end);
  start += scope->escaped != '\0';

  for (; have_char(start, end) > 0 && (scope->escaped || *start != '\"'); start++)
    scope->escaped = *start == '\\' && !scope->escaped;

  assert(start == end || *start == '\"' || scope->escaped);
  return start + (start < end);
}

#if 0
static bool is_option(
  const scope_t *scope, const char *start, const char *end)
{
  return false;
}

static bool is_suboption(
  const scope_t *scope, const char *start, const char *end)
{
  return false;
}
#endif

nonnull_all
static int32_t scan(
  parser_t *parser, scope_t *scope, token_t *token)
{
  const char *start, *end, *bound;

  start = end = parser->file->buffer.data.read + parser->file->buffer.offset;
  bound = parser->file->buffer.data.read + parser->file->buffer.length;

  int32_t code;
  if (end == bound) {
    // end-of-file, refill?
    code = END_OF_FILE;
    goto end_of_file; // temporary
  }

  if (*start == '\"')
    code = QUOTED_LITERAL;
  else if (*start == '-') // identifiers cannot start with '-'
    code = LITERAL;
  else if ((code = table[(uint8_t)*start]) < 0)
    return -1;

  end = start + 1;
  do {
    if (end == bound) {
      // end-of-file, refill?
      break;
    }

    switch (code) {
      case SPACE:
        end = scan_space(scope, end, bound);
        break;
      case COMMENT:
        end = scan_comment(scope, end, bound);
        break;
      case LINE_FEED:
        end++;
        break;
      case QUOTED_LITERAL:
        end = scan_quoted_literal(scope, end, bound);
        break;
      case IDENTIFIER:
        end = scan_identifier(scope, end, bound);
        assert(end <= bound);
        if (end == bound)
          break;
        // x. Check if identifier is option if end points to ':' or
        //    suboption if end points to '='.
        code = LITERAL;
        /* fall through */
      default:
        assert(code == LITERAL);
        end = scan_literal(scope, end, bound);
        break;
    }
  } while (end == bound);

tmp_eof_handler:

  assert(code >= 0);
  assert(start <= end);

  token->code = code;
  token->data = start;
  token->length = (uintptr_t)end - (uintptr_t)start;
  assert((token->code == END_OF_FILE) == (token->length == 0));
  parser->file->buffer.offset += token->length;

  return token->code;
}

int32_t parse_options(const char *str, size_t len)
{
  parser_t parser = { 0 };

  parser.first.buffer.data.read = str;
  parser.first.buffer.length = parser.first.buffer.size = len;
  parser.first.buffer.offset = 0;

  parser.first.position.line = 1;
  parser.first.position.column = 1;

  parser.file = &parser.first;

  int32_t code;
  token_t token = { 0 };
  scope_t scope = { 0 };
  while ((code = scan(&parser, &scope, &token)) > 0) {
    printf("token { code = %" PRIu32 ", '%.*s' }\n", token.code, token.length, token.data);
  }

  printf("code at end: %" PRIu32 "\n", code);

  return 0;
}

#if 0
nonnull_all
int32_t parse_options(
  const option_t *options, const char *name)
{
  int32_t code;
  parser_t parser = { 0 };

  memset(&parser, 0, sizeof(parser));
  if ((code = open_file(&parser, &parser.first, name)) < 0)
    return code;
  parser.file = &parser.first;
  parser.options = options;
  code = parse(&parser);

  for (file_t *file = parser.file; file; file = parser.file) {
    close_file(parser, file);
    if (file != &parser.first)
      free(file);
  }

  return code;
}
#endif
