.DEFAULT_GOAL:=default
CFLAGS=-Wall -Wextra -g

.PHONY: default clean
default: rdma_server rdma_client

clean:
	rm rdma_client rdma_server *.o

%.o: %.c
	gcc $(CFLAGS) -c $<

rdma.o rdma_server.o rdma_client.o: rdma.h

rdma_server: rdma_server.o rdma.o
	gcc -o $@ $^ /lib64/libibverbs.so.1

rdma_client: rdma_client.o rdma.o
	gcc -o $@ $^ /lib64/libibverbs.so.1
