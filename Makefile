CFLAGS=-Wall -O2
BUILD_DIR=.build
OUTPUT=lc3

build: lc3.c
	mkdir -p $(BUILD_DIR)
	cc $(CFLAGS) lc3.c -o $(BUILD_DIR)/$(OUTPUT)

clean:
	rm -rf $(BUILD_DIR)
