/*
 * Copyright 2021 Netherlands eScience Center and ASTRON.
 * Licensed under the Apache License, version 2.0. See LICENSE for details.
 */

#include <stdint.h>
#include <unistd.h>

uint32_t crc32(uint32_t crc, const unsigned char *data, ssize_t size);
