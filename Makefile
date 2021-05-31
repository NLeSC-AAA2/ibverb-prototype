FLAGS:=-Wall -Wextra -pedantic -g -isystemsys/ -isystem/var/scratch/package/altera_pro/20.3.0.158/hld/host/include/
CFLAGS:=-std=c11 $(FLAGS)
CXXFLAGS:=-std=c++11 $(FLAGS)

.PHONY: rdma clean all kernel

udp: udp.o
	gcc -o $@ $^

raw: raw.o lookup_addr.o
	gcc -o $@ $^

raw_ibverbs: raw_ibverbs.o lookup_addr.o opencl_utils.o raw_packet.o fpga_host.o
	g++ $(shell aocl link-config) -o $@ $^

rdma: rdma_server rdma_client

all: udp raw raw_ibverbs rdma

kernel: ibverbs.aocx

clean:
	rm -r rdma_client rdma_server udp raw raw_ibverbs *.o ibverbs.*.temp/

rdma.o rdma_server.o rdma_client.o: rdma.h

rdma_%: rdma_%.o rdma.o
	gcc -o $@ $^ /lib64/libibverbs.so.1

%.o: %.c
	gcc $(CFLAGS) -c $<

%.o: %.cc
	g++ $(CXXFLAGS) $(shell aocl compile-config) -c $<

AOC=			aoc
AOCOFLAGS+=		-Wno-error=analyze-channels-usage
AOCOFLAGS+=		-I$(INTELFPGAOCLSDKROOT)/include/kernel_headers
AOCRFLAGS+=		-fp-relaxed
AOCRFLAGS+=		-report
AOCRFLAGS+=		-opt-arg=--allow-io-channel-autorun-kernel

%.aoco: %.cl
	$(AOC) -c $(AOCOFLAGS) $<

%.aocr: %.aoco
	$(AOC) -rtl $(AOCRFLAGS) $<

%.aocx: %.aocr
	$(AOC) $(AOCXFLAGS) $<
