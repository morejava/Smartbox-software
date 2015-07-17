#
# Makefile
# gcc sensord.c -o sensord -lmbsrv -ldht `pkg-config --libs --cflags libmodbus`
#

RM = \rm -f
PROG = sensord
BINPATH=/usr/local/bin

# DEBUG	= -O2
CC	= gcc
INCLUDE	= -I.
CFLAGS	= $(DEBUG) $(INCLUDE) -Wformat=2 -Wall -Winline  -pipe -fPIC 

LSWI = -L
LIBS =  $(LSWI)/usr/local/lib

# List of objects files for the dependency
OBJS_DEPEND= -lmbsrv -ldht -lsht `pkg-config --libs --cflags libmodbus`

# OPTIONS = --verbose

all: target

target: Makefile
	@echo "--- Compile and Linking all object files to create the whole file: $(PROG) ---"
	$(CC) $(PROG).c -o $(PROG) $(CFLAGS) $(OBJS_DEPEND) $(OPTIONS)
	@echo ""

clean :
	@echo "---- Cleaning all object files in all the directories ----"
	$(RM) $(PROG)
	@echo "" 

install : target
	@echo "---- Install binaries ----"
	cp $(PROG) $(BINPATH)