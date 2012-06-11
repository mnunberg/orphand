all: orphand orphand-forkwait.so

CFLAGS = -I. -Wall -Winit-self -ggdb3

orphand: orphand.c contrib/cliopts.c
	$(CC) $(CFLAGS) -o $@ $^

orphand-forkwait.so: orphand-forkwait.c
	$(CC) -shared -fPIC $(CFLAGS) -o $@ $^ -ldl

clean:
	rm -f orphand orphand-forkwait.so
