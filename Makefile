all: orphand

orphand: orphand.c hashtable.c contrib/cliopts.c
	$(CC) -I. -Wall -ggdb3 -o $@ $^
