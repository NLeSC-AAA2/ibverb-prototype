CFLAGS=-std=c11 -Wall -Wextra -pedantic -g -isystemsys/

.PHONY: rdma clean all

udp: udp.o
	gcc -o $@ $^

raw: raw.o
	gcc -o $@ $^

rdma: rdma_server rdma_client

all: udp raw rdma

clean:
	rm rdma_client rdma_server udp raw *.o

rdma.o rdma_server.o rdma_client.o: rdma.h

rdma_%: rdma_%.o rdma.o
	gcc -o $@ $^ /lib64/libibverbs.so.1

%.o: %.c
	gcc $(CFLAGS) -c $<
