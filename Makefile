ROOT_DIR:=$(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))

CPPFLAGS += -DDCOP_ROOT_DIR="\"$(ROOT_DIR)\""

include ../sniper/sniper-6.1/config/buildconf.makefile

CC=$(SNIPER_CC)

CFLAGS += $(SNIPER_CFLAGS) -Wall -g

LDFLAGS += $(SNIPER_LDFLAGS)

LIBS = -lpthread -llua -ldl -lm

CSRC := $(wildcard *.c)

OBJ = $(CSRC:%.c=%.o)

EXE = dcop

.PHONY: all
all: build

.PHONY: build
build: $(OBJ)
	$(CC) $(CFLAGS) -o $(EXE) $(OBJ) $(LIBS) $(LDFLAGS)
	@echo
	@echo "*** BUILD COMPLETE ***"

%.o: %.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	rm -f $(OBJ)

