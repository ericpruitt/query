CC = clang
CFLAGS = -std=c99 -Wall -Weverything

query: query.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f query

.PHONY: clean
