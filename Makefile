ROOT_DIR:=$(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))

ifeq ($(shell test -e /usr/include/lua.h && echo -n yes), yes)
	LUA_LIBRARY=-llua
else
	LUA_INCLUDE=-I/usr/include/lua5.1
	LUA_LIBRARY=-llua5.1
endif

CPPFLAGS += -DDCOP_ROOT_DIR="\"$(ROOT_DIR)\"" $(LUA_INCLUDE)

ifdef DEBUG_NATIVE_CONSTRAINTS
	CPPFLAGS += -DDEBUG_NATIVE_CONSTRAINTS
endif

include ../sniper/sniper-6.1/config/buildconf.makefile

CC=$(SNIPER_CC)

CFLAGS += $(SNIPER_CFLAGS) -Wall -g -std=gnu11

LD=$(SNIPER_LD)

LDFLAGS += $(SNIPER_LDFLAGS)

LIBS = -lm $(LUA_LIBRARY) -ldl -lpthread

CSRC := $(wildcard *.c)

OBJ = $(CSRC:%.c=%.o)

EXE = dcop

DEPDIR := .d
$(shell mkdir -p $(DEPDIR) >/dev/null)

DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.Td

POSTCOMPILE = @mv -f $(DEPDIR)/$*.Td $(DEPDIR)/$*.d && touch $@

.PHONY: all
all: run-script build

.PHONY: build
build: $(EXE)

$(EXE): $(OBJ)
	$(LD) $(CFLAGS) -o $(EXE) $(OBJ) $(LIBS) $(LDFLAGS)
	@echo
	@echo "*** BUILD COMPLETE ***"

%.o: %.c
%.o: %.c $(DEPDIR)/%.d
	$(CC) -c $(DEPFLAGS) $(CPPFLAGS) $(CFLAGS) -o $@ $<
	$(POSTCOMPILE)

$(DEPDIR)/%.d: ;
.PRECIOUS: $(DEPDIR)/%.d

include $(wildcard $(patsubst %,$(DEPDIR)/%.d,$(basename $(CSRC))))

.PHONY: clean
clean:
	rm -f $(OBJ)

.PHONY: run-script
run-script: run-dcop

run-dcop: run-dcop.template
	cp run-dcop.template run-dcop
	sed -i -e 's/^SNIPER_ROOT=".*"$$/SNIPER_ROOT="$(subst /,\/,$(SNIPER_ROOT))"/' run-dcop
	sed -i -e 's/^DCOP_ROOT=".*"$$/DCOP_ROOT="$(subst /,\/,$(ROOT_DIR))"/' run-dcop
	chmod u+x run-dcop

.PHONY: distclean
distclean: clean
	rm -f dcop
	rm -f run-dcop
	rm -rf $(DEPDIR)

