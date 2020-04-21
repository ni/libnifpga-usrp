#
# Makefile for NiFlexRio and NiFpga
#
# Copyright (c) 2016 National Instruments
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#

CC	= $(CROSS_COMPILE)gcc
CXX	= $(CROSS_COMPILE)g++
CPP	= $(CROSS_COMPILE)cpp

CXXFLAGS += \
	-Wall -Wextra \
	-fPIC \
	-fvisibility=hidden \
	-std=c++17 \
	-pthread \
	-I"../include" \
	-Wold-style-cast \

TARGETS := libNiFpga.so lvbitx2dtso

.PHONY: release
release: CXXFLAGS += -O2 -DNDEBUG
release: $(TARGETS)

.PHONY: debug
debug: CXXFLAGS += -DDEBUG -g -rdynamic -O0
debug: $(TARGETS)

.PHONY: valgrind
valgrind: CXXFLAGS += -DENABLE_VALGRIND
valgrind: $(TARGETS)

NIFPGA_OBJECTS := \
	Bitfile.o \
	DeviceFile.o \
	DeviceInfo.o \
	Fifo.o \
	FifoInfo.o \
	FileLock.o \
	NiFpga.o \
	RegisterInfo.o \
	ResourceInfo.o \
	Session.o \
	ErrnoMap.o \
	SysfsFile.o \
	Type.o \
	libb64/cdecode.o \
	dtgen.o \

libNiFpga.so: $(NIFPGA_OBJECTS) .libNiFpga.so.symalias .libNiFpga.so.map
	@echo '  LD    ' $@
	@$(CXX) -Wl,-soname,$@ -Wl,-z,defs -Wl,@.libNiFpga.so.symalias -Wl,--version-script=.libNiFpga.so.map -shared -pthread $(LDFLAGS) $(NIFPGA_OBJECTS) -o $@

lvbitx2dtso: lvbitx2dtso.o Bitfile.o Type.o RegisterInfo.o FifoInfo.o ResourceInfo.o libb64/cdecode.o dtgen.o
	$(CXX) -o $@ $^

dependency = $(dir $1)$(patsubst %.o,.%.d,$(notdir $1))

.PHONY: clean
clean:
	@echo '  CLEAN   .'
	@rm -f $(TARGETS) $(NIFPGA_OBJECTS)
	@rm -f $(foreach o,$(NIFPGA_OBJECTS),$(call dependency,$(o)))
	@rm -rf .libNiFpga.so.symalias .libNiFpga.so.map

%.o: %.cpp
	@echo '  CXX   ' $@
	@$(CXX) $(CXXFLAGS) $(CPPFLAGS) -MP -MMD -MF $(call dependency,$@) -c -o $@ $<

INSTALL = install
PREFIX = /usr/local
DESTDIR =
LIBDIR = $(PREFIX)/lib
INCLUDEDIR = $(PREFIX)/include

.PHONY: install
install: $(TARGETS)
	$(INSTALL) -d $(DESTDIR)$(LIBDIR)
	$(INSTALL) libNiFpga.so $(DESTDIR)$(LIBDIR)
	$(INSTALL) libniflexrio.so $(DESTDIR)$(LIBDIR)
	$(INSTALL) -d $(DESTDIR)$(INCLUDEDIR)
	$(INSTALL) ../include/NiFpga.h $(DESTDIR)$(INCLUDEDIR)
	$(INSTALL) ../include/niflexrio.h $(DESTDIR)$(INCLUDEDIR)

.PHONY: cppcheck
cppcheck:
	cppcheck -I../include/ -I../driver/include --enable=all .

# Generate aliases for NiFpgaDll_ -> NiFpga_
# This is intended for binary compatibility with libraries built against the
# "real" libNiFpga.so
.libNiFpga.so.symalias: libNiFpga.exports
	@sed -n 's/NiFpga_\(\w\+\)/-defsym=NiFpgaDll_\1=&/p' $< > $@

.libNiFpga.so.map: libNiFpga.exports
	@echo '{' > $@
	@echo '    global:' >> $@
	@sed '/^NiFpga_/p; s/NiFpga_/NiFpgaDll_/' $< | sed 's/.*/        &;/' >> $@
	@echo '    local:' >> $@
	@echo '        *;' >> $@
	@echo '};' >> $@

-include .*.d */.*.d

