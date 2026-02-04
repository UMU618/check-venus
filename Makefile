all: check-venus

check-venus: check-venus.c
	$(CC) -std=c11 -O2 check-venus.c -o check-venus -lvulkan -ldl

.PHONY: clean
clean:
	rm -f check-venus
