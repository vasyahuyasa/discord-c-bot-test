.PHONY: build clean

all: build

build:
	gcc -o bot main.c -lcurl -ljson-c

clean:
	rm bot