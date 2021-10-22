==========================
Raw/FPGA ibverbs prototype
==========================

This repository contains prototype code for doing ibverbs networking over
ROCEv1 from Altera FPGAs.

Contains the following executables:
 - `udp`
 - `raw_udp`
 - `rdma_server`
 - `rdma_client`
 - `raw_ibverbs`

License
=======

Copyright 2021 Netherlands eScience Center and ASTRON.
Licensed under the Apache License, version 2.0. See LICENSE for details.

udp
===

Minimal implementation of UDP client and server that sends a stream of packets
from the client to the server. Mainly for testing/debugging the packet sniffing
and the use of linux raw sockets.

Files:
 - `udp.c`

raw_udp
=======

Implementation of the UDP client part above, but using linux raw sockets to do
the UDP packet and network header initialisation in user code. Reusable address
lookup code for MAC/ethernet addresses, IPv4, and IPv6 addresses is located in
`lookup_addr.h`/`lookup_addr.c`.

Files:
 - `raw_udp.c`
 - `lookup_addr.h`
 - `lookup_addr.c`

rdma_server
===========

An ibverbs-based server that receives a stream of incoming Unreliable Datagram
packets and put them into a circular buffer for processing. Headers and
boilerplate are scattered into their own circular buffer to avoid intermingling
headers/metadata and payload.

Shared ibverbs code is located in `rdma.h`/`rdma.c`. The `constants.h` contains
message size for consistency across the ibverbs, raw, and FPGA implementations.

Files:
 - `rdma_server.c`
 - `constants.h`
 - `rdma.h`
 - `rdma.c`

rdma_client
===========

An ibverbs-based client that produces a stream of Unreliable Datagram packets
for the above `rdma_server`.

Shared ibverbs code is located in `rdma.h`/`rdma.c`. The `constants.h` contains
message size for consistency across the ibverbs, raw, and FPGA implementations.

Files:
 - `rdma_client.c`
 - `constants.h`
 - `rdma.h`
 - `rdma.c`

raw_ibverbs
===========

Same functionality as `rdma_client` but uses Linux' raw sockets and/or an
FPGA implementation to produce the ibverbs packets, instead of using the
ibverbs API.

The `constants.h` contains message size for consistency across the ibverbs,
raw, and FPGA implementations. Reusable address lookup code for MAC/ethernet
addresses, IPv4, and IPv6 addresses is located in
`lookup_addr.h`/`lookup_addr.c`. `crc32.h` has a simple, unoptimised CRC-32
implementation. Struct definitions for the various ibverbs headers are in
`raw_packet.h`/`raw_packet.c`. OpenCL wrapper code for running the FPGA kernel
is in `fpga_host.h`/`fpga_host.cc`, the OpenCL FPGA kernel itself is in
`ibverbs.cl`. The `opencl_utils.hpp`/`opencl_utils.cc` files contain various
wrappers for OpenCL boilerplate.

Files:
 - `raw_ibverbs.c`
 - `constants.h`
 - `lookup_addr.h`
 - `lookup_addr.c`
 - `crc32.h`
 - `raw_packet.h`
 - `raw_packet.c`
 - `fpga_host.h`
 - `fpga_host.cc`
 - `ibverbs.cl`
 - `opencl_utils.hpp`
 - `opencl_utils.cc`
