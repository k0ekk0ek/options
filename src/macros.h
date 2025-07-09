/*
 * macros.h -- compiler macro abstractions
 *
 * Copyright (c) 2025, Jeroen Koekkoek
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#ifndef MACROS_H
#define MACROS_H

#if defined __has_attribute
# define has_attribute(x) __has_attribute(x)
#else
# define has_attribute(x) (0)
#endif

#if defined __has_builtin
# define has_builtin(x) __has_builtin(x)
#else
# define has_builtin(x) (0)
#endif

#if has_builtin(__builtin_expect)
# define likely(x) __builtin_expect(!!(x), 1)
# define unlikely(x) __builtin_expect(!!(x), 0)
#endif

#if has_attribute(nonnull)
# define nonnull(x) __attribute__((__nonnull__ x))
# define nonnull_all __attribute__((__nonnull__))
#else
# define nonnull(x)
# define nonnull_all
#endif

#if defined __GNUC__
# define have_gnuc(major, minor) \
((__GNUC__ > major) || (__GNUC__ == major && __GNUC_MINORE__ >= minor))
#else
# define have_gnuc(major, minor) (0)
#endif

#if _MSC_VER
# define always_inline __forceinline
# define never_inline __declspec(noinline)
#else // _MSC_VER
# if (has_attribute(always_inline) || have_gnuc(3, 1)) && ! defined(__NO_INLINE__)
  // Compilation using GCC 4.2.1 without optimizations failes.
  //   sorry, unimplemented: inlining failed in call to ...
  // GCC 4.1.2 and GCC 4.30 compile forward declared functions annotated
  // with __attribute__((always_inline)) without problems. Test if
  // __NO_INLINE__ is defined and define macro accordingly.
#   define always_inline inline __attribute__((always_inline))
# else
#   define always_inline inline
# endif

# if has_attribute(noinline) || have_gnuc(2, 96)
#   define never_inline __attribute__((noinline))
# else
#   define never_inline
# endif
#endif

#if has_attribute(returns_nonnull)
# define returns_nonnull __attribute__((returns_nonnull))
#else
# define returns_nonnull
#endif

#if has_attribute(unused)
# define maybe_unused __attribute__((unused))
#else
# define maybe_unused
#endif

#if has_attribute(warn_unused_result)
# define nodiscard __attribute__((warn_unused_result))
#else
# define nodiscard
#endif

#endif // MACROS_H
