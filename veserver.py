#!/usr/bin/env python3

#
# Bobbi 2020
#
# Alternative server for ADTPro's VEDRIVE.SYSTEM
# Virtual Ethernet Drive for Apple II / ProDOS
#
# See https://www.adtpro.com/protocolv1.html
#

import socket
import time

IP = "::"
PORT = 6502
FILE1 = "virtual-1.po"
FILE2 = "virtual-2.po"
BLKSZ = 512

# vt100 colour codes for pretty printing
GRN = '\033[92m'
RED = '\033[91m'
ENDC = '\033[0m'

pd25 = True   # Set to True for ProDOS 2.5+ clock driver, False otherwise

packet = 1    # Sent packet counter

#
# Get date/time bytes
#
def getDateTimeBytes(pd25):
    t = time.localtime()
    dt = []
    if pd25:
        # ProDOS 2.5+
        word1 = 2048 * t.tm_mday + 64 * t.tm_hour + t.tm_min
        word2 = 4096 * (t.tm_mon + 1) + t.tm_year
    else:
        # Legacy ProDOS <2.5
        word1 = t.tm_mday + 32 * t.tm_mon + 512 * (t.tm_year - 2000)
        word2 = t.tm_min + 256 * t.tm_hour
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
# Read block with date/time update
#
def read3(sock, addr, drive, d):
    global packet
    global pd25

    blknum = d[2] + 256 * d[3]
    print('{0}{1:05d}{2} '.format(GRN, blknum, ENDC), end='', flush=True)
    b = blknum * BLKSZ

    if d[1] == 0x03:
       file = FILE1
    else:
       file = FILE2

    err = False
    try:
        with open(file, 'rb') as f:
            f.seek(b)
            block = f.read(BLKSZ)
    except:
        err = True

    dt = getDateTimeBytes(pd25)
    l = []
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

    cs = 0
    for i in range (0, BLKSZ):
        cs = appendbyte(l, block[i], cs)

    # Signal read errors by responding with incorrect checksum
    if err:
        cs += 1

    appendbyte(l, cs, cs)         # Checksum for datablock

    b = sock.sendto(bytearray(l), addr)
    #print('Sent {} bytes to {}'.format(b, addr))

#
# Write block
#
def write(sock, addr, drive, d):
    global packet

    blknum = d[2] + 256 * d[3]
    print('{0}{1:05d}{2} '.format(RED, blknum, ENDC), end='', flush=True)
    b = blknum * BLKSZ

    if d[1] == 0x02:
       file = FILE1
    else:
       file = FILE2

    cs = 0
    for i in range (0, BLKSZ):
         cs ^= d[i+5]

    err = False
    if cs == d[517]:
        try:
            with open(file, 'r+b') as f:
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
    appendbyte(l, packet & 0xff, 0)  # Packet number
    packet += 1
    appendbyte(l, 0xc5, 0)     # "E"
    appendbyte(l, d[1], 0)     # 0x02 or 0x04
    appendbyte(l, d[2], 0)     # Block num LSB
    appendbyte(l, d[3], 0)     # Block num MSB
    appendbyte(l, cs, 0)       # Checksum of datablock
    b = sock.sendto(bytearray(l), addr)
    #print('Sent {} bytes to {}'.format(b, addr))


print("VEServer v0.7 alpha")
if pd25:
    print("ProDOS 2.5+ Clock Driver")
else:
    print("Legacy ProDOS Clock Driver")

with socket.socket(socket.AF_INET6, socket.SOCK_DGRAM) as s:
    s.bind((IP, PORT))
    print("veserver - listening on UDP port {}".format(PORT))

    while True:
        data, address = s.recvfrom(1024)
        #print('Received {} bytes from {}'.format(len(data), address))
        if (data[0] == 0xc5):
            if (data[1] == 0x03):
                read3(s, address, 1, data)
            elif (data[1] == 0x05):
                read3(s, address, 2, data)
            elif (data[1] == 0x02):
                write(s, address, 1, data)
            elif (data[1] == 0x04):
                write(s, address, 2, data)

