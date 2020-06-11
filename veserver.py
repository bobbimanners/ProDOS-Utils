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

IP = "::"
PORT = 6502
FILE1 = "virtual-1.po"
FILE2 = "virtual-2.po"
BLKSZ = 512

packet = 1

# Append byte b to list l
# Return updated checksum
def appendbyte(l, b, csin):
    l.append(b)
    return csin ^ b

#
# Read block with date/time update
#
def read3(sock, addr, drive, d):
    global packet
    l = []
    appendbyte(l, packet & 0xff, 0)  # Packet number
    packet += 1
    cs = appendbyte(l, 0xc5, 0)   # "E"
    cs = appendbyte(l, d[1], cs)  # 0x03 or 0x05
    cs = appendbyte(l, d[2], cs)  # Block num LSB
    cs = appendbyte(l, d[3], cs)  # Block num MSB
    cs = appendbyte(l, 0, cs)     # TODO: Date/time
    cs = appendbyte(l, 0, cs)     # TODO: Date/time
    cs = appendbyte(l, 0, cs)     # TODO: Date/time
    cs = appendbyte(l, 0, cs)     # TODO: Date/time
    appendbyte(l, cs, cs)         # Checksum

    blknum = d[2] + 256 * d[3]
    print('R{0:05d} '.format(blknum), end='', flush=True)
    b = blknum * BLKSZ
    if d[1] == 0x03:
       file = FILE1
    else:
       file = FILE2
    with open(file, 'rb') as f:
        f.seek(b)
        block = f.read(BLKSZ)

    cs = 0
    for i in range (0, BLKSZ):
        cs = appendbyte(l, block[i], cs)
    appendbyte(l, cs, cs)

    b = sock.sendto(bytearray(l), addr)
    #print('Sent {} bytes to {}'.format(b, addr))

#
# Write block
#
def write(sock, addr, drive, d):
    global packet
    l = []
    appendbyte(l, packet & 0xff, 0)  # Packet number
    packet += 1
    cs = appendbyte(l, 0xc5, 0)   # "E"
    cs = appendbyte(l, d[1], cs)  # 0x02 or 0x04
    cs = appendbyte(l, d[2], cs)  # Block num LSB
    cs = appendbyte(l, d[3], cs)  # Block num MSB
    cs = appendbyte(l, d[517], cs)  # Block num MSB

    blknum = d[2] + 256 * d[3]
    print('W{0:05d} '.format(blknum), end='', flush=True)
    b = blknum * BLKSZ

    if d[1] == 0x02:
       file = FILE1
    else:
       file = FILE2
    with open(file, 'r+b') as f:
        f.seek(b)
        for i in range (0, BLKSZ):
            f.write(bytes([d[i+5]]))

    b = sock.sendto(bytearray(l), addr)
    #print('Sent {} bytes to {}'.format(b, addr))


with socket.socket(socket.AF_INET6, socket.SOCK_DGRAM) as s:
    s.bind((IP, PORT))
    print("veserver - listening on UDP port ", PORT)
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

