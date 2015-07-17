#
# Top level Makefile for TeleGea custom software modules
#

MAKE=make

# List of objects files for the dependency
OBJS_DEPEND=modbus_tcp_client statusd sensord controld pulsecountd mbrtud

all: target

target: $(OBJS_DEPEND) Makefile

	@echo "---- Compiling an Linking all object files in all the directories ----"
	for number in $(OBJS_DEPEND) ; do \
	   $(MAKE) -C $$number ; \
	done
	@echo ""

clean : $(OBJS_DEPEND)
	@echo "---- Cleaning all object files in all the directories ----"
	for number in $(OBJS_DEPEND) ; do \
	   $(MAKE) -C $$number clean ; \
	done
	@echo ""

install : $(OBJS_DEPEND)
	@echo "---- Install all binaries ----"
	for number in $(OBJS_DEPEND) ; do \
	   $(MAKE) -C $$number install ; \
	done
	@echo ""