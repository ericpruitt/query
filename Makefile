CFLAGS = -std=c99 -Wall

query: query.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f query

.PHONY: clean
