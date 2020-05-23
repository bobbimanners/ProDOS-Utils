######################################################################
# Makefile for Sortdir, using cc65
# Bobbi, 2020
# GPL v3+
######################################################################

# Adjust these to match your site installation
CC65DIR = ~/Personal/Development/cc65
CC65BINDIR = $(CC65DIR)/bin
CC65LIBDIR = $(CC65DIR)/lib
CC65INCDIR = $(CC65DIR)/include
CA65INCDIR = $(CC65DIR)/asminc

all: sortdir.po sortdir.system\#ff0000

clean:
	rm -f *.s *.o *.map sortdir

sortdir.o: sortdir.c
	$(CC65BINDIR)/cc65 -I $(CC65INCDIR) -t apple2enh -D A2E -o sortdir.s sortdir.c
	$(CC65BINDIR)/ca65 -I $(CA65INCDIR) -t apple2enh sortdir.s

sortdir.system\#ff0000: sortdir.o
	$(CC65BINDIR)/ld65 -m sortdir.map -o sortdir.system\#ff0000 -C apple2enh-system.cfg sortdir.o $(CC65LIBDIR)/apple2enh.lib

sortdir.po: sortdir.system\#ff0000
	cadius deletefile sortdir.po /p8.2.5/sortdir.system
	cadius addfile sortdir.po /p8.2.5 sortdir.system\#ff0000

