CC = gcc
CFLAGS = -Wall -g
LD = gcc
LDFLAGS = -g

all: mush2 mush2.o

mush2: mush2.o
	$(CC) $(CFLAGS) -L ~pn-cs357/Given/Mush/libmush/lib64 -o mush2 mush2.o \
	-lmush

mush2.o: mush2.c
	$(CC) $(CFLAGS) -I ~pn-cs357/Given/Mush/libmush/include -c -o \
	mush2.o mush2.c

comp:
	$(CC) $(CFLAGS) -c -o mush2.o mush2.c
	$(CC) $(CFLAGS) -o mush2 mush2.o

clean: mush2
	rm mush2.o
test: mush2
	./mush2 test_cmd
valgrind: mush2
	valgrind -s --leak-check=full --track-origins=yes --show-leak-kinds=all \
	./mush2 test_cmd
valgrind2: mush2
	valgrind -s --leak-check=full --track-origins=yes --show-leak-kinds=all \
	./mush2 test_cmd2