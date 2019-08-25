all:bench_your_web.o
	gcc bench_your_web.o -pthread -o bench
bench_your_web.o:bench_your_web.c
	gcc -c bench_your_web.c -pthread
clean:
	rm -f *.o bench
