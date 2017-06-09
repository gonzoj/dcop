ROOT_DIR:=$(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))

CPPFLAGS += -DDCOP_ROOT_DIR="\"$(ROOT_DIR)\""

include ../sniper/sniper-6.1/config/buildconf.makefile

CC=$(SNIPER_CC)

CFLAGS += $(SNIPER_CFLAGS) -Wall -g

LD=$(SNIPER_LD)

LDFLAGS += $(SNIPER_LDFLAGS)

LIBS = -lm -llua -ldl -lpthread

CSRC := $(wildcard *.c)

OBJ = $(CSRC:%.c=%.o)

EXE = dcop

.PHONY: all
all: run-script build

.PHONY: build
build: $(OBJ)
	$(LD) $(CFLAGS) -o $(EXE) $(OBJ) $(LIBS) $(LDFLAGS)
	@echo
	@echo "*** BUILD COMPLETE ***"

%.o: %.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	rm -f $(OBJ)

.PHONY: run-script
run-script:
	sed -i -e 's/^SNIPER_ROOT=".*"$$/SNIPER_ROOT="$(subst /,\/,$(SNIPER_ROOT))"/' run-dcop
	sed -i -e 's/^DCOP_ROOT=".*"$$/DCOP_ROOT="$(subst /,\/,$(ROOT_DIR))"/' run-dcop

.PHONY: distclean
distclean: clean
	sed -i -e 's/^SNIPER_ROOT=".*"$$/SNIPER_ROOT=""/' run-dcop
	sed -i -e 's/^DCOP_ROOT=".*"$$/DCOP_ROOT=""/' run-dcop

