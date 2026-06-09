CC = gcc
CFLAGS = -Wall -O2
LDFLAGS = -libverbs

# Default target: build the bw_template executable
all: bw_template

# Rule to build the RDMA benchmark executable
bw_template: bw_template.c
	$(CC) $(CFLAGS) -o bw_template bw_template.c $(LDFLAGS)

# Rule to clean up the built executable
clean:
	rm -f bw_template

.PHONY: all clean