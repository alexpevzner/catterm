CC		= gcc 
CPPFLAGS	= -Wall -O2 -g
LIBS		=
LDFLAGS		=
OBJS		= \
    catterm.o
PROG		= catterm
ALL		= $(PROG)
PREFIX		= /usr/local

# Autodependencies
ifneq (.depend,$(wildcard .depend))
ALL = dep_then_all
endif

# Standard rules
all:	$(ALL)

dep_then_all:
	make dep
	make all

clean:
	rm -f .depend
	rm -f *.o core $(TESTS)
	rm -f *.so
	rm $(PROG)

install: $(PROG)
	cp $(PROG) $(PREFIX)/bin/$(PROG)
	strip $(PREFIX)/bin/$(PROG)

atnBuffer: atnBuffer.o test-atnBuffer.o
	$(CC) $(CPPFLAGS) -o $@ $+

$(PROG): $(OBJS)
	-ctags -R .
	$(CC) $(CPPFLAGS) -o $@ $+ $(LIBS) $(LDFLAGS)

# Autodependencies
dep:
	$(CC) $(CPPFLAGS) -M *.c >.depend	

ifeq (.depend,$(wildcard .depend))
include .depend
endif


