FLAGS:=-Wall -Wextra -pedantic -g -isystemsys/
CFLAGS:=-std=c11 $(FLAGS)
CXXFLAGS:=-std=c++11 $(FLAGS)

.PHONY: rdma clean all

udp: udp.o
	gcc -o $@ $^

raw: raw.o lookup_addr.o
	gcc -o $@ $^

raw_ibverbs: raw_ibverbs.o crc32.o lookup_addr.o opencl_utils.o
	g++ $(shell aocl link-config) -o $@ $^

rdma: rdma_server rdma_client

all: udp raw raw_ibverbs rdma

clean:
	rm rdma_client rdma_server udp raw raw_ibverbs *.o

rdma.o rdma_server.o rdma_client.o: rdma.h

rdma_%: rdma_%.o rdma.o
	gcc -o $@ $^ /lib64/libibverbs.so.1

%.o: %.c
	gcc $(CFLAGS) -c $<

%.o: %.cc
	g++ $(CXXFLAGS) $(shell aocl compile-config) -c $<
