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

#undef SECTION
#undef OPTION
#undef SUBOPTION

#include "regex.h"
#include "macros.h"

typedef struct token token_t;
struct token {
  /** Type of token. e.g., SPACE, IDENTIFIER, etc */
  int32_t code;
  /** Location of token */
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
  FILE *handle;

  position_t position;

  struct {
    size_t offset, size, capacity;
    union { const char *read; char *write; } data;
  } buffer;

  /** Token stack (first reserved for no-indentation) */
  struct {
    size_t offset, size, capacity;
    token_t *data;
  } tokens;

  // Register latest indent (indent for next (sub)option) to make finding
  // corresponding scope more convenient. Set by parser after newline.
  size_t indent;
  // Register start of line (to filter scope or otherwise blank space).
  size_t escape;
};

typedef struct parser parser_t;
struct parser {
  file_t first, *file;
  option_tuple_t options;
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
  for (; have_char(start, end) == OPTION; start++) ;
  return start; // maybe ':' or '='
}

nodiscard nonnull_all
static always_inline const char *scan_literal(
  maybe_unused parser_t *parser, maybe_unused const scope_t *scope, const char *start, const char *end)
{
  for (; have_char(start, end) >= OPTION; start++) ;
  return start;
}

nodiscard nonnull_all
static always_inline const char *scan_quoted_literal(
  parser_t *parser, maybe_unused const scope_t *scope, const char *start, const char *end)
{
  assert(start < end);
  start += parser->file->escape != '\0';

  for (; have_char(start, end) > 0 && (parser->file->escape || *start != '\"'); start++)
    parser->file->escape = *start == '\\' && !parser->file->escape;

  assert(start == end || *start == '\"' || parser->file->escape);
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
  for (size_t i = 0; i < option->options.size; i++)
    if (matches(&option->options.data[i], identifier, length))
      return &option->options.data[i];
  return NULL;
}

nodiscard nonnull((1,2,4))
static const option_t *is_option(
  const parser_t *parser, const scope_t *scope, const int32_t state,
  const char *identifier, size_t length)
{
  if (!(state & (1 << OPTION)))
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
  for (size_t i = 0; i < option->options.size; i++)
    if (matches(&option->options.data[i], identifier, length))
      return &option->options.data[i];
  return NULL;
}

nodiscard nonnull((1,2,4))
static const option_t *is_suboption(
  const parser_t *parser, const scope_t *scope, const int32_t state,
  const char *identifier, size_t length)
{
  (void)parser;
  if (!(state & (1 << SUBOPTION)))
    return NULL;

  // FIXME: incorrect, must be properly enclosed if on different line
  return has_suboption(scope->option, identifier, length);
}

nonnull_all
static int32_t scan(
  parser_t *parser, const scope_t *scope, const int32_t state, token_t *token)
{
  const char *start, *end, *bound;
  const option_t *option = NULL;

  start = end = parser->file->buffer.data.read + parser->file->buffer.offset;
  bound = parser->file->buffer.data.read + parser->file->buffer.size;

  int32_t code;
  if (end == bound) {
    // end-of-file, refill?
    code = END_OF_FILE;
    goto end_of_file; // temporary
  }

  if (*start == '\"')
    code = QUOTED_VALUE;
  else if (*start == '-') // identifiers cannot start with '-'
    code = VALUE;
  else if ((code = table[(uint8_t)*start]) < 0)
    return -1;

  end = start + 1;
  do {
    if (end == bound) {
      // FIXME: end-of-file, refill?
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
        // FIXME: do not loop if end == bound
        break;
      case QUOTED_VALUE:
        end = scan_quoted_literal(parser, scope, end, bound);
        break;
      case OPTION:
        end = scan_identifier(parser, scope, end, bound);
        assert(end <= bound);
        if (end == bound) {
          // FIXME: actually conditional
          code = VALUE;
          break;
        }
        if (*end == ':' && (option = is_option(parser, scope, state, start, (size_t)(end - start)))) {
          end++;
          break;
        }
        if (*end == '=' && (option = is_suboption(parser, scope, state, start, (size_t)(end - start)))) {
          end++;
          break;
        }
        code = VALUE;
        // fall through
      default:
        assert(code == VALUE);
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

nodiscard nonnull_all
static never_inline int32_t shift(
  parser_t *parser, const scope_t *scope, int32_t state, token_t **token)
{
  assert(parser->file->tokens.size > 0);
  assert(parser->file->tokens.offset > 0);

  if (parser->file->tokens.offset == parser->file->tokens.size) {
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

    *token = &parser->file->tokens.data[ parser->file->tokens.size ];

    if (scan(parser, scope, state, *token) < 0)
      return OUT_OF_MEMORY;
    parser->file->tokens.offset++;
    parser->file->tokens.size++;
    return (*token)->code;
  }

  *token = &parser->file->tokens.data[ parser->file->tokens.offset++ ];
  return (*token)->code;
}

nonnull_all
static never_inline void unshift(parser_t *parser)
{
  assert(parser->file->tokens.offset > 1);
  assert(parser->file->tokens.offset <= parser->file->tokens.size);
  parser->file->tokens.offset--;
}

nonnull_all
static never_inline void reduce(parser_t *parser)
{
  assert(parser->file->tokens.offset > 1);
  assert(parser->file->tokens.offset <= parser->file->tokens.size);

  // retain unshifted tokens
  const size_t move = parser->file->tokens.size - parser->file->tokens.offset;
  memmove(
    parser->file->tokens.data + (parser->file->tokens.offset - 1),
    parser->file->tokens.data + (parser->file->tokens.offset),
    move * sizeof(*parser->file->tokens.data));

  parser->file->tokens.offset--;
  parser->file->tokens.size--;
}

nodiscard nonnull_all
static int32_t enter_scope(parser_t *parser, const scope_t *scope)
{
  assert(scope->option);
  if (!scope->option->enter)
    return 0;
  assert(scope->identifier < parser->file->tokens.offset);
  const token_t *token = &parser->file->tokens.data[scope->identifier];
  assert(token->code & OPTION);
  assert(token->offset < parser->file->buffer.size);
  const char *data = &parser->file->buffer.data.read[token->offset];
  const lexeme_t lexeme = { token->location, { token->length, data } };
  return scope->option->enter(scope->option, &lexeme, NULL);
}

nodiscard nonnull_all
static int32_t exit_scope(parser_t *parser, const scope_t *scope)
{
  assert(scope->option);
  // FIXME: implement reducing scope indentation
  if (!scope->option->exit)
    return 0;
  assert(scope->identifier < parser->file->tokens.offset);
  const token_t *token = &parser->file->tokens.data[scope->identifier];
  assert(token->code & OPTION);
  assert(token->offset < parser->file->buffer.size);
  const char *data = &parser->file->buffer.data.read[token->offset];
  const lexeme_t lexeme = { token->location, { token->length, data } };
  return scope->option->exit(scope->option, &lexeme, NULL);
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
    (int)token->length,
    &parser->file->buffer.data.read[token->offset]);
  return 0;
}

static always_inline bool in_options(
  const option_t *section, const option_t *option)
{
  return option >= section->options.data &&
         option <= section->options.data + section->options.size;
}

nodiscard nonnull((1,2))
static bool in_scope(parser_t *parser, scope_t *scope, size_t indent)
{
  const char *data = parser->file->buffer.data.read;
  const token_t *inner = &parser->file->tokens.data[indent];
  const token_t *outer = &parser->file->tokens.data[scope->indent];

  return inner->length == outer->length &&
         memcmp(data+outer->offset, data+inner->offset, inner->length) == 0;
}

nodiscard nonnull_all
static int32_t syntax_error(
  const parser_t *parser, const scope_t *scope, const token_t *token,
  const char *format)
{
  (void)parser;
  (void)scope;
  (void)token;
  fprintf(stderr, "error: %s\n", format);
  return SYNTAX_ERROR;
}

nodiscard nonnull_all
static int32_t parse_suboption(parser_t *parser, const scope_t *scope)
{
  int32_t code;
  const int32_t state = (1 << VALUE);
  token_t *token;

  if ((code = enter_scope(parser, scope)) < 0)
    return code;
  if ((code = shift(parser, scope, state, &token)) < 0)
    return code;

  if (code == LINE_FEED) {
    if (parser->file->indent > scope->indent)
      reduce(parser);
  } else if (code == VALUE || code == QUOTED_VALUE) {
    if ((code = accept(parser, scope, token)) < 0)
      return code;
  }

  return exit_scope(parser, scope);
}

static int32_t parse_option(parser_t *parser, scope_t *scope)
{
  bool indent = false, newline = false;
  int32_t code, state = (1 << SUBOPTION) | (1 << VALUE);
  token_t *token;

  if ((code = enter_scope(parser, scope)) < 0)
    return code;

  for (;;) {
    if ((code = shift(parser, scope, state, &token)) < 0)
      return code;

    if (code == END_OF_FILE || code == OPTION || code == SECTION) {
      unshift(parser);
      return exit_scope(parser, scope);
    } else if (code == SPACE) {
      if (indent) {
        if (!scope->indent)
          scope->indent = parser->file->tokens.size - 1;
        parser->file->indent = parser->file->tokens.size - 1;
        continue; // retain indentation
      }
    } else if (code == LINE_FEED) {
      // discard indentation unless it dictates scope
      if (scope->indent < parser->file->indent &&
          scope->indent > scope->encloser->indent)
        reduce(parser);
      state |= (1 << OPTION);
      newline = true;
      parser->file->indent = 0;
    } else if (code == SUBOPTION) {
      if (newline && !in_scope(parser, scope, parser->file->indent))
        return syntax_error(parser, scope, token, "syntax error");
      if (!in_options(scope->option, token->option))
        return syntax_error(parser, scope, token, "syntax error");
      const scope_t enclosed = { scope, 0, 0, token->option };
      if ((code = parse_suboption(parser, &enclosed)) < 0)
        return code;
      // suboptions follow (optional) literals
      state &= ~((1 << OPTION) | (1 << VALUE));
    } else if (code == VALUE || code == QUOTED_VALUE) {
      if (!(state & (1 << VALUE)))
        return syntax_error(parser, scope, token, "unexpected literal");
      if (newline && !in_scope(parser, scope, parser->file->indent))
        return syntax_error(parser, scope, token, "scope did not match");
      if ((code = accept(parser, scope, token)) < 0)
        return code;
      state &= ~(1 << OPTION);
    } else {
      assert(code == COMMENT);
    }

    reduce(parser);
    indent = (code == LINE_FEED);
  }
}

static int32_t parse_section(parser_t *parser, scope_t *scope)
{
  bool indent = false;
  int32_t code, state = 0;
  token_t *token;

  if ((code = enter_scope(parser, scope)) < 0)
    return code;

  for (;;) {
    if ((code = shift(parser, scope, state, &token)) < 0)
      return code;

    if (code == END_OF_FILE) {
      unshift(parser);
      return exit_scope(parser, scope);
    } else if (code == SPACE) {
      if (indent) {
        parser->file->indent = parser->file->tokens.size - 1;
        continue; // retain indentation
      }
    } else if (code == LINE_FEED) {
      // reduce indent unless it determines (parent) scope
      if (parser->file->indent > scope->indent &&
          parser->file->indent > scope->encloser->indent)
        reduce(parser);
      state |= (1 << OPTION);
      parser->file->indent = 0;
    } else if (code == OPTION || code == SECTION) {
      assert(token && token->option);
      if (!in_options(scope->option, token->option))
        return exit_scope(parser, scope);

      if (!scope->indent)
        scope->indent = parser->file->indent;
      else if (!in_scope(parser, scope, parser->file->indent))
        return syntax_error(parser, scope, token, "syntax error");

      scope_t enclosed = { scope, 0, 0, token->option };
      if (code == OPTION)
        code = parse_option(parser, &enclosed);
      else
        code = parse_section(parser, &enclosed);
      if (code < 0)
        return code;
    } else if (code != COMMENT) {
      return syntax_error(parser, scope, token, "syntax error");
    }

    indent = (code == LINE_FEED);
    reduce(parser);
  }
}

static int32_t parse_file(parser_t *parser, const scope_t *scope)
{
  bool indent = false;
  int32_t code;
  const int32_t state = (1 << OPTION);
  token_t *token;

  if ((code = enter_scope(parser, scope)) < 0)
    return code;

  for (;;) {
    if ((code = shift(parser, scope, state, &token)) < 0)
      return code;

    if (code == END_OF_FILE) {
      return exit_scope(parser, scope);
    } else if (code == SPACE) {
      if (indent) {
        parser->file->indent = parser->file->tokens.size - 1;
        continue; // retain indentation
      }
    } else if (code == LINE_FEED) {
      if (parser->file->indent > scope->indent)
        reduce(parser);
      assert(parser->file->tokens.size > 1);
      parser->file->indent = 0;
    } else if (code == OPTION || code == SECTION) {
      if (parser->file->indent)
        return syntax_error(parser, scope, token, "syntax error");
      assert(token && token->option);
      scope_t enclosed = { scope, 0, parser->file->tokens.size - 1, token->option };
      if (code == OPTION)
        code = parse_option(parser, &enclosed);
      else
        code = parse_section(parser, &enclosed);
      if (code < 0)
        return code;
    } else if (code != COMMENT) {
      return syntax_error(parser, scope, token, "syntax error");
    }

    indent = (code == LINE_FEED);
    reduce(parser);
  }
}

#if 0
static int32_t include_file(parser_t *parser, scope_t *scope)
{
  // implement
}
#endif

static int32_t parse(parser_t *parser, void *user_data)
{
  file_t *file = parser->file;
  const option_t option = { SECTION, { 0, NULL }, parser->options, 0, 0, 0 };
  const scope_t scope = { NULL, 0, 0, &option };

  (void)user_data;

  if (!(file->tokens.data = malloc(sizeof(*file->tokens.data) * 64)))
    return OUT_OF_MEMORY;
  // file scope indentation token (no indentation)
  file->tokens.data[0] = (token_t){ SPACE, { file->name, {1, 1}, {1, 1} }, 0, 0, NULL };
  file->tokens.capacity = 64;
  file->tokens.offset = file->tokens.size = 1;

  file->position.line = 1;
  file->position.column = 1;

  file->indent = 0;
  file->escape = 0;

  int32_t code = parse_file(parser, &scope);

  free(file->tokens.data);
  return code;
}

int32_t parse_options_string(
  const option_tuple_t *options,
  const char *string,
  size_t length,
  void *user_data)
{
  parser_t parser = { 0 };

  parser.file = &parser.first;
  parser.file->includer = NULL;
  parser.file->name = not_a_file;
  parser.file->path = not_a_file;
  parser.file->handle = NULL;
  parser.file->buffer.data.read = string;
  parser.file->buffer.size = parser.file->buffer.capacity = length;
  parser.options = *options;

  return parse(&parser, user_data);
}
