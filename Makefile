.PHONY: build clean

all: build

build:
	gcc -o bot main.c -lcurl

clean:
	rm bot