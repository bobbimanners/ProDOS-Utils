# VEServer - Virtual Ethernet Drive Server for Apple II

## Purpose

VEServer is a very simple virtual disk server for an Apple II equipped with
a compatible ethernet card (such as the Uthernet II.)  VEServer can run on
Linux, Mac or Windows, provided Python v3 is installed.  It is intended to be
used with the Apple II client program `VEDRIVE.SYSTEM` which is included as
part of the [ADTPro](https://github.com/ADTPro/adtpro) disk imaging program.

ADTPro's `VEDRIVE.SYSTEM` program is designed to communicate with the ADTPro
server, which is a multi-platform Java program.  VEServer can be used in
place of the ADTPro server for this purpose.  VEServer is a much smaller and
simpler program, so it is easier to modify and better suited to being run as
a system service.

## Principle of Operation

VEServer uses IPv6 and listens on UDP port 6502 for incoming datagrams from
`VEDRIVE.SYSTEM`.  There are two types of supported request:

  - Read disk block and obtain updated system date and time
  - Write disk block

Two simulated drives are supported, backed up by disk files in `.po` "ProDOS
Order".  These may be 143K or 800K floppy disk images or any volume up to the
ProDOS limit of 32MB.

VEServer can provide system date and time in a similar manner to an Apple II
clock such as Thunderclock or No Slot Clock.  Because the legacy ProDOS date
and time format will run out in a few years time, ProDOS 2.5 introduces a new
date and time format.  VEServer uses the legacy format by default.  If using
ProDOS 2.5 please specify the `--prodos25` flag to use the new format.

## Command Line Options

There are only a few options:

 - `-h`, `--help` - Display brief usage information.
 - `-p`, `--prodos25` - Use new ProDOS 2.5 date/time format (see above.)
 - `-1 FNAME`, `--disk1=FNAME` - Specify filename for disk 1 image. 
 - `-2 FNAME`, `--disk2=FNAME` - Specify filename for disk 1 image. 

## Running in a Shell

You can just run `veserver.py` directly, for example
`./veserver.py --prodos25 -1 /home/woz/hd32_1.po -2 /home/woz/hd32_2.po`.

When run in a shell, each block number read or written is logged, and vt100
escape codes are used to colourize the output.  Reads are shown in green and
writes in red.  If a block number is prefixed by a `+` symbol this indicates
that VEServer believes this is a duplicate request, which is usually caused by
UDP packet loss.  If a block number is shown prefixed by `X`, this indicates
a checksum failure (not seen in normal operation.)

## Running as a System Service using Systemd

A sample Systemd unit file `veserver.service` is provided.  This has been
tested on Raspbian (Raspberry Pi) but may need modifications for other
distributions of Linux.  On most systems you just need to edit the
`veserver.service` file to reflect the path where `veserver.py` is installed
and copy it to `/lib/systemd/system`.

Once the service file has been installed, you can control the `veserver`
service as follows:

 - `systemctl start veserver`
 - `systemctl status veserver`

The reads and writes are logged to the system log and may be seen using the
following command:

 - `systemctl stop veserver`

Rather than using colour to indicate reads and writes the letter 'R' or 'W' is
shown before the block number in the log.

