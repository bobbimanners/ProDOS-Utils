#!/usr/bin/env python3

###########################################################################
# Bobbi June 2020
#
# Alternative server for ADTPro's VEDRIVE.SYSTEM
# Virtual Ethernet Drive for Apple II / ProDOS
#
# See https://www.adtpro.com/protocolv1.html
#
###########################################################################

pd25 = False   # Default to old-style date/time --prodos25 to use new format
file1 = "/home/bobbi/virtual-1.po"  # Disk image drive 1 --disk1 to override
file2 = "/home/bobbi/virtual-2.po"  # Disk image drive 2 --disk2 to override
serial_port = None  # Serial port to use instead of ethernet
baud_rate = 115200  # Baud rate for serial mode

###########################################################################

import socket
import time
import os
import getopt
import sys

IP = "::"
PORT = 6502
BLKSZ = 512

# vt100 colour codes for pretty printing
BLK = '\033[90m'
RED = '\033[91m'
GRN = '\033[92m'
YEL = '\033[93m'
BLU = '\033[94m'
MAG = '\033[95m'
CYN = '\033[96m'
WHT = '\033[97m'
ENDC = '\033[0m'

# Globals
systemd = False # True if running under Systemd
packet = 1      # Sent packet counter
prevblk = -1    # Last block read/written
prevdrv = -1    # Last drive read/written
prevop = -1     # Last operation (read or write)
prevcs = -1     # Previous checksum
col = 0         # Used to control logging printout
skip1 = 0       # Bytes to skip over header (Drive 1)
skip2 = 0       # Bytes to skip over header (Drive 2)

#
# Get date/time bytes
#
def getDateTimeBytes():
    global pd25
    t = time.localtime()
    dt = []
    if pd25:
        # ProDOS 2.5+
        word1 = 2048 * t.tm_mday + 64 * t.tm_hour + t.tm_min
        word2 = 4096 * (t.tm_mon + 1) + t.tm_year
    else:
        # Legacy ProDOS <2.5
        word1 = t.tm_min + 256 * t.tm_hour
        word2 = t.tm_mday + 32 * t.tm_mon + 512 * (t.tm_year - 2000)
    dt.append(word1 & 0xff)
    dt.append((word1 & 0xff00) >> 8)
    dt.append(word2 & 0xff)
    dt.append((word2 & 0xff00) >> 8)
    return dt

#
# Append byte b to list l, return updated checksum
#
def appendbyte(l, b, csin):
    l.append(b)
    return csin ^ b


#
# Pretty print info about each request
#
def printinfo(drv, blknum, isWrite, isError, cs, filename):
    global systemd, prevblk, prevdrv, prevop, prevcs, col
    if drv != prevdrv:
        if systemd:
            print('\nDrive {} ({})'.format(drv, filename))
        else:
            print('\n{}Drive {} ({}){}'.format(BLU, drv, filename, ENDC))
        col = 0
    e = '+' if ((blknum == prevblk) and (drv == prevdrv) and (isWrite == prevop) and (cs == prevcs)) else ' '
    e = 'X' if isError else e
    if systemd:
        c = 'W' if isWrite else 'R'
        print(' {0}{1}{2:05d}'.format(e, c, blknum), end='', flush=True)
    else:
        c = RED if isWrite else GRN
        print('{0} {1}{2:05d}{3}'.format(c, e, blknum, ENDC), end='', flush=True)
    col += 1
    if col == 8:
        print('')
        col = 0
    prevblk = blknum
    prevdrv = drv
    prevop = isWrite
    prevcs = cs

#
# Augment filename by adding IP
# If filename is basename.ext and IP is 192.168.2.3
# will return basename-192.168.2.3.ext
#
def augment_filename(filename, ip):
    idx = filename.rfind(".")
    basename = filename[0 : idx]
    ext = filename[idx:]
    return basename + "-" + ip + ext

#
# See if augmented filename exists, if so return that name
# otherwise return the unaugmented name
#
def select_filename(filename, ip):
    if serial_port:
        return filename
    filename_with_ip = augment_filename(filename, ip)
    try:
        with open(filename_with_ip, 'r+b'):
            pass
    except:
        return filename
    return filename_with_ip

#
# Read block with date/time update
#
def read3(dataport, addr, ip, d):
    global packet

    d = dataport.recvmore(d, 3)

    if d[1] == 0x03:
       filename = file1
       drv = 1
       skip = skip1
    else:
       filename = file2
       drv = 2
       skip = skip2

    filename = select_filename(filename, ip)

    blknum = d[2] + 256 * d[3]

    err = False
    try:
        with open(filename, 'r+b') as f:
            b = blknum * BLKSZ + skip
            f.seek(b)
            block = f.read(BLKSZ)
    except:
        err = True

    dt = getDateTimeBytes()
    l = []
    if not serial_port:
        appendbyte(l, packet & 0xff, 0)  # Packet number
        packet += 1
    cs = appendbyte(l, 0xc5, 0)   # "E"
    cs = appendbyte(l, d[1], cs)  # 0x03 or 0x05
    cs = appendbyte(l, d[2], cs)  # Block num LSB
    cs = appendbyte(l, d[3], cs)  # Block num MSB
    cs = appendbyte(l, dt[0], cs) # Time of day LSB
    cs = appendbyte(l, dt[1], cs) # Time of day MSB
    cs = appendbyte(l, dt[2], cs) # Date LSB
    cs = appendbyte(l, dt[3], cs) # Date MSB
    appendbyte(l, cs, cs)         # Checksum for header

    # Signal read errors by responding with incorrect checksum
    if err:
        cs += 1
    else:
        cs = 0
        for i in range (0, BLKSZ):
            cs = appendbyte(l, block[i], cs)


    appendbyte(l, cs, cs)         # Checksum for datablock

    printinfo(drv, blknum, False, err, cs, filename)

    b = dataport.sendto(bytearray(l), addr)
    #print('Sent {} bytes to {}'.format(b, ip))

#
# Write block
#
def write(dataport, addr, ip, d):
    global packet

    d = dataport.recvmore(d, BLKSZ + 4)

    if d[1] == 0x02:
       filename = file1
       drv = 1
       skip = skip1
    else:
       filename = file2
       drv = 2
       skip = skip2

    filename = select_filename(filename, ip)

    cs = 0
    for i in range (0, BLKSZ):
         cs ^= d[i+5]

    blknum = d[2] + 256 * d[3]

    err = False
    if cs == d[517]:
        try:
            with open(filename, 'r+b') as f:
                b = blknum * BLKSZ + skip
                f.seek(b)
                for i in range (0, BLKSZ):
                    f.write(bytes([d[i+5]]))
        except:
            err = True         # Write error
    else:
        err == True            # Bad checksum

    # Signal write errors by responding with bad data checksum.
    # Use sender's checksum + 1, so there is never an inadvertent match.
    if err:
        cs = d[517] + 1

    l = []
    if not serial_port:
        appendbyte(l, packet & 0xff, 0)  # Packet number
        packet += 1
    appendbyte(l, 0xc5, 0)     # "E"
    appendbyte(l, d[1], 0)     # 0x02 or 0x04
    appendbyte(l, d[2], 0)     # Block num LSB
    appendbyte(l, d[3], 0)     # Block num MSB
    appendbyte(l, cs, 0)       # Checksum of datablock

    printinfo(drv, blknum, True, err, cs, filename)

    b = dataport.sendto(bytearray(l), addr)
    #print('Sent {} bytes to {}'.format(b, ip))

#
# See if file is a 2MG and, if so, that it contains .PO image
# Returns bytes to skip over header
#
def check2MG(filename):
    try:
        with open(filename, 'r+b') as f:
            hdr = f.read(16)
    except:
        return 0
    if (hdr[0] == 0x32) and (hdr[1] == 0x49) and (hdr[2] == 0x4d) and (hdr[3] == 0x47):
        print('** ' + filename + ' is a 2MG file **')
        if hdr[0x0c] != 0x01:
            print('** Warning NOT in ProDOS order **')
        return 64
    return 0

class DataPort:
    class Timeout(Exception):
        pass

    def __init__(self, serial_port, baud_rate):
        if serial_port:
            import serial  # Import locally to avoid hard dependency

            # Use a short timeout so that the protocol resets on desync. This
            # tends to happen if you reboot the client.
            self.impl = serial.Serial(
                port=serial_port, baudrate=baud_rate, bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE, stopbits=serial.STOPBITS_ONE,
                timeout=1, xonxoff=False, rtscts=True, dsrdtr=True
            )
            self.stream = True
            print("veserver - listening on serial port {}".format(serial_port))
        else:
            self.impl = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
            self.impl.bind((IP, PORT))
            self.stream = False
            print("veserver - listening on UDP port {}".format(PORT))

    def __enter__(self):
        self.impl.__enter__()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.impl.__exit__(exc_type, exc_val, exc_tb)

    def recvfrom(self, required_bytes):
        if self.stream:
            data = self.impl.read(required_bytes)
            if len(data) < required_bytes:
                raise DataPort.Timeout
            else:
                return data, None
        else:
            return self.impl.recvfrom(1024)

    def recvmore(self, data, remaining_bytes):
        if self.stream:
            return data + self.impl.read(remaining_bytes)
        else:
            return data

    def sendto(self, data, addr):
        if self.stream:
            return self.impl.write(data)
        else:
            return self.impl.sendto(data, addr)

def usage():
    print('usage: veserver [OPTION]...')
    print('  -h, --help               Show this help');
    print('  -p, --prodos25           Use ProDOS 2.5 date/time format');
    print('  -1 FNAME, --disk1=FNAME  Specify filename for disk 1 image');
    print('  -2 FNAME, --disk2=FNAME  Specify filename for disk 2 image');
    print('  -s PORT, --serial=PORT   Use a serial link instead of ethernet');
    print('  -b BAUD, --baud=BAUD     Baud rate for serial link');

#
# Entry point
#

# Check whether we are running under Systemd or not
if 'INVOCATION_ID' in os.environ:
    systemd = True

short_opts = "hp1:2:s:b:"
long_opts = ["help", "prodos25", "disk1=", "disk2=", "serial=", "baud="]
try:
    args, vals = getopt.getopt(sys.argv[1:], short_opts, long_opts)
except getopt.error as e:
    print (str(e))
    usage()
    sys.exit(2)

for a, v in args:
    if a in ('-h', '--help'):
        usage()
        sys.exit(0)
    elif a in ('-p', '--prodos25'):
        pd25 = True
    elif a in ('-1', '--disk1'):
        file1 = v
    elif a in ('-2', '--disk2'):
        file2 = v
    elif a in ('-s', '--serial'):
        serial_port = v
    elif a in ('-b', '--baud'):
        baud_rate = int(v)

print("VEServer v1.3")
if pd25:
    print("ProDOS 2.5+ Clock Driver")
else:
    print("Legacy ProDOS Clock Driver")

print("Disk 1: {}".format(file1))
skip1 = check2MG(file1)
print("Disk 2: {}".format(file2))
skip2 = check2MG(file2)

with DataPort(serial_port, baud_rate) as dataport:
    while True:
        try:
            data, address = dataport.recvfrom(2)
            ip = address[0]
            ip = ip[ip.rfind(":")+1:]
            #print('Received {} bytes from {}'.format(len(data), ip))
            if (data[0] == 0xc5):
                if (data[1] == 0x03) or (data[1] == 0x05):
                    read3(dataport, address, ip, data)
                elif (data[1] == 0x02) or (data[1] == 0x04):
                    write(dataport, address, ip, data)
        except DataPort.Timeout:
            pass


