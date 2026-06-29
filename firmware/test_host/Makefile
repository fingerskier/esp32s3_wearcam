CC ?= cc
CFLAGS = -Wall -Wextra -std=c11 -I../main -g
test: test_ring_buffer
	./test_ring_buffer
test_ring_buffer: test_ring_buffer.c ../main/ring_buffer.c ../main/ring_buffer.h
	$(CC) $(CFLAGS) -o test_ring_buffer test_ring_buffer.c ../main/ring_buffer.c
clean:
	rm -f test_ring_buffer test_ring_buffer.exe