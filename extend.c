/*
 * Simple program to modify the volume directory (block 2) of a ProDOS
 * volume to make the directory extensible to more than four blocks (51
 * entries) when using ProDOS 2.5 or later.  This program can also revert
 * a volume with an extensible volume directory back to fixed-size directory
 * provided the directory is still exactly 4 blocks in length.
 *
 * Bobbi
 * March 2020
 */

#include <prodos.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>

char buf[512];
char str[128]; /* Should be enough */

int main(int argc, char *argv[]) {
	puts("ProDOS 2.5+ Volume Directory Patching Utility");
	puts("---------------------------------------------");
	fputs("\nEnter device number> ", stdout);
	int dev;
	scanf("%u", &dev);
	BlockRec br;
	br.blockDevNum = dev;
	br.blockDataBuffer = buf;
	br.blockNum = 2;
	READ_BLOCK(&br);
	int rc = toolerror();
	if (rc) {
		printf("Block read failed, err=%x\n", rc);
		exit(255);
	}
	if ((buf[0x14] == 0) && (buf[0x15] == 0)) {
		puts("Fixed-size (4 block, 51 entry) volume directory");
		printf("Change to ProDOS 2.5+ extensible volume "
		       "directory ('yes' to confirm)?");
		scanf("%s", str);
		if (strcasecmp(str, "yes") == 0) {
			buf[0x14] = 4;
			buf[0x15] = 0;
			WRITE_BLOCK(&br);
			int rc = toolerror();
			if (rc) {
				printf("Block write failed, err=%x\n", rc);
				exit(255);
			}
			puts("** Changed to extensible **");
		} else
			puts("No change made.");
	} else {
		printf("Extensible volume directory, currently %u blocks\n",
		       buf[0x14] + 256U * buf[0x15]);
		if ((buf[0x14] == 4) && (buf[0x15] == 0)) {
			printf("Revert to classic ProDOS fixed-size volume "
			       "directory ('yes' to confirm)?");
			scanf("%s", str);
			if (strcasecmp(str, "yes") == 0) {
				buf[0x14] = 0;
				buf[0x15] = 0;
				WRITE_BLOCK(&br);
				int rc = toolerror();
				if (rc) {
					printf("Block write failed, err=%x\n",
					       rc);
					exit(255);
				}
				puts("** Changed to fixed-size **");
			} else
				puts("No change made.");
		}
	}
	return 0;
}

