# Makefile for brandy under NetBSD and Linux

# Use the following to generate your app.c:
# examples/bin2c /path/to/basicprog src/app.c

# Then, build brandyapp with this makefile.

CC = gcc
LD = gcc
AR = ar
ADDFLAGS = ${BRANDY_BUILD_FLAGS}

include build/git.mk

#CFLAGS = -g -DDEBUG $(shell sdl-config --cflags)  -DUSE_SDL -DDEFAULT_IGNORE -DBRANDYAPP -Wall $(GITFLAGS) $(ADDFLAGS)
#CFLAGS = -g $(shell sdl-config --cflags)  -DUSE_SDL -DDEFAULT_IGNORE -DBRANDYAPP -Wall $(GITFLAGS) $(ADDFLAGS)
CFLAGS = -O3 $(shell sdl-config --cflags)  -DUSE_SDL -DDEFAULT_IGNORE -DBRANDYAPP -Wall $(GITFLAGS) $(ADDFLAGS)

LDFLAGS +=

LIBS = -lm $(shell sdl-config --libs) -ldl -lrt -lX11

SRCDIR = src

OBJ = $(SRCDIR)/variables.o $(SRCDIR)/tokens.o $(SRCDIR)/graphsdl.o \
	$(SRCDIR)/strings.o $(SRCDIR)/statement.o $(SRCDIR)/stack.o \
	$(SRCDIR)/miscprocs.o $(SRCDIR)/mainstate.o $(SRCDIR)/lvalue.o \
	$(SRCDIR)/keyboard.o $(SRCDIR)/iostate.o $(SRCDIR)/heap.o \
	$(SRCDIR)/functions.o $(SRCDIR)/fileio.o $(SRCDIR)/evaluate.o \
	$(SRCDIR)/errors.o $(SRCDIR)/mos.o $(SRCDIR)/editor.o \
	$(SRCDIR)/convert.o $(SRCDIR)/commands.o $(SRCDIR)/brandy.o \
	$(SRCDIR)/assign.o $(SRCDIR)/net.o $(SRCDIR)/mos_sys.o \
	$(SRCDIR)/soundsdl.o $(SRCDIR)/app.o

SRC = $(SRCDIR)/variables.c $(SRCDIR)/tokens.c $(SRCDIR)/graphsdl.c \
	$(SRCDIR)/strings.c $(SRCDIR)/statement.c $(SRCDIR)/stack.c \
	$(SRCDIR)/miscprocs.c $(SRCDIR)/mainstate.c $(SRCDIR)/lvalue.c \
	$(SRCDIR)/keyboard.c $(SRCDIR)/iostate.c $(SRCDIR)/heap.c \
	$(SRCDIR)/functions.c $(SRCDIR)/fileio.c $(SRCDIR)/evaluate.c \
	$(SRCDIR)/errors.c $(SRCDIR)/mos.c $(SRCDIR)/editor.c \
	$(SRCDIR)/convert.c $(SRCDIR)/commands.c $(SRCDIR)/brandy.c \
	$(SRCDIR)/assign.c $(SRCDIR)/net.c $(SRCDIR)/mos_sys.c \
	$(SRCDIR)/soundsdl.c $(SRCDIR)/app.c

brandyapp:	$(OBJ)
	$(LD) $(LDFLAGS) -o brandyapp $(OBJ) $(LIBS)

include build/depends.mk

.c.o:
	$(CC) $(CFLAGS) $< -c -o $@

clean:
	rm -f $(SRCDIR)/*.o brandyapp

all:	brandyapp
