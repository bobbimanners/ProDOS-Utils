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

When *Sortdir* is launched without any command line parameters, a very
primitive interactive user interface is shown, as seen above.  The program
asks questions sequentially.  If you make a mistake and want to go back,
entering the `^` (caret) character will go back to the previous question.

When in doubt, the `-` (minus) character is mapped to the most conservative
choice for each option.

In the example shown above, the user has requested that the `/gno/usr` 
tree be processed (ie: `/gno/usr` and all directories underneath it in
the heirarchy.)  Two levels of directory sorting are to be performed,
first by name (in ascending order) and then to sort the directories
to the bottom.  No conversion of filename case will be performed, but
modification and creation time dates will be updated to the new ProDOS 2.5+
format.

The following prompts are presented in order:

  - *Path of starting directory*  Enter an absolute or relative path here.
    The directory operations will start in this directory (unless the
    'whole volume' option is selected, in which directory operations will
    begin in the volume directory of the of the volume which contains the
    directory specified here.)
  - *What to process ...* There are three options:
    - `-` - Only operate on the specified directory.
    - `r` - Operate recursively, descending the tree from the specified
      directory.
    - `v` - Operate on the entire volume, descending the tree from the volume
      directory.
  - *Multi-Level directory sort ...* Here you can enter up to four levels of
    directory sorting.  For each level, the following choices are available:
    - `n` - Sort by filename in ascending alphabetical order (A-Z).
    - `N` - Sort by filename in descending alphabetical order (Z-A).
    - `i` - Sort by filename in ascending alphabetical order (A-Z) in a case
            insensitive manner.
    - `I` - Sort by filename in descending alphabetical order (Z-A) in a case
            insensitive manner.
    - `d` - Sort in ascending order of modification time/date.
    - `D` - Sort in descending order of modification time/date.
    - `t` - Sort in ascending order of file type (considered as an integer)
    - `T` - Sort in descending order of file type (considered as an integer)
    - `f` - Sort in directories ("folders") to the top.
    - `F` - Sort in directories ("folders") to the bottom.
    - `b` - Sort in ascending order of file size in blocks.
    - `B` - Sort in descending order of file size in blocks.
    - `e` - Sort in ascending order of file size in bytes (ie: EOF position).
    - `E` - Sort in descending order of file size in bytes (ie: EOF position).
    - `-` - Entering `-` (minus) will end the entry of sort options and move
            on to the next section.
  - *Filename case conversion ...*
    - `-` - No filename conversion.  Leave them as-is.
    - `l` - Convert filenames to lower case `example.txt`
    - `u` - Convert filenames to upper case `EXAMPLE.TXT`
    - `i` - Convert filenames to initial case `Example.txt`
    - `c` - Convert filenames to camel case `Example.Txt`
  - *On-disk date format conversion ...*
    - `-` - No date/time conversion.  Leave them as-is.
    - `n` - Convert modification and creation date/time to new ProDOS 2.5+
            format.
    - `o` - Convert modification and creation date/time to legacy ProDOS
            format.
  - *Attempt to fix errors? ...*
    - `-` - Never attempt to fix errors. Just report them.
    - `?` - Every time a correctable error is encountered, prompt.
    - `a` - Always fix correctable errors.  Use this with caution!
  - *Allow writing to disk? ...*
    - `-` - Do not write changes to disk. This is useful to dry run the
            settings to see what will happen.
    - `w` - Write changes to disk.


### Command Line Options

![](/Screenshots/BASIC_Launch.png)

ProDOS 2.5 introduces support for passing command line parameters when
starting a `.SYSTEM` file.  If no command line parameters are passed
the the interactive user interface is presented (see previous section.)

The following command line syntax is supported:

```
sortdir [-s xxx] [-n x] [-rDwcvVh] path

      Options: -s xxx  Directory sort options
               -n x    Filename upper/lower case options
               -d x    Date format conversion options
               -f x    Fix mode
               -r      Recursive descent
               -D      Whole-disk mode (implies -r)
               -w      Enable writing to disk
               -c      Use create time rather than modify time
               -z      Zero free space
               -v      Verbose output
               -V      Verbose debugging output
               -h      This help
    
    -nx: Upper/lower case filenames, where x is:
      l  convert filenames to lower case           eg: read.me
      u  convert filenames to upper case           eg: READ.ME
      i  convert filenames to initial upper case   eg: Read.me
      c  convert filenames to camel case           eg: Read.Me
    
    -dx: Date/time on-disk format conversion, where x is:
      n  convert to new (ProDOS 2.5+) format
      o  convert to old (ProDOS 1.0-2.4.2, GSOS) format
    
    -sxxx: Dir sort, where xxx is a list of fields to sort
    on. The sort options are processed left-to-right.
      n  sort by filename ascending
      N  sort by filename descending
      i  sort by filename ascending - case insensitive
      I  sort by filename descending - case insensitive
      d  sort by modify (or create [-c]) date ascending
      D  sort by modify (or create [-c]) date descending
      t  sort by type ascending
      T  sort by type descending
      f  sort folders (directories) to top
      F  sort folders (directories) to bottom
      b  sort by blocks used ascending
      B  sort by blocks used descending
      e  sort by EOF position ascending
      E  sort by EOF position descending
    
    -fx: Fix mode, where x is:
      ?  prompt for each fix
      n  never fix
      y  always fix (be careful!)
```
    
For example `sortdir -rw -snf /foo` will sort the tree rooted at directory
`/foo` first by name (ascending), then sort directories to the top, and will
write the sorted directory to disk.


### Understanding the Display

![](/Screenshots/Running.png)

For each directory processed, *Sortdir* performs the following steps:

  - Interate through the directory, checking the integrity of the directory
    structure and that of each of the active directory entries.  For each
    entry the type is displayed (`Dir` - directory, `Seed` - seedling file,
    `Sapl` - sapling file, `Tree` - tree file, `Fork` - GSOS file with
    resource fork.  The number of blocks belonging to each entry displayed,
    followed by `[ OK ]` if no errors were found.  If errors were found
    they are printed out below the directory entry in question.
  - If sorting was requested, the sorted directory is displayed. This second
    listing will reflect any filename case conversions or time/date format
    conversions.  For each file the type, filename, number of blocks, EOF
    position in bytes and date time are displayed.  Normally the modification
    date/time is shown, but this may be switched to the creation time if the
    `-c` command line option is used. This listing also allows the on-disk
    format of the date/time fields to be determined as follows: the new-style
    ProDOS 2.5+ format is indentied by showing an asterisk to the right of the
    date/time.
  - If writing to disk is enabled then a message is shown confirming the
    updated directory has been written back to disk.  If writing to disk is
    not enabled, which is the default, a warning message is displayed.

### Directory Check and Repair

TODO

### Directory Sort

TODO

### Filename Case Change

ProDOS 2.5 supports mixed-case filenames rather than the uppercase only
filenames supported by previous ProDOS versions.  This is done in a backwards
compatible manner (using the 'version' and 'minversion' fields as a bitmap)
so that ProDOS 2.5 filesystems will appear to contain upper-case only
filenames when viewed using an earlier ProDOS version.

*Sortdir* provides convenient options for manipulating the case of filenames.
Four options are currently provided:

  - *Upper Case* - `MY.EXAMPLE.FILE`
  - *Lower Case* - `my.example.file`
  - *Initial Case* - `My.example.file`
  - *Camel Case* - `My.Example.File`

### Date and Time Format

This allows the format of the modification time and creation time fields in
ProDOS directories to be converted from the legacy ProDOS format (ProDOS <2.5)
to the new date and time formats introduced in ProDOS 2.5.  These new formats
extend the range of dates that may be represented, in a backwards-compatible
manner.  *Sortdir* also allows conversion from the new ProDOS 2.5 date and
time format back to the legacy format.

### Zeroing Free Blocks

Not yet implemented in ProDOS-8 version.


