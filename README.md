# ProDOS-Utils
File management utilities for the ProDOS operating system on the Apple II

## Sortdir

*Sortdir* is a powerful utility for managing ProDOS directories.  It provides
a number of functions, all of which may be applied to an individual directory,
a directory tree or an entire volume:

  - Checking directory integrity and making repairs
  - Multi-level directory sort
  - Manipulating the case of filenames *
  - Manipulating the format of time and date information *
  - Zeroing free blocks

*Sortdir* is intended to help users migrate to the new ProDOS 2.5 release,
which is currently in alpha.  The code has been testing with ProDOS 2.5a8,
but should run on older versions of ProDOS.  The features marked with an
asterix (\*) above allow directory entries to be converted from the legacy
format to the new ProDOS 2.5 format, and vice versa.

ProDOS 2.5 releases may be obtained [here](https://prodos8.com/releases/prodos-25/)

### System Requirements

*Sortdir* requires an enhanced Apple //e, //c or IIgs with 128KB of memory.
It should run on all versions of ProDOS, but is intended for use with
ProDOS 2.5.

### Quickstart - Test Disk Image

Download the disk image `sortdir.po`.  This is a bootable 143KB (Disk \]\[)
ProDOS 2.5 disk image which includes `SORTDIR.SYSTEM`, ready-to-run.

### Build Instructions

If you want to build *Sortdir* (and perhaps contribute to the code!), you
will require the [`cc65`](https://github.com/cc65/cc65) C cross compiler for
6502.

I also use [Cadius](https://github.com/mach-kernel/cadius) for copying
`sortdir.system#ff0000` to an Apple II disk image.

On a Linux system, you should be able to build by simply invoking `make`.

### How to Run `SORTDIR.SYSTEM`

`SORTDIR.SYSTEM` is a ProDOS system file, which means it loads at address
$2000.  It is possible to launch this program using any application or
utility that can launch ProDOS systm files, such as Bitsy Bye, which is
included with ProDOS 2.5.

It is also possible to start the program from the ProDOS `BASIC.SYSTEM`
prompt, using the normal syntax `-/PATH/TO/SORTDIR.SYSTEM`.  Since the volume
name of the disk image provided is `/P8.2.5`, the following command can be
used to start the program: `-/P8.2.5/SORTDIR.SYSTEM`.

When launching `SORTDIR.SYSTEM` from the BASIC prompt it is also possible to
specify command line parameters.  See below for more information.  If no
command line options are provided then *Sortdir* will present an interactive
user interface.

If *Sortdir* is started from a launcher other than `BASIC.SYSTEM`, there is
no way to pass command line options, so the interactive user interface will
be used.

Because *Sortdir* uses all of the system memory it reboots the system on
exit.  (It is not possible to return to BASIC because the workspace has been
overwritten.)

### Interactive User Interface

![](/Screenshots/Interactive.png)

TODO

### Command Line Options

![](/Screenshots/BASIC_Launch.png)

TODO

### Understading the Display

![](/Screenshots/Running.png)

TODO

### Command Line Options

![](/Screenshots/BASIC_Launch.png)

TODO

### Directory Check and Repair

TODO

### Directory Sort

TODO

### Filename Case Change

TODO

### Date and Time Format

This allows the format of the modification time and creation time fields in
ProDOS directories to be converted from the legacy ProDOS format (ProDOS <2.5)
to the new date and time formats introduced in ProDOS 2.5.  These new formats
extend the range of dates that may be represented, in a backwards-compatible
manner.  *Sortdir* also allows conversion from the new ProDOS 2.5 date and
time format back to the legacy format.

### Zeroing Free Blocks

Not yet implemented in ProDOS-8 version.


### Directory Check and Repair

TODO

### Directory Sort

TODO

### Filename Case Change

TODO

### Date and Time Format

This allows the format of the modification time and creation time fields in
ProDOS directories to be converted from the legacy ProDOS format (ProDOS <2.5)
to the new date and time formats introduced in ProDOS 2.5.  These new formats
extend the range of dates that may be represented, in a backwards-compatible
manner.  *Sortdir* also allows conversion from the new ProDOS 2.5 date and
time format back to the legacy format.

### Zeroing Free Blocks

Not yet implemented in ProDOS-8 version.


