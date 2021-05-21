/*
 * Copyright 2021 Netherlands eScience Center and ASTRON.
 * Licensed under the Apache License, version 2.0. See LICENSE for details.
 */

#include <fstream>
#include <iostream>
#include <vector>

#include <stdint.h>

#include "fpga_host.h"
#include "opencl_utils.hpp"

extern "C" void
ib_fpga_send_loop(struct packet *packet, uint32_t header_crc)
{
    cl::Context context;
    std::vector<cl::Device> devices;
    createContext(context, devices);

    cl::Program program = get_program(context, devices[0], "ibverbs.aocx");
    cl::CommandQueue queue(context, devices[0], CL_QUEUE_PROFILING_ENABLE);

    uint32_t header_size = sizeof packet->ether_header + sizeof packet->ib_header;

    cl::Buffer device_buf(context, CL_MEM_READ_ONLY, header_size);
    cl::Buffer host_buf(context,
            CL_MEM_HOST_WRITE_ONLY | CL_MEM_ALLOC_HOST_PTR, header_size);

    void *buffer = queue.enqueueMapBuffer(host_buf, CL_TRUE, CL_MAP_WRITE, 0, header_size);
    memcpy(buffer, packet, header_size);

    cl::Event event;
    Kernel kernel(program, "write_ibverb_packet");

    queue.enqueueCopyBuffer(host_buf, device_buf, 0, 0, header_size);

    kernel(device_buf, header_crc, header_size, 1);
    kernel.finish();

    queue.enqueueUnmapMemObject(host_buf, buffer);
}
