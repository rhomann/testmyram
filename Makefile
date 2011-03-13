CC=gcc
CCWARN=-Wall -Wundef -Wshadow -Winline -Wcast-qual -Wcast-align -Wpointer-arith -Wsign-compare -Wfloat-equal -Wwrite-strings -Waggregate-return -Wnested-externs -Wmissing-prototypes -Wstrict-prototypes -Wnested-externs -Werror
CCOPT=-g -O0
CFLAGS=-std=c99 -pipe -pthread $(CCWARN) $(CCOPT)
CPPFLAGS=-D_XOPEN_SOURCE=600
LDLIBS=-lpthread

OBJ=testmyram.o prng.o

.PHONY: all clean

all: testmyram

clean:
	rm -f $(OBJ)
	rm -f testmyram

testmyram: $(OBJ)

testmyram.o: testmyram.c prng.h

prng.o: prng.c prng.h
