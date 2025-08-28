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
#include <stdarg.h>
#include <errno.h>

#include "options.h"

#undef SECTION
#undef OPTION
#undef SUBOPTION

#include "regex.h"
#include "macros.h"
#include "findfirst.h"

typedef struct token token_t;
struct token {
  /** Type of token. e.g., SPACE, IDENTIFIER, etc */
  int32_t code;
  /** Location of token */
  location_t location;
  /** Start of token (offset) relative to file buffer */
  size_t first;
  /** Size of token */
  size_t size;
  /** Pointer to option if code is section, option or suboption */
  const option_t *option;
};

typedef struct scope scope_t;
struct scope {
  /** Enclosing scope (section or file) */
  const scope_t *encloser;
  /** Indent of scope */
  size_t indent;
  /** Identifier for scope */
  size_t identifier;
  /** Pointer to associated option */
  const option_t *option;
};

static char not_a_file[] = "<string>";

typedef struct file file_t;
struct file {
  file_t *includer;
  /** Filename in "include:" directive */
  char *name;
  /** Absolute path */
  char *path;
  /** File handle */
  FILE *handle;

  location_t location;

  struct {
    size_t first, size, capacity;
    char *data;
  } buffer;

  /** Token stack (first reserved for "no-indentation") */
  struct {
    size_t last, size, capacity;
    token_t *data;
  } tokens;

  /** Latest file indentation */
  size_t indent;
};

typedef struct parser parser_t;
struct parser {
  file_t first, *file;
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
#define OPTION (4)
#define SECTION (OPTION | 1)
#define SUBOPTION (OPTION | 2)
#define INCLUDE (OPTION | 3)
#define VALUE (8)
#define QUOTED_VALUE (VALUE | 1)


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


static const option_t include =
  { INCLUDE, { 7, "include" }, { 0, NULL }, 0, 0, 0 };

nodiscard nonnull_all
static int32_t syntax_error(
  maybe_unused const parser_t *parser, maybe_unused const scope_t *scope,
  const location_t *location, const char *format, ...)
{
  va_list ap;
  va_start(ap, format);
  fprintf(stderr, "%s:%zu:%zu: ", location->file, location->line, location->column);
  vfprintf(stderr, format, ap);
  fprintf(stderr, "\n");
  va_end(ap);
  return SYNTAX_ERROR;
}

nodiscard nonnull_all
static int32_t semantic_error(
  maybe_unused const parser_t *parser, maybe_unused const scope_t *scope,
  const location_t *location, const char *format, ...)
{
  va_list ap;
  va_start(ap, format);
  fprintf(stderr, "%s:%zu:%zu: ", location->file, location->line, location->column);
  vfprintf(stderr, format, ap);
  fprintf(stderr, "\n");
  va_end(ap);
  return SEMANTIC_ERROR;
}

nodiscard nonnull((1))
static always_inline int32_t have_char(const parser_t *parser, size_t first)
{
  return first < parser->file->buffer.size
    ? table[(uint8_t)parser->file->buffer.data[first]] : 0;
}

nodiscard nonnull((1))
static always_inline size_t scan_space(const parser_t *parser, size_t first)
{
  for (; have_char(parser, first) == SPACE; first++) ;
  return first;
}

nodiscard nonnull_all
static always_inline size_t scan_comment(const parser_t *parser, size_t first)
{
  for (; have_char(parser, first) > 0 &&
         parser->file->buffer.data[first] != '\n'; first++)
    ;
  return first;
}

nodiscard nonnull_all
static always_inline size_t scan_identifier(const parser_t *parser, size_t first)
{
  for (; have_char(parser, first) == OPTION; first++) ;
  return first; // maybe ':' or '='
}

nodiscard nonnull_all
static always_inline size_t scan_value(const parser_t *parser, size_t first)
{
  for (; have_char(parser, first) >= OPTION &&
         parser->file->buffer.data[first] != '\"'; first++)
    ;
  return first;
}

nodiscard nonnull((1,2))
static bool is_indent(
  const parser_t *parser, const scope_t *scope, size_t indent)
{
  const token_t *encloser = &parser->file->tokens.data[scope->indent];
  const token_t *enclosed = &parser->file->tokens.data[indent];
  const char *data = parser->file->buffer.data;

  size_t size = encloser->size;
  if (encloser->size < enclosed->size)
    size = enclosed->size;
  return memcmp(data+encloser->first, data+enclosed->first, size) == 0;
}

nodiscard nonnull((1,2))
static int32_t in_scope(
  const parser_t *parser, const scope_t *scope, size_t indent)
{
  const token_t *encloser = &parser->file->tokens.data[scope->indent];
  const token_t *enclosed = &parser->file->tokens.data[indent];

  if (encloser->size > enclosed->size)
    return  1;
  if (encloser->size < enclosed->size)
    return -1;
  return 0;
}

nodiscard nonnull((1,2))
static always_inline bool matches(
  const option_t *option, const char *name, size_t size)
{
  return option->pattern.size == size &&
         memcmp(option->pattern.data, name, size) == 0;
}

nodiscard nonnull((1,2,4))
static const option_t *is_include(
  maybe_unused const parser_t *parser, maybe_unused const scope_t *scope,
  int32_t state, const char *name, size_t size)
{
  if (!(state & (1 << OPTION)))
    return NULL;
  if (!matches(&include, name, size))
    return NULL;
  return &include;
}

nodiscard nonnull((1,2))
static const option_t *has_option(
  const option_t *option, const char *name, size_t size)
{
  // options contain suboptions, sections contain options and/or sections
  if (option->code != SECTION)
    return NULL;
  for (size_t i = 0; i < option->options.size; i++)
    if (matches(&option->options.data[i], name, size))
      return &option->options.data[i];
  return NULL;
}

nodiscard nonnull((1,2,4))
static const option_t *is_option(
  const parser_t *parser, const scope_t *scope,
  int32_t state, const char *name, size_t size)
{
  if (!(state & (1 << OPTION)))
    return NULL;

  const char *data = parser->file->buffer.data;
  const token_t *inner = &parser->file->tokens.data[parser->file->indent];
  const token_t *outer;

  // scope indentation is determined by first section/option
  if (scope->encloser && !scope->indent) {
    outer = &parser->file->tokens.data[scope->encloser->indent];
    assert(outer->code == SPACE);
    if (outer->size < inner->size)
      return !memcmp(data+outer->size, data+inner->size, outer->size)
        ? has_option(scope->option, name, size) : NULL;
    scope = scope->encloser;
  }

  for (; scope->encloser; scope = scope->encloser) {
    assert(scope->indent);
    outer = &parser->file->tokens.data[scope->indent];
    assert(outer->code == SPACE);
    if (outer->size <= inner->size)
      return !memcmp(data+outer->size, data+inner->size, outer->size)
        ? has_option(scope->option, name, size) : NULL;
  }

  assert(scope);
  assert(!scope->encloser);
  assert(!scope->indent);

  outer = &parser->file->tokens.data[scope->indent];
  assert(!outer->size);
  return outer->size == 0 && inner->size == 0
    ? has_option(scope->option, name, size) : NULL;
}

nodiscard nonnull((1,2))
static always_inline const option_t *has_suboption(
  const option_t *option, const char *name, size_t size)
{
  if (option->code != OPTION)
    return NULL;
  for (size_t i = 0; i < option->options.size; i++)
    if (matches(&option->options.data[i], name, size))
      return &option->options.data[i];
  return NULL;
}

nodiscard nonnull((1,2,4))
static const option_t *is_suboption(
  const parser_t *parser, const scope_t *scope, int32_t state,
  const char *name, size_t size)
{
  (void)parser;
  if (!(state & (1 << SUBOPTION)))
    return NULL;

  // FIXME: incorrect, must be properly enclosed if on different line
  return has_suboption(scope->option, name, size);
}

nodiscard nonnull_all
static int32_t empty(const parser_t *parser)
{
  return !parser->file->handle || feof(parser->file->handle) != 0;
}

nodiscard nonnull_all
static int32_t refill(parser_t *parser, size_t *first, size_t *last)
{
  // implement
  (void)parser;
  (void)first;
  (void)last;
  return 0;
}

nodiscard nonnull((1))
static int32_t tokenize(
  parser_t *parser, int32_t code, size_t first, size_t last,
  const option_t *option)
{
  assert(last >= first);
  const size_t size = last - first;
  assert((code == END_OF_FILE) == (size == 0));
  assert(parser->file->tokens.size < parser->file->tokens.capacity);
  token_t *token = &parser->file->tokens.data[ parser->file->tokens.size++ ];
  token->code = code;
  token->first = first;
  token->size = size;
  token->option = option;
  parser->file->buffer.first += size;
  token->location = parser->file->location;

  if (code == LINE_FEED) {
    assert(size == 1);
    parser->file->location.line += 1;
    parser->file->location.column = 1;
  } else {
    parser->file->location.column += size;
  }

  return token->code;
}

nodiscard nonnull_all
static int32_t scan_quoted_value(
  parser_t *parser, const scope_t *scope)
{
  size_t first, last;

  first = last = parser->file->buffer.first;
  assert(parser->file->buffer.size >= last);
  assert(parser->file->buffer.data[last] == '\"');

  last++;
  for (bool escaped = false;;) {
    if (last == parser->file->buffer.size) {
      int32_t code;
      if ((code = refill(parser, &first, &last)) < 0)
        return code;
      if (last == parser->file->buffer.size)
        return syntax_error(parser, scope, &parser->file->location,
          "unterminated quoted value");
    } else {
      if (parser->file->buffer.data[last] == '\n')
        return syntax_error(parser, scope, &parser->file->location,
          "line feed in quoted value");
      if (parser->file->buffer.data[last] == '\"' && !escaped)
        break;
      escaped = parser->file->buffer.data[last] == '\\' && !escaped;
      last++;
    }
  }

  assert(parser->file->buffer.size >= last);
  assert(parser->file->buffer.data[last] == '\"');

  return tokenize(parser, QUOTED_VALUE, first, last+1, NULL);
}

nodiscard nonnull_all
static int32_t scan(
  parser_t *parser, const scope_t *scope, const int32_t state)
{
  int32_t type;
  size_t first, last;

  first = last = parser->file->buffer.first;

  if (last == parser->file->buffer.size) {
    int32_t code;
    if ((code = refill(parser, &first, &last)) < 0)
      return code;
    if (last == parser->file->buffer.size)
      return tokenize(parser, END_OF_FILE, first, last, NULL);
  }

  assert(parser->file->buffer.size >= last);

  if (parser->file->buffer.data[last] == '\"')
    return scan_quoted_value(parser, scope);
  if ((type = table[(uint8_t)parser->file->buffer.data[last]]) < 0)
    return syntax_error(parser, scope, &parser->file->location,
      "invalid character");

  last++;
  do {
    if (last == parser->file->buffer.size) {
      int32_t code;
      if ((code = refill(parser, &first, &last)) < 0)
        return code;
      if (last == parser->file->buffer.size)
        break;
    }

    if (type == SPACE) {
      last = scan_space(parser, last);
    } else if (type == COMMENT) {
      last = scan_comment(parser, last);
    } else if (type == LINE_FEED) {
      break;
    } else if (type == OPTION) {
      last = scan_identifier(parser, last);
      if (last == parser->file->buffer.size && !empty(parser))
        continue;
      if (last == parser->file->buffer.size)
        return tokenize(parser, VALUE, first, last, NULL);

      const char *end = parser->file->buffer.data + last;
      const char *name = parser->file->buffer.data + first;
      const size_t size = (size_t)(last - first);
      const option_t *option;
      if (*end == ':' && (option = is_include(parser, scope, state, name, size)))
        return tokenize(parser, option->code, first, last+1, option);
      if (*end == ':' && (option = is_option(parser, scope, state, name, size)))
        return tokenize(parser, option->code, first, last+1, option);
      if (*end == '=' && (option = is_suboption(parser, scope, state, name, size)))
        return tokenize(parser, option->code, first, last+1, option);

      type = VALUE;
      last = scan_value(parser, last);
    } else {
      assert(type == VALUE);
      last = scan_value(parser, last);
    }
  } while (last == parser->file->buffer.size);

  assert(type >= 0);

  return tokenize(parser, type, first, last, NULL);
}

nodiscard nonnull_all
static never_inline int32_t shift(
  parser_t *parser, const scope_t *scope, const int32_t state, size_t *token)
{
  assert(parser->file->tokens.last > 0);
  assert(parser->file->tokens.size > 0);

  if (parser->file->tokens.last == parser->file->tokens.size) {
    if (parser->file->tokens.size == parser->file->tokens.capacity) {
      if (SIZE_MAX - parser->file->tokens.capacity < 64)
        return OUT_OF_MEMORY;
      const size_t capacity = parser->file->tokens.capacity + 64;
      token_t *data = parser->file->tokens.data;
      if (!(data = reallocarray(data, capacity, sizeof(*data))))
        return OUT_OF_MEMORY;
      parser->file->tokens.capacity = capacity;
      parser->file->tokens.data = data;
    }

    int32_t code;
    if ((code = scan(parser, scope, state)) < 0)
      return code;
  }

  assert(parser->file->tokens.last < parser->file->tokens.size);
  return parser->file->tokens.data[(*token = parser->file->tokens.last++)].code;
}

nonnull_all
static never_inline void unshift(parser_t *parser)
{
  assert(parser->file->tokens.last > 1);
  assert(parser->file->tokens.last <= parser->file->tokens.size);
  parser->file->tokens.last--;
}

nonnull((1))
static never_inline void reduce(parser_t *parser, size_t token)
{
  assert(parser->file->tokens.last > 1);
  assert(parser->file->tokens.last <= parser->file->tokens.size);
  assert(token > 0 && token < parser->file->tokens.last);

  // retain in-use and unshifted tokens
  const size_t move = parser->file->tokens.size - token;
  memmove(
    parser->file->tokens.data + (token),
    parser->file->tokens.data + (token + 1),
    move * sizeof(*parser->file->tokens.data));

  assert(parser->file->indent != token);
  if (parser->file->indent > token)
    parser->file->indent--;
  parser->file->tokens.last--;
  parser->file->tokens.size--;
}

nodiscard nonnull_all
static int32_t enter_scope(const parser_t *parser, const scope_t *scope)
{
  assert(scope->option);
  if (!scope->option->enter)
    return 0;
  assert(scope->identifier < parser->file->tokens.last);
  const token_t *token = &parser->file->tokens.data[scope->identifier];
  assert(token->code & OPTION);
  assert(token->first < parser->file->buffer.size);
  const char *data = &parser->file->buffer.data[token->first];
  const lexeme_t lexeme = { token->location, { token->size, data } };
  return scope->option->enter(scope->option, &lexeme, NULL);
}

nodiscard nonnull_all
static int32_t exit_scope(parser_t *parser, const scope_t *scope)
{
  int32_t code = 0;
  assert(scope->option);
  if (scope->option->exit) {
    assert(scope->identifier < parser->file->tokens.last);
    const token_t *token = &parser->file->tokens.data[scope->identifier];
    assert(token->code & OPTION);
    assert(token->first < parser->file->buffer.size);
    const char *data = &parser->file->buffer.data[token->first];
    const lexeme_t lexeme = { token->location, { token->first, data } };
    code = scope->option->exit(scope->option, &lexeme, NULL);
  }
  if (scope->encloser && scope->indent > scope->encloser->indent) {
    reduce(parser, scope->indent);
  }
  return code;
}

nodiscard nonnull_all
static int32_t accept(
  parser_t *parser, const scope_t *scope, const token_t *token)
{
  (void)parser;
  (void)scope;
  (void)token;
  fprintf(stderr, "token: %s, value: '%.*s'\n",
    scope->option->pattern.data, // FIXME: option needs a name
    (int)token->size,
    &parser->file->buffer.data[token->first]);
  return 0;
}

nodiscard nonnull_all
static int32_t parse_suboption(parser_t *parser, scope_t *scope)
{
  int32_t code, state = (1 << VALUE);
  size_t token;

  if ((code = enter_scope(parser, scope)) < 0)
    return code;
  if ((code = shift(parser, scope, state, &token)) < 0)
    return code;

  if (code == LINE_FEED) {
    if (parser->file->indent > scope->indent)
      reduce(parser, token);
  } else if (code == VALUE || code == QUOTED_VALUE) {
    if ((code = accept(parser, scope, &parser->file->tokens.data[token])) < 0)
      return code;
  }

  return exit_scope(parser, scope);
}

static int32_t include_filespec(
  parser_t *parser, scope_t *scope, const char *filespec);

static int32_t parse_include(parser_t *parser, scope_t *scope)
{
  int32_t code;
  size_t last;

  if ((code = shift(parser, scope, 0, &last)) < 0)
    return code;
  // accept space between include: and file name
  if (code == SPACE) {
    reduce(parser, last);
    if ((code = shift(parser, scope, 0, &last)) < 0)
      return code;
  }

  if (code != VALUE && code != QUOTED_VALUE)
    return semantic_error(
      parser, scope, &parser->file->tokens.data[last].location,
      "include: directive takes a file name");

  const size_t value = last;

  // accept space and comment after file name
  if ((code = shift(parser, scope, 0, &last)) < 0)
    return code;
  if (code == SPACE) {
    reduce(parser, last);
    if ((code = shift(parser, scope, 0, &last)) < 0)
      return code;
  }
  if (code == COMMENT) {
    reduce(parser, last);
    if ((code = shift(parser, scope, 0, &last)) < 0)
      return code;
  }

  if (code != LINE_FEED && code != END_OF_FILE)
    return semantic_error(
      parser, scope, &parser->file->tokens.data[last].location,
      "include: directive takes only a file name");
  else
    unshift(parser);

  char *file;
  const token_t *token = &parser->file->tokens.data[value];
  if (!(file = strndup(parser->file->buffer.data+token->first, token->size)))
    return OUT_OF_MEMORY;

  code = include_filespec(parser, scope, file);
  free(file);
  reduce(parser, value);
  return code;
}

static int32_t parse_option(parser_t *parser, scope_t *scope)
{
  bool indent = false, newline = false;
  int32_t code, state = (1 << SUBOPTION) | (1 << VALUE);
  size_t last;

  assert(scope->encloser);
  if ((code = enter_scope(parser, scope)) < 0)
    return code;

  for (;;) {
    if ((code = shift(parser, scope, state, &last)) < 0)
      return code;

    if (code == END_OF_FILE || (code & OPTION)) {
      unshift(parser);
      return exit_scope(parser, scope);
    } else if (code == SPACE) {
      if (indent) {
        if (!scope->indent && in_scope(parser, scope->encloser, last) == -1)
          scope->indent = last;
        parser->file->indent = last;
        continue; // retain indentation
      }
    } else if (code == LINE_FEED) {
      // discard indentation unless it dictates scope
      if (parser->file->indent > scope->indent &&
          parser->file->indent > scope->encloser->indent)
        reduce(parser, parser->file->indent);
      state |= (1 << OPTION);
      newline = true;
      parser->file->indent = 0;
    } else if (code == SUBOPTION) {
      const token_t *token = &parser->file->tokens.data[last];
      if (newline && !is_indent(parser, scope, parser->file->indent))
        return syntax_error(parser, scope, &token->location,
          "syntax error, bad indent");
      if (newline && in_scope(parser, scope, parser->file->indent) != 0)
        return semantic_error(parser, scope, &token->location,
          "syntax error, bad indent");
      scope_t enclosed = { scope, 0, last, token->option };
      if ((code = parse_suboption(parser, &enclosed)) < 0)
        return code;
      // suboptions follow (optional) values
      state &= ~((1 << OPTION) | (1 << VALUE));
    } else if (code == VALUE || code == QUOTED_VALUE) {
      const token_t *token = &parser->file->tokens.data[last];
      if (!(state & (1 << VALUE)))
        return semantic_error(parser, scope, &token->location,
          "unexpected literal");
      if (newline && !in_scope(parser, scope, parser->file->indent))
        return semantic_error(parser, scope, &token->location,
          "scope did not match");
      if ((code = accept(parser, scope, token)) < 0)
        return code;
      state &= ~(1 << OPTION);
    } else {
      assert(code == COMMENT);
    }

    reduce(parser, last);
    indent = (code == LINE_FEED);
  }
}

static int32_t parse_section(parser_t *parser, scope_t *scope)
{
  bool indent = false;
  int32_t code, state = 0;
  size_t last;

  assert(scope->encloser);
  if ((code = enter_scope(parser, scope)) < 0)
    return code;

  for (;;) {
    if ((code = shift(parser, scope, state, &last)) < 0)
      return code;

    if (code == END_OF_FILE) {
      unshift(parser);
      return exit_scope(parser, scope);
    } else if (code == SPACE) {
      if (indent) {
        if (!scope->indent && in_scope(parser, scope->encloser, last) == -1)
          scope->indent = last;
        parser->file->indent = last;
        continue; // retain indentation
      }
    } else if (code == LINE_FEED) {
      // reduce indent unless it determines (parent) scope
      if (parser->file->indent > scope->indent &&
          parser->file->indent > scope->encloser->indent)
        reduce(parser, parser->file->indent);
      state |= (1 << OPTION);
      parser->file->indent = 0;
    } else if (code & OPTION) {
      const token_t *token = &parser->file->tokens.data[last];
      if (!is_indent(parser, scope, parser->file->indent))
        return syntax_error(parser, scope, &token->location,
          "syntax error, invalid indentation");

      switch (in_scope(parser, scope, parser->file->indent)) {
        case  1: // option defined in enclosing scope
          unshift(parser);
          return exit_scope(parser, scope);
        case -1: // option defined in enclosed scope, impossible
          return syntax_error(parser, scope, &token->location,
            "syntax error, invalid indentation");
      }

      scope_t enclosed = { scope, 0, last, token->option };
      if (code == OPTION)
        code = parse_option(parser, &enclosed);
      else if (code == SECTION)
        code = parse_section(parser, &enclosed);
      else
        code = parse_include(parser, &enclosed);
      if (code < 0)
        return code;
    } else if (code != COMMENT) {
      const token_t *token = &parser->file->tokens.data[last];
      return syntax_error(parser, scope, &token->location,
        "syntax error");
    }

    indent = (code == LINE_FEED);
    reduce(parser, last);
  }
}

static int32_t parse_file(parser_t *parser, scope_t *scope)
{
  bool indent = false;
  int32_t code, state = (1 << OPTION);
  size_t last;

  for (;;) {
    if ((code = shift(parser, scope, state, &last)) < 0)
      return code;

    if (code == END_OF_FILE) {
      return exit_scope(parser, scope);
    } else if (code == SPACE) {
      if (indent) {
        parser->file->indent = last;
        continue; // retain indentation
      }
    } else if (code == LINE_FEED) {
      if (parser->file->indent)
        reduce(parser, parser->file->indent);
      assert(parser->file->tokens.size > 1);
      parser->file->indent = 0;
    } else if (code == OPTION || code == SECTION) {
      const token_t *token = &parser->file->tokens.data[last];
      if (parser->file->indent)
        return semantic_error(parser, scope, &token->location,
          "syntax error, no indentation at file level");
      scope_t enclosed = { scope, 0, last, token->option };
      if (code == OPTION)
        code = parse_option(parser, &enclosed);
      else
        code = parse_section(parser, &enclosed);
      if (code < 0)
        return code;
    } else if (code != COMMENT) {
      const token_t *token = &parser->file->tokens.data[last];
      return semantic_error(parser, scope, &token->location,
        "syntax error");
    }

    indent = (code == LINE_FEED);
    reduce(parser, last);
  }
}

static char *resolve_path(const char *file)
{
  return strdup(file); // implement properly
}

nonnull_all
static void close_file(maybe_unused parser_t *parser, file_t *file)
{
  assert((file->name == not_a_file) == (file->path == not_a_file));
  assert((file->name != not_a_file) || (file->handle == NULL));

  const bool is_file = file->name && file->name != not_a_file;

  if (file->name != not_a_file)
    free(file->name);
  if (file->path != not_a_file)
    free(file->path);
  if (file->handle)
    (void)fclose(file->handle);
  if (is_file)
    free(file->buffer.data);
  free(file->tokens.data);
}

nonnull_all
static void close_files(parser_t *parser)
{
  for (file_t *file = parser->file, *includer; file; file = includer) {
    includer = file->includer;
    close_file(parser, file);
    if (file != &parser->first)
      free(file);
  }
}

nodiscard nonnull_all
static int32_t open_file(
  maybe_unused parser_t *parser, file_t *file, const char *name)
{
  memset(file, 0, sizeof(*file));

  if (!(file->name = strdup(name)))
    return OUT_OF_MEMORY;
  if (!(file->path = resolve_path(file->name)))
    switch (errno) {
      case ENOMEM: return OUT_OF_MEMORY;
      default:     return NO_SUCH_FILE;
    }
  if (!(file->handle = fopen(file->name, "rb")))
    switch (errno) {
      case ENOMEM: return OUT_OF_MEMORY;
      case EACCES: return NO_ACCESS;
      default:     return NO_SUCH_FILE;
    }

  file->location.file = file->name;
  file->location.line = 1;
  file->location.column = 1;

  if (!(file->tokens.data = calloc(64, sizeof(*file->tokens.data))))
    return OUT_OF_MEMORY;
  // file scope indentation token (no indentation)
  file->tokens.data[0] = (token_t){SPACE, file->location, 0, 0, NULL};
  file->tokens.capacity = 64;
  file->tokens.last = file->tokens.size = 1;
  file->indent = 0;

  return 0;
}

int32_t parse_options_file(
  const option_t *options,
  size_t count,
  const char *file,
  maybe_unused void *user_data)
{
  int32_t code;
  parser_t parser = { 0 };

  if ((code = open_file(&parser, &parser.first, file)) < 0)
    goto error_open;

  parser.file = &parser.first;

  const option_t option = {SECTION, {0, NULL}, {count, options}, 0, 0, 0};
  scope_t scope = {NULL, 0, 0, &option};

  code = parse_file(&parser, &scope);
error_open:
  close_files(&parser);
  return code;
}

static int32_t include_file(
  parser_t *parser, scope_t *scope, const char *filename)
{
  int32_t code;
  file_t *file, *includer;

  if (!(file = calloc(1, sizeof(*file))))
    return OUT_OF_MEMORY;
  file->includer = includer = parser->file;
  parser->file = file;

  if ((code = open_file(parser, file, filename)) < 0)
    return code;

location_t location = { "dummy", 1, 1 };
  for (uint32_t depth = 1; includer; depth++, includer = includer->includer) {
    if (strcmp(file->path, includer->path) == 0)
      return semantic_error(parser, scope, &location,
        "circular include in %s", file->name);
    // check if include limit is not exceeded
    //if (depth > parser->include_limit)
  }

  assert(scope->encloser->option && scope->encloser->option->code == SECTION);

  const option_t option = { SECTION, { 0, NULL }, scope->option->options, 0, 0, 0 };
  scope_t enclosed = { NULL, 0, 0, &option };

  code = parse_file(parser, &enclosed);
  parser->file = file->includer;
  close_file(parser, file);
  return code;
}

static int32_t include_filespec(
  parser_t *parser, scope_t *scope, const char *filespec)
{
  int32_t code;
  find_handle_t *handle = NULL;
  find_data_t data;

  if ((code = find_file(&handle, filespec, &data)) < 0)
    return code;

  while (code > 0) {
    if (data.type == 1 && (code = include_file(parser, scope, data.name)) < 0)
      break;
    if ((code = find_next_file(handle, &data)) < 0) {
      // FIXME: print error
      break;
    }
  }

  find_close(handle);
  return code;
}

int32_t parse_options(
  const option_t *options,
  size_t count,
  const char *string,
  size_t length,
  maybe_unused void *user_data)
{
  parser_t parser = { 0 };

  parser.file = &parser.first;
  parser.file->includer = NULL;
  parser.file->name = not_a_file;
  parser.file->path = not_a_file;
  parser.file->handle = NULL;
  parser.file->buffer.data = (char *)string;
  parser.file->buffer.size = parser.file->buffer.capacity = length;
  parser.file->location.file = parser.file->name;
  parser.file->location.line = 1;
  parser.file->location.column = 1;

  if (!(parser.file->tokens.data = calloc(64, sizeof(token_t))))
    return OUT_OF_MEMORY;
  // file scope indentation token (no indentation)
  parser.file->tokens.data[0] = (token_t){SPACE, parser.file->location, 0, 0, NULL};
  parser.file->tokens.capacity = 64;
  parser.file->tokens.last = parser.file->tokens.size = 1;
  parser.file->indent = 0;

  const option_t option = {SECTION, {0, NULL}, {count, options}, 0, 0, 0};
  scope_t scope = {NULL, 0, 0, &option};

  const int32_t code = parse_file(&parser, &scope);
  close_files(&parser);
  return code;
}
