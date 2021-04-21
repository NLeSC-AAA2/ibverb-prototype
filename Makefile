CFLAGS=-std=c11 -Wall -Wextra -pedantic -g

.PHONY: rdma clean

rdma: rdma_server rdma_client

clean:
	rm rdma_client rdma_server *.o

rdma.o rdma_server.o rdma_client.o: rdma.h

rdma_%: rdma_%.o rdma.o
	gcc -o $@ $^ /lib64/libibverbs.so.1

%.o: %.c
	gcc $(CFLAGS) -c $<
