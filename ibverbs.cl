#pragma OPENCL EXTENSION cl_intel_channels : enable
#include "constants.h"
#include "crc32.h"

#define FLAG_FIRST	0x01
#define FLAG_LAST	0x02

struct line {
  uint8 payload;
  uchar flags;
} __attribute__((packed));

channel struct line ch_out0 __attribute__((depth(0))) __attribute__((io("kernel_output_ch0")));
channel struct line ch_out1 __attribute__((depth(0))) __attribute__((io("kernel_output_ch1")));

__attribute__((max_global_work_dim(0)))
__kernel void
write_ibverb_packet
(__global const char *header, unsigned header_crc, unsigned header_size, int chan)
{
    unsigned char data[MSG_SIZE+100];

    for (int i = 0; i < header_size; i++) {
        data[i] = header[i];
    }

    unsigned foo = crc32(header_crc, &data[header_size], MSG_SIZE);

    *((unsigned*) &data[header_size + MSG_SIZE]) = foo;

    unsigned size = header_size + MSG_SIZE + 4;
    for (unsigned i = 0; 32 * i < size; i ++) {
        struct line line;
        line.payload = ((uint8 *) data)[i];
        line.flags   = 0;

        if (i == 0)
            line.flags |= FLAG_FIRST;

        if (i == (size - 1) / 32U)
            line.flags |= FLAG_LAST | ((-size % 32U) << 3);

        if (chan == 0) write_channel_intel(ch_out0, line);
        else write_channel_intel(ch_out1, line);
    }
}
