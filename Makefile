#
# Makefile.in for membox.
#
# This file is currently maintained manually. There is currently
# no generation out of automake. This will change as soon as we
# switch to our new autoconf/configure build process.
#

SHELL = /bin/bash

srcdir = .
top_srcdir = .

prefix = /home/emanus/testing/membox/build
exec_prefix = ${prefix}

bindir = ${exec_prefix}/bin
sbindir = ${exec_prefix}/sbin
libexecdir = ${exec_prefix}/libexec
datadir = ${datarootdir}
datarootdir = ${prefix}/share
sysconfdir = ${prefix}/etc
sharedstatedir = ${prefix}/com
localstatedir = ${prefix}/var
logdir = ${prefix}/logs
libdir = ${exec_prefix}/lib
infodir = ${datarootdir}/info
mandir = ${datarootdir}/man
includedir = ${prefix}/include
oldincludedir = /usr/include

docdir = ${datarootdir}/doc/${PACKAGE}

DESTDIR =

top_builddir = .

ACLOCAL = @ACLOCAL@
AUTOCONF = @AUTOCONF@
AUTOHEADER = @AUTOHEADER@

INSTALL = /usr/bin/install -c
INSTALL_PROGRAM = ${INSTALL} $(AM_INSTALL_PROGRAM_FLAGS)
INSTALL_DATA = ${INSTALL} -m 644
INSTALL_SCRIPT = ${INSTALL}
transform = s,x,x,

NORMAL_INSTALL = :
PRE_INSTALL = :
POST_INSTALL = :
NORMAL_UNINSTALL = :
PRE_UNINSTALL = :
POST_UNINSTALL = :
CC = gcc
MAKEINFO = @MAKEINFO@
PACKAGE = @PACKAGE@
RANLIB = ranlib
SHELL = /bin/bash
VERSION = cvs-
SUFFIX = 
LEX = @LEX@
PERL = @PERL@
YACC = @YACC@

# -v gives verbose output.
YFLAGS = -d -p ws_yy_

mkinstalldirs = $(SHELL) $(top_srcdir)/mkinstalldirs
CONFIG_HEADER = gw-membox-config.h
CONFIG_CLEAN_FILES =

LIBOBJS=
LIBSRCS=$(LIBOBJS:.o=.c)

LIBS= -L/home/emanus/project/kannel/lib/kannel -lgw -lwap -lgwlib -lssl -lrt -lresolv -lnsl -lm  -lpthread -lxml2 -L/usr/lib64 -lcrypto -lssl -L/home/emanus/project/amgx/lib -lamgxlib -lwritestatus -lboost_system
CFLAGS=-D_REENTRANT=1 -I. -Igw -Igwlib -g -O0 -I/home/emanus/project/kannel/include/kannel -g -O0 -D_XOPEN_SOURCE=600 -D_BSD_SOURCE -D_LARGE_FILES= -I/usr/include/libxml2 -I/usr/include/openssl -I/home/emanus/project/amgx/include
LDFLAGS=-L/home/emanus/local/lib

MKDEPEND=$(CC) $(CFLAGS) -MM

# Set this to something if you want all installed binaries to have a suffix.
# Version number is common.
suffix = $(SUFFIX)

#
# You probably don't need to touch anything below this, if you're just
# compiling and installing the software.
#

binsrcs = 
binobjs = $(binsrcs:.c=.o)
binprogs = $(binsrcs:.c=)

sbinsrcs = gw/membox.c 
progsrcs = $(sbinsrcs)
progobjs = $(progsrcs:.c=.o)
progs = $(progsrcs:.c=)
sbinprogs = $(sbinsrcs:.c=)

gwsrcs = $(wildcard gw/*.c)
gwobjs = $(gwsrcs:.c=.o)

gwlibsrcs = $(wildcard gwlib/*.c)
gwlibobjs = $(gwlibsrcs:.c=.o)

testsrcs = $(wildcard test/*.c)
testobjs = $(testsrcs:.c=.o)
testprogs = $(testsrcs:.c=)
tests = $(testprogs) $(wildcard test/*.sh)

rootobjs = README.membox

srcs = $(wildcard */*.c)
objs = $(srcs:.c=.o)
pres = $(srcs:.c=.i)

libs = libmembox.a

srcdirs = gw

.SUFFIXES: $(SUFFIXES) .c .i .o .lo .so

.c.o:
	$(CC) $(CFLAGS) -o $@ -c $<

.c.i:
	$(CC) $(CFLAGS) -o $@ -E $<

.c.lo:
	$(CC) $(CFLAGS) -o $@ -c $<
	
.lo.so:
	$(CC) -shared -o $@ $<
	
all: progs $(binprogs) $(testprogs) membox-config

progs: $(progs)
tests: $(testprogs)
pp: $(pres)

depend .depend: gw-membox-config.h
	for dir in $(srcdirs); do \
	for file in $$dir/*.c; do \
	    $(MKDEPEND) $$file -MT $$dir/`basename $$file .c`.o -MT $$dir/`basename $$file .c`.i; done; done > .depend  
include .depend

libmembox.a: $(gwobjs)
	ar rc libmembox.a $(gwobjs)
	$(RANLIB) libmembox.a

install: all
	$(INSTALL) -d $(DESTDIR)$(prefix)
	for prog in $(rootobjs); do \
		$(INSTALL) $$prog \
			$(DESTDIR)$(prefix)/`basename $$prog`$(suffix); \
	done
	$(INSTALL) -d $(DESTDIR)$(bindir)
	for prog in $(binprogs) membox-config; do \
		$(INSTALL) $$prog \
		    $(DESTDIR)$(bindir)/`basename $$prog`$(suffix); \
	done
	$(INSTALL) -d $(DESTDIR)$(sbindir)
	for prog in $(sbinprogs); do \
		$(INSTALL) $$prog \
		    $(DESTDIR)$(sbindir)/`basename $$prog`$(suffix); \
	done
	$(INSTALL) -d $(DESTDIR)$(includedir)/membox
	$(INSTALL) -d $(DESTDIR)$(sysconfdir)
	test -r $(DESTDIR)$(bindir)/membox-config || \
		ln -sf membox-config$(suffix) $(DESTDIR)$(bindir)/membox-config

clean:
	find . -name "*.o" -o -name "*.i" -o -name "*.a" | xargs rm -f
	rm -f core $(progs) $(pluginobjs) $(pluginobjsS) $(plugindsos)

distclean: clean
	rm -f Makefile config.cache config.log config.status config.nice .depend

$(progs): $(libs) $(progobjs)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(@:=).o  $(libs) $(LIBS)

$(binprogs): $(libs) $(binobjs)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(@:=).o $(libs) $(LIBS)

$(testprogs): $(testobjs) $(libs)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(@:=).o $(libs) $(LIBS)
		
membox-config: utils/foobar-config.sh Makefile
	./utils/foobar-config.sh "-I$(includedir)/kannel -I$(includedir)/membox -g -O0 -I/home/emanus/project/kannel/include/kannel -g -O0 -D_XOPEN_SOURCE=600 -D_BSD_SOURCE -D_LARGE_FILES= -I/usr/include/libxml2 -I/usr/include/openssl -I/home/emanus/project/amgx/include" \
		"-L$(libdir)/kannel -lgw -lwap -lgwlib  -L/home/emanus/project/kannel/lib/kannel -lgw -lwap -lgwlib -lssl -lrt -lresolv -lnsl -lm  -lpthread -lxml2 -L/usr/lib64 -lcrypto -lssl -L/home/emanus/project/amgx/lib -lamgxlib -lwritestatus -lboost_system" \
		"cvs-" > membox-config
	chmod 0755 membox-config
	
