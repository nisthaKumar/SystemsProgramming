CC = clang
CFLAGS = -Wall -Wextra -Werror -pedantic
SOURCE = hello.c

all: hello

hello: hello.c
	$(CC) $(CFLAGS) -o hello $(SOURCE); ./hello

clean:
	rm -rf *.o
	rm -rf hello
	

