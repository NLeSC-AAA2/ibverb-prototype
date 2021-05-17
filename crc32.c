/*
 * Copyright 2021 Netherlands eScience Center and ASTRON.
 * Licensed under the Apache License, version 2.0. See LICENSE for details.
 */

#include "crc32.h"
#include "crc32table.h"

uint32_t
crc32(uint32_t crc, const unsigned char *data, ssize_t size)
{
    crc = crc ^ 0xFFFFFFFF;

    for (int i = 0; i < size; i++) {
        crc = crc32table[(((int) crc) & 0xFF) ^ data[i]] ^ (crc >> 8);
    }

    return crc ^ 0xFFFFFFFF;
}
