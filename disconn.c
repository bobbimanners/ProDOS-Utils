//
// Very simple utility which just disconnects any drives mapped to slot 1
// Bobbi September 2020
//

#include <conio.h>
#include <stdint.h>
#include <stdio.h>

/*
 * Disconnect drives from slot 1
 */
void disconnect_slot1(void) {
  uint8_t i, j;
  uint8_t *devcnt = (uint8_t*)0xbf31; // Number of devices
  uint8_t *devlst = (uint8_t*)0xbf32; // Disk device numbers
  uint16_t *s0d1 = (uint16_t*)0xbf10; // s0d1 driver vector
  uint16_t *s1d1 = (uint16_t*)0xbf12; // s3d1 driver vector
  uint16_t *s1d2 = (uint16_t*)0xbf22; // s3d2 driver vector
  if (*s0d1 == *s1d2)
    goto s1d1;                        // No s1d2
  for (i = 0; i <= *devcnt; ++i)
    if ((devlst[i] & 0xf0) == 0x90) {
      for (j = i; j <= *devcnt; ++j)
        devlst[j] = devlst[j + 1];
      break;
    }
  *s1d2 = *s0d1;
  --(*devcnt);  
s1d1:
  if (*s0d1 == *s1d1)
    return;                           // No s1d1
  for (i = 0; i <= *devcnt; ++i)
    if ((devlst[i] & 0xf0) == 0x10) {
      for (j = i; j <= *devcnt; ++j)
        devlst[j] = devlst[j + 1];
      break;
    }
  *s1d1 = *s0d1;
  --(*devcnt);  
}

void main(void) {
  char c;
  printf("*********************************\n");
  printf("** Will disconnect any drives  **\n");
  printf("** mapped to slot 1            **\n");
  printf("*********************************\n\n");
  printf("  Press [D] to disconnect drives\n");
  printf("  Any other key to cancel\n");
  c = cgetc();
  if ((c == 'd') || (c == 'D')) {
    disconnect_slot1();
    printf("\n** Done.\n");
  } else {
    printf("\n** Cancelled.\n");
  }
}

