/**
 * findfirst.c -- _findfirst-like directory search for POSIX
 *
 * Copyright (c) 2025, Jeroen Koekkoek
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>

#include "findfirst.h"
#include "match.h"

struct find_handle {
  char *file;
  char *directory;
  DIR *stream;
};

nodiscard nonnull_all
static int32_t find_first_file(
  find_handle_t *handle, const char *filespec, find_data_t *fileinfo)
{
  const char *end = filespec, *slash = NULL;

  // find last separator
  for (; *end; end++)
    if (*end == '/')
      slash = end;

  handle->file = !slash
    ? strdup(filespec) : strdup(slash + 1);
  handle->directory = !slash
    ? strdup(".") : strndup(filespec, (size_t)(slash - filespec) + 1);

  if (!handle->file || !handle->directory)
    return OUT_OF_MEMORY;
  if (!*handle->file)
    return BAD_PARAMETER;

  if (!(handle->stream = opendir(handle->directory)))
    switch (errno) {
      case EACCES:  return NO_ACCESS;
      case ENOENT:
      case ENOTDIR: return NO_SUCH_FILE;
      default:      return OUT_OF_MEMORY;
    }

  return find_next_file(handle, fileinfo);
}

int32_t find_file(
  find_handle_t **handlep, const char *filespec, find_data_t *fileinfo)
{
  int32_t code;
  find_handle_t *handle;

  if (!(handle = calloc(1, sizeof(*handle))))
    return OUT_OF_MEMORY;
  if ((code = find_first_file(handle, filespec, fileinfo)) >= 0)
    return (void)(*handlep = handle), code;

  find_close(handle);
  return code;
}

int32_t find_next_file(find_handle_t *handle, find_data_t *fileinfo)
{
  for (;;) {
    const int saved_errno = errno;
    errno = 0;
    const struct dirent *entry = readdir(handle->stream);
    const int readdir_errno = errno;
    errno = saved_errno;
    if (!entry)
      switch (readdir_errno) {
        case ENOENT: return NO_SUCH_FILE;
        case EINVAL: return BAD_PARAMETER;
        default:     return 0;
      }

    if (!match_mask(entry->d_name, handle->file))
      continue;

    fileinfo->name = entry->d_name;
    switch (entry->d_type) {
      case DT_DIR: fileinfo->type = FIND_DIRECTORY; break;
      case DT_REG: fileinfo->type = FIND_REGULAR;   break;
      default:     fileinfo->type = FIND_UNKNOWN;   break;
    }

    return 1;
  }
}

void find_close(find_handle_t *handle)
{
  if (!handle)
    return;
  free(handle->file);
  free(handle->directory);
  if (handle->stream)
    closedir(handle->stream);
  free(handle);
}
