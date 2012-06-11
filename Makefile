all: orphand orphand-forkwait.so libprocstat.so

CFLAGS = -Iinclude/orphand -I. -Wall -Winit-self -ggdb3

orphand: src/orphand.c contrib/cliopts.c src/procstat.c
	$(CC) $(CFLAGS) -o $@ $^

orphand-forkwait.so: src/orphand-forkwait.c
	$(CC) -shared -fPIC $(CFLAGS) -o $@ $^ -ldl

libprocstat.so: src/procstat.c
	$(CC) -shared -fPIC $(CFLAGS) -o $@ $^

clean:
	rm -f orphand orphand-forkwait.so
