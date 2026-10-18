/*
 * Strict integer parsing helpers.
 *
 * strtol-based replacements for atoi() that reject empty input, trailing
 * garbage, and out-of-range values instead of silently truncating or
 * returning 0. Header-only so they can be unit-tested directly.
 *
 * Copyright (C) Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef ERNIC_PARSE_INT_H
#define ERNIC_PARSE_INT_H

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

/*
 * Parse a base-10 int. Rejects NULL/empty input, trailing non-numeric
 * characters, and values outside [INT_MIN, INT_MAX]. On success stores the
 * value in *out and returns true; otherwise leaves *out untouched and
 * returns false.
 */
static inline bool ernic_parse_int(const char *s, int *out)
{
    if (!s || *s == '\0') {
        return false;
    }

    errno = 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);

    if (errno != 0 || end == s || *end != '\0') {
        return false;
    }
    if (v < INT_MIN || v > INT_MAX) {
        return false;
    }

    *out = (int)v;
    return true;
}

/*
 * Parse a TCP port in [1, 65535]. Returns true on success (value in *out),
 * false for anything out of range, non-numeric, or zero.
 */
static inline bool ernic_parse_port(const char *s, uint16_t *out)
{
    int v;
    if (!ernic_parse_int(s, &v)) {
        return false;
    }
    if (v < 1 || v > 65535) {
        return false;
    }

    *out = (uint16_t)v;
    return true;
}

/*
 * Parse a base-10 uint64_t. Only the digits 0-9 are accepted: unlike
 * strtoull(), a sign or leading whitespace is rejected, as are NULL/empty
 * input, trailing characters, and values above UINT64_MAX. On success stores
 * the value in *out and returns true; otherwise leaves *out untouched and
 * returns false.
 */
static inline bool ernic_parse_u64(const char *s, uint64_t *out)
{
    uint64_t v = 0;

    if (!s || *s == '\0') {
        return false;
    }
    for (; *s; s++) {
        if (*s < '0' || *s > '9') {
            return false;
        }
        uint64_t digit = (uint64_t)(*s - '0');
        if (v > (UINT64_MAX - digit) / 10) {
            return false;
        }
        v = v * 10 + digit;
    }

    *out = v;
    return true;
}

#endif /* ERNIC_PARSE_INT_H */
