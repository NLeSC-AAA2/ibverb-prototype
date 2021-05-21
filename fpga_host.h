/*
 * Copyright 2021 Netherlands eScience Center and ASTRON.
 * Licensed under the Apache License, version 2.0. See LICENSE for details.
 */
#ifndef FPGA_HOST_H
#define FPGA_HOST_H

#ifdef __cplusplus
extern "C" {
#endif

#include "raw_packet.h"

void ib_fpga_send_loop(struct packet *packet, uint32_t header_crc);

#ifdef __cplusplus
}
#endif
#endif
