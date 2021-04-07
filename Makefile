.DEFAULT_GOAL:=default
CFLAGS=-Wall -Wextra -g

.PHONY: default clean
default: ud_server ud_client

clean:
	rm ud_client ud_server *.o

rdma.o: rdma.c rdma.h
	gcc $(CFLAGS) -c $<

ud_server.o: ud_server.c rdma.h
	gcc $(CFLAGS) -c $<

ud_server: ud_server.o rdma.o
	gcc -o $@ $^ /lib64/libibverbs.so.1

ud_client.o: ud_client.c rdma.h
	gcc $(CFLAGS) -c $<

ud_client: ud_client.o rdma.o
	gcc -o $@ $^ /lib64/libibverbs.so.1
