CC = clang
CFLAGS = -Wall -Wextra -Werror -pedantic
SOURCE = memory.c

all: clean memory

memory: memory.c
	$(CC) $(CFLAGS) -o memory $(SOURCE);

clean:
	rm -rf *.o *.log *.txt *.dat
	rm -rf memory mem_size valgrind.txt command

