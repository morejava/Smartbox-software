# ;
# Makefile:
###############################################################################
#  Modbus TCP server library
#  - listens to Modbus TCP requests from local or remote clients
#  - performs requested read or write operations via specific callback functions
#
###############################################################################

DYN_VERS_MAJ=0
DYN_VERS_MIN=1

VERSION=$(DYN_VERS_MAJ).$(DYN_VERS_MIN)
DESTDIR=/usr
PREFIX=/local

STATIC=libmbsrv.a
DYNAMIC=libmbsrv.so.$(VERSION)

#DEBUG	= -g -O0
DEBUG	= -O2
CC	= gcc
INCLUDE	= -I.
DEFS	= -D_GNU_SOURCE
PKGCONF = `pkg-config --libs --cflags libmodbus`
CFLAGS	= $(DEBUG) $(DEFS) $(PKGCONF) -Wformat=2 -Wall -Winline $(INCLUDE) -pipe -fPIC 

LIBS    =

# Should not alter anything below this line
###############################################################################

SRC	=	modbustcp_server_lib.c

OBJ	=	$(SRC:.c=.o)

all:		$(DYNAMIC)

static:		$(STATIC)

$(STATIC):	$(OBJ)
	@echo "[Link (Static)]"
	@ar rcs $(STATIC) $(OBJ)
	@ranlib $(STATIC)
#	@size   $(STATIC)

$(DYNAMIC):	$(OBJ)
	@echo "[Link (Dynamic)]"
	@$(CC) -shared -Wl,-soname,libmbsrv.so -o libmbsrv.so.$(VERSION) -lrt $(OBJ)

.c.o:
	@echo [Compile] $<
	@$(CC) -c $(CFLAGS) $< -o $@

.PHONEY:	clean
clean:
	@echo "[Clean]"
	@rm -f $(OBJ) $(OBJ_I2C) *~ core tags Makefile.bak libmbsrv.*

.PHONEY:	tags
tags:	$(SRC)
	@echo [ctags]
	@ctags $(SRC)


.PHONEY:	install-headers
install-headers:
	@echo "[Install Headers]"
	@install -m 0755 -d		$(DESTDIR)$(PREFIX)/include
	@install -m 0644 modbustcp_server_lib.h		$(DESTDIR)$(PREFIX)/include

.PHONEY:	install
install:	$(DYNAMIC) install-headers
	@echo "[Install Dynamic Lib]"
	@install -m 0755 -d					$(DESTDIR)$(PREFIX)/lib
	@install -m 0755 libmbsrv.so.$(VERSION)			$(DESTDIR)$(PREFIX)/lib/libmbsrv.so.$(VERSION)
	@ln -sf $(DESTDIR)$(PREFIX)/lib/libmbsrv.so.$(VERSION)	$(DESTDIR)/lib/libmbsrv.so
	@ldconfig

.PHONEY:	install-static
install-static:	$(STATIC) install-headers
	@echo "[Install Static Lib]"
	@install -m 0755 -d		$(DESTDIR)$(PREFIX)/lib
	@install -m 0755 libmbsrv.a	$(DESTDIR)$(PREFIX)/lib

.PHONEY:	uninstall
uninstall:
	@echo "[UnInstall]"
	@rm -f $(DESTDIR)$(PREFIX)/include/modbustcp_server_lib.h
	@rm -f $(DESTDIR)$(PREFIX)/lib/libmbsrv.*
	@ldconfig


# DO NOT DELETE

modbustcp_server_lib.o: modbustcp_server_lib.h
 
