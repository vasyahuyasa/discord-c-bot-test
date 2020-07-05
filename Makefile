.PHONY: build clean

all: build

build:
	gcc -o bot main.c -lwebsockets 

clean:
	rm bot