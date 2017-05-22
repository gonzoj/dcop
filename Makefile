ROOT_DIR:=$(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))

CPPFLAGS += -DDCOP_ROOT_DIR="\"$(ROOT_DIR)\""

CFLAGS += -Wall -g

LIBS = -lpthread -llua

CSRC := $(wildcard *.c)

OBJ = $(CSRC:%.c=%.o)

EXE = dcop

.PHONY: all
all: build

.PHONY: build
build: $(OBJ)
	$(CC) $(CFLAGS) -o $(EXE) $(OBJ) $(LIBS)
	@echo
	@echo "*** BUILD COMPLETE ***"

%.o: %.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	rm -f $(OBJ)

