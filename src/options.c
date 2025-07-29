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
#include <string.h>

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
  struct {
    size_t length;
    const char *data;
  } pattern;
  // Regular expression used to match identifier
  //regex_t regex;
  struct {
    size_t length;
    const option_t *data;
  } options;
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
  /** Type of token. e.g., SPACE, IDENTIFIER, etc */
  int32_t code;
  location_t location;
  /** Start of token (offset) relative to file buffer */
  size_t offset;
  /** Token length */
  size_t length;
  /** Pointer to option if code is section, option or suboption */
  const option_t *option;
};

typedef struct scope scope_t;
struct scope {
  /** Enclosing scope (section or file) */
  const scope_t *encloser;
  /** Indent of scope (token index) */
  uint32_t indent;
  /** Corresponding section or option */
  const option_t *option;
};

typedef struct file file_t;
struct file {
  file_t *includer;
  /** Filename in "include:" directive */
  char *file;
  /** Absolute path */
  char *path;
  FILE *handle;

  position_t position;

  struct {
    size_t offset, length, size;
    union { const char *read; char *write; } data;
  } buffer;

  /** Token stack. */
  // FIXME: allocate extra token and reserve first to indicate no-indentation
  struct {
    size_t bottom, top, size;
    token_t *data;
  } tokens;

  /** State (per scope) to deny literals after suboptions etc. */
  // FIXME: more states are likely required
  enum {
    SCAN,
    SCAN_LITERAL_OR_SUBOPTION,
    SCAN_SUBOPTION
  } state;

  // FIXME: scope must be maintained per file to associate file scope directly
  //        with a section. e.g.
  //        zone:
  //          include: zone.generic.conf

  // Register latest indent (indent for next (sub)option) to make finding
  // corresponding scope more convenient. Set by parser after newline.
  size_t indent;

  // Register start of line (to filter scope or otherwise blank space).
  //bool start_of_line;
  //bool escaped;
  //bool end_of_file;
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
  maybe_unused parser_t *parser, maybe_unused const scope_t *scope, const char *start, const char *end)
{
  for (; have_char(start, end) == SPACE; start++) ;
  return start;
}

nodiscard nonnull_all
static always_inline const char *scan_comment(
  maybe_unused parser_t *parser, maybe_unused const scope_t *scope, const char *start, const char *end)
{
  for (; have_char(start, end) > 0 && *start != '\n'; start++) ;
  return start;
}

nodiscard nonnull_all
static always_inline const char *scan_identifier(
  maybe_unused parser_t *parser, maybe_unused const scope_t *scope, const char *start, const char *end)
{
  for (; have_char(start, end) == IDENTIFIER; start++) ;
  return start; // maybe ':' or '='
}

nodiscard nonnull_all
static always_inline const char *scan_literal(
  maybe_unused parser_t *parser, maybe_unused const scope_t *scope, const char *start, const char *end)
{
  for (; have_char(start, end) >= IDENTIFIER; start++) ;
  return start;
}

nodiscard nonnull_all
static always_inline const char *scan_quoted_literal(
  parser_t *parser, maybe_unused const scope_t *scope, const char *start, const char *end)
{
  assert(start < end);
  start += parser->file->escaped != '\0';

  for (; have_char(start, end) > 0 && (parser->file->escaped || *start != '\"'); start++)
    parser->file->escaped = *start == '\\' && !parser->file->escaped;

  assert(start == end || *start == '\"' || parser->file->escaped);
  return start + (start < end);
}

nodiscard nonnull((1,2))
static always_inline bool matches(
  const option_t *option, const char *identifier, size_t length)
{
  return option->pattern.length == length &&
         memcmp(option->pattern.data, identifier, length) == 0;
}

nodiscard nonnull((1,2))
static const option_t *has_option(
  const option_t *option, const char *identifier, size_t length)
{
  // options contain suboptions, sections contain options and/or sections
  if (option->code != SECTION)
    return NULL;
  for (size_t i = 0; i < option->options.length; i++)
    if (matches(&option->options.data[i], identifier, length))
      return &option->options.data[i];
  return NULL;
}

nodiscard nonnull((1,2,3))
static const option_t *is_option(
  const parser_t *parser, const scope_t *scope,
  const char *identifier, size_t length)
{
  if (parser->file->state != SCAN)
    return NULL;

  const char *data = parser->file->buffer.data.read;
  const token_t *inner = &parser->file->tokens.data[parser->file->indent];
  const token_t *outer;

  // scope indentation is determined by first section/option
  if (scope->encloser && !scope->indent) {
    outer = &parser->file->tokens.data[scope->encloser->indent];
    assert(outer->code == SPACE);
    if (outer->length < inner->length)
      return !memcmp(data+outer->length, data+inner->length, outer->length)
        ? has_option(scope->option, identifier, length) : NULL;
    scope = scope->encloser;
  }

  for (; scope->encloser; scope = scope->encloser) {
    assert(scope->indent);
    outer = &parser->file->tokens.data[scope->indent];
    assert(outer->code == SPACE);
    if (outer->length < inner->length)
      return !memcmp(data+outer->length, data+inner->length, outer->length)
        ? has_option(scope->option, identifier, length) : NULL;
  }

  assert(scope);
  assert(!scope->encloser);
  assert(!scope->indent);

  outer = &parser->file->tokens.data[scope->indent];
  assert(!outer->length);
  return outer->length == 0 && inner->length == 0
    ? has_option(scope->option, identifier, length) : NULL;
}

nodiscard nonnull((1,2))
static always_inline const option_t *has_suboption(
  const option_t *option, const char *identifier, size_t length)
{
  if (option->code != OPTION)
    return NULL;
  for (size_t i = 0; i < option->options.length; i++)
    if (matches(&option->options.data[i], identifier, length))
      return &option->options.data[i];
  return NULL;
}

nodiscard nonnull((1,2,3))
static const option_t *is_suboption(
  const parser_t *parser, const scope_t *scope,
  const char *identifier, size_t length)
{
  if (parser->file->state != SCAN_LITERAL_OR_SUBOPTION &&
      parser->file->state != SCAN_SUBOPTION)
    return NULL;

  // FIXME: incorrect, must be properly enclosed if on different line
  return has_suboption(scope->option, identifier, length);
}

nonnull_all
static int32_t scan(
  parser_t *parser, scope_t *scope, token_t *token)
{
  const char *start, *end, *bound;
  const option_t *option = NULL;

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
        end = scan_space(parser, scope, end, bound);
        break;
      case COMMENT:
        end = scan_comment(parser, scope, end, bound);
        break;
      case LINE_FEED:
        // FIXE: do not loop if end == bound
        end++;
        break;
      case QUOTED_LITERAL:
        end = scan_quoted_literal(parser, scope, end, bound);
        break;
      case IDENTIFIER:
        end = scan_identifier(parser, scope, end, bound);
        assert(end <= bound);
        if (end == bound) {
          // FIXME: actually conditional
          code = LITERAL;
          break;
        }
        if (*end == ':' && (option = is_option(parser, scope, start, (size_t)(end - start)))) {
          end++;
          break;
        }
        if (*end == '=' && (option = is_suboption(parser, scope, start, (size_t)(end - start)))) {
          end++;
          break;
        }
        code = LITERAL;
        // fall through
      default:
        assert(code == LITERAL);
        end = scan_literal(parser, scope, end, bound);
        break;
    }
  } while (end == bound);

end_of_file:

  assert(code >= 0);
  assert(start <= end);

  token->code = code;
  token->offset = (size_t)(start - parser->file->buffer.data.read);
  token->length = (uintptr_t)end - (uintptr_t)start;
  token->option = option;
  assert((token->code == END_OF_FILE) == (token->length == 0));
  parser->file->buffer.offset += token->length;

  return token->code;
}

#define X_SUBOPTION(pattern_) \
  { SUBOPTION, { sizeof(pattern_) - 1, pattern_ }, 0, { 0, NULL } }

#define NO_SUBOPTIONS() { 0, NULL }
#define SUBOPTIONS(suboptions_) { sizeof(suboptions_)/sizeof(suboptions_[0]), suboptions_ }

// FIXME: introduce specialized options like INTEGER, STRING, etc?
#define X_OPTION(pattern_, suboptions_) \
  { OPTION, { sizeof(pattern_) - 1, pattern_ }, suboptions_ }

#define NO_OPTIONS() { 0, NULL }
#define OPTIONS(options_) { sizeof(options_)/sizeof(options_[0]), options_ }

#define X_SECTION(pattern_, options_) \
  { SECTION, { sizeof(pattern_) - 1, pattern_ }, options_ }

static const option_t options[] = {
  X_OPTION("foo", NO_SUBOPTIONS()),
  X_OPTION("bar", NO_SUBOPTIONS())
};

static const option_t section[] = {
  X_SECTION("not-a-pattern", OPTIONS(options))
};

int32_t parse_options(const char *str, size_t len)
{
  parser_t parser = { 0 };
  token_t indent = { SPACE, { { 1, 1 }, { 1, 1 } }, 0, 0, NULL };

  parser.first.buffer.data.read = str;
  parser.first.buffer.length = parser.first.buffer.size = len;
  parser.first.buffer.offset = 0;

  parser.first.position.line = 1;
  parser.first.position.column = 1;

  parser.first.tokens.bottom = 0;
  parser.first.tokens.top = 1;
  parser.first.tokens.size = 1;
  parser.first.tokens.data = &indent;

  parser.file = &parser.first;

  int32_t code;
  token_t token = { 0 };
  scope_t scope = { NULL, 0, section };

  // FIXME: parser must check for correct indentation
  while ((code = scan(&parser, &scope, &token)) > 0) {
    const int length = (int)token.length;
    const char *data = &parser->file->buffer.data.read[token.offset];
    printf("token { code = %" PRIu32 ", '%.*s' }", token.code, length, data);
    if (token.code & IDENTIFIER)
      printf(" (matched option '%s')\n", token.option->pattern.data);
    else
      printf("\n");
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
