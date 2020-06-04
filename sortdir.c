/*
 * SORTDIR - for Apple II (ProDOS)
 *
 * Bobbi January-June 2020
 *
 * TODO: Enable free list functionality on ProDOS-8 using aux mem
 * TODO: Get both ProDOS-8 and GNO versions to build from this source
 * TODO: Trimming unused directory blocks
 *
 * Revision History
 * v0.50 Initial alpha release on GitHub. Ported from GNO/ME version.
 * v0.51 Made buf[] and buf2[] dynamic.
 * v0.52 Support for aux memory.
 * v0.53 Auto-sizing of filelist[] to fit available memory.
 * v0.54 Make command line argument handling a compile time option.
 * v0.55 Can use *all* of largest heap block for filelist[].
 * v0.56 Minor improvements to conditional compilation.
 * v0.57 Fixed bugs in aux memory allocation, memory zeroing bug.
 * v0.58 Fixed more bugs. Now working properly using aux memory.
 * v0.59 Moved creation of filelist[] into buildsorttable(). More bugfix.
 * v0.60 Modified fileent to be a union. Build it for each subsort. Saves RAM.
 * v0.61 Squeezed fileent to be a few bytes smaller. Fixed folder sort.
 * v0.62 Modified buildsorttable() to update existing filelist[].
 * v0.63 Made code work properly with #undef CHECK.
 * v0.64 Fixed overflow in file count (entries). Added check to auxalloc().
 * v0.65 Fixed length passed to AUXMOVE in copyaux().
 * v0.66 Modified to build sorted blocks on the fly rather than in aux memory.
 * v0.67 Fixed bug in v0.66 where garbage was written to end of directory.
 * v0.68 Cleaned up error msgs.
 * v0.69 Fixed support for drive number >2. (cc65 needs to be fixed too!)
 * v0.70 Changed sort options to support mtime & ctime. Improved UI a bit.
 * v0.71 Added support for allocating aux LC memory.
 */

//#pragma debug 9
//#pragma lint -1
//#pragma stacksize 16384
//#pragma memorymodel 0
//#pragma optimize -1      /* Disable stack repair code */

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
//#include <sys/stat.h>
//#include <orca.h>
//#include <gsos.h>
//#include <prodos.h>
#include <apple2enh.h>
#include <dio.h>
#include <conio.h> // For revers() and clrscr()

#define CHECK		/* Perform additional integrity checking */
#define SORT        /* Enable sorting code */
#undef FREELIST     /* Checking of free list */
#define AUXMEM      /* Auxiliary memory support on //e and up */
#undef CMDLINE      /* Command line option parsing */

#define NLEVELS 4	/* Number of nested sorts permitted */

typedef unsigned char uchar;
typedef unsigned int  uint;
typedef unsigned long ulong;

#define NMLEN 15	/* Length of filename */

/*
 * ProDOS directory header
 * See ProDOS-8 Tech Ref pp. 152
 */
struct pd_dirhdr {
	uchar	typ_len;
	char	name[NMLEN];
	char	reserved[8];
	uchar	ctime[4];
	uchar	vers;
	uchar	minvers;
	uchar	access;
	uchar	entlen;
	uchar	entperblk;
	uchar	filecnt[2];
	uchar	parptr[2];	/* Bitmap pointer in volume dir */
	uchar	parentry;	/* Total blocks LSB in volume dir */
	uchar	parentlen;	/* Total blocks MSB in volume dir */
};

/*
 * ProDOS file entry
 * See ProDOS-8 Tech Ref pp. 155
 */
struct pd_dirent {
	uchar	typ_len;
	char	name[NMLEN];
	uchar 	type;
	uchar	keyptr[2];
	uchar	blksused[2];
	uchar	eof[3];
	uchar	ctime[4];
	uchar	vers;
	uchar	minvers;
	uchar	access;
	uchar	auxtype[2];
	uchar	mtime[4];
	uchar	hdrptr[2];
};

#define BLKSZ 512      /* 512 byte blocks */
#define PTRSZ 4        /* 4 bytes of pointers at beginning of each blk */
#define ENTSZ 0x27     /* Normal ProDOS directory entry size */
#define ENTPERBLK 0x0d /* Normal ProDOS dirents per block */
#define FLSZ 8192      /* Bytes required for 64K block free-list */

/* Exit codes */
#define EXIT_SUCCESS    0
#define EXIT_BAD_ARG    1
#define EXIT_ALLOC_ERR  2
#define EXIT_FATAL_ERR  3

/*
 * Linked list of directory blocks read from disk
 * Directory block is stored in data[]
 */
struct block {
#ifdef AUXMEM
	char *data;               /* Original contents of block */
#else
	char data[BLKSZ];         /* Original contents of block */
#endif
	uint blocknum;            /* Block number on disk */
	struct block *next;
};

/*
 * Entry for array of filenames used by qsort()
 */
struct fileent {
	uchar blockidx;          /* Index of dir block (1,2,3 ...)  */
	uchar entrynum;	         /* Entry within the block */
	union {
		char  name[NMLEN-2]; /* Name converted to upper/lower case */
		char  datetime[12+1];/* Date/time as a yyyymmddhhmm string */
		uchar type;          /* ProDOS file type */
		uint  blocks;        /* Size in blocks */
		ulong eof;           /* EOF position in bytes */
	};
	/* NOTE: Because name is unique we do not need the order field to make
	 * the sort stable, so we can let the name buffer overflow by 2 bytes
	 */
	uint  order;             /* Hack to make qsort() stable */
};

/*
 * Entry for list of directory keyblocks to check
 */
struct dirblk {
	uint          blocknum;
	struct dirblk *next;
};

/*
 * Represents a date and time
 */
struct datetime {
	uint  year;
	uchar month;
	uchar day;
	uchar hour;
	uchar minute;
	uchar ispd25format;
};

/*
 * Globals
 */
#ifdef AUXMEM
#define STARTAUX1 0x0800 // 46K main aux block
#define ENDAUX1   0xbfff
#define STARTAUX2 0xd000 // 12K block in aux LC
#define ENDAUX2   0xffff
static char *auxp     = (char*)STARTAUX1;    /* For allocating aux main */
static char *auxp2    = (char*)STARTAUX2;    /* For allocating aux LC */
static char *auxlockp = (char*)STARTAUX1;    /* Aux mem protection */
#endif
#ifdef FREELIST
static uint totblks;                     /* Total # blocks on volume */
static uchar *freelist;                  /* Free-list bitmap */
static uchar *usedlist;                  /* Bit map of used blocks */
static uchar flloaded = 0;               /* 1 if free-list has been loaded */
#endif
static char currdir[NMLEN+1];            /* Name of current directory */
static struct block *blocks = NULL;      /* List of directory disk blocks */
static struct dirblk *dirs = NULL;       /* List of key blocks of subdirs */
static uint numfiles;                    /* Number of files in current dir */
static uint maxfiles;                    /* Size of filelist[] */
static uchar entsz;                      /* Bytes per file entry */
static uchar entperblk;                  /* Number of entries per block */
static uint errcount = 0;                /* Error counter */
static dhandle_t dio_hdl;                /* cc64 direct I/O handle */
static uchar dowholedisk = 0;            /* -D whole-disk option */
static uchar dorecurse = 0;              /* -r recurse option */
static uchar dowrite = 0;                /* -w write option */
static uchar doverbose = 0;              /* -v verbose option */
static uchar dodebug = 0;                /* -V very verbose option */
#ifdef FREELIST
static uchar dozero = 0;                 /* -z zero free blocks option */
#endif
static char sortopts[NLEVELS+1] = "";    /* -s:abc list of sort options */
static char caseopts[2] = "";            /* -c:x case conversion option */
static char fixopts[2] = "";             /* -f:x fix mode option */
static char dateopts[2] = "";            /* -d:x date conversion option */

// Allocated dynamically in main()
static char *buf;                        /* General purpose scratch buffer */
static char *buf2;                       /* General purpose scratch buffer */
static char *dirblkbuf;                  /* Used for reading directory blocks */
static struct fileent *filelist;         /* Used for qsort() */

/* Error messages */
static const char err_nomem[]    = "No memory!";
static const char err_noaux[]    = "No aux mem!";
static const char err_rdblk1[]   = "Can't read blk %u";
static const char err_rdblk2[]   = "Can't read blk %u ($%2x)";
static const char err_wtblk1[]   = "Can't write blk %u";
static const char err_wtblk2[]   = "Can't write blk %u ($%2x)";
static const char err_stype2[]   = "Bad storage type $%2x for %s";
static const char err_odir1[]    = "Can't open dir %s";
static const char err_rddir1[]   = "Can't read dir %s";
static const char err_rdpar[]    = "Can't read parent dir";
static const char err_sdname[]   = "Bad subdir name";
static const char err_entsz2[]   = "Bad entry size %u, should be %u";
static const char err_entblk2[]  = "Bad entries/blk %u, should be %u";
static const char err_parblk3[]  = "Bad parent %s %u, should be %u";
static const char err_hdrblk2[]  = "Bad hdr blk %u, should be %u";
static const char err_forksz3[]  = "%s fork size %u is wrong, should be %u";
static const char err_used2[]    = "Blks used %u is wrong, should be %u";
static const char err_many[]     = "Too many files to sort";
static const char err_count2[]   = "Filecount %u wrong, should be %u";
static const char err_nosort[]   = "Errors ... will not sort";
#ifdef FREELIST
static const char err_rdfl[]     = "Can't read free list";
static const char err_blfree1[]  = "In use blk %u is marked free";
static const char err_blfree2[]  = "%s blk %u is marked free";
static const char err_blused1[]  = "Unused blk %u is not marked free";
static const char err_blused2[]  = "%s blk %u used elsewhere";
#endif
static const char err_updsdir1[] = "Can't update subdir entry (%s)";
static const char err_invopt[]   = "Invalid %s option";
#ifdef CMDLINE
static const char err_usage[]    = "Usage error";
#endif
static const char err_80col[]    = "Need 80 cols";
static const char err_128K[]     = "Need 128K";


/* Prototypes */
#ifdef AUXMEM
void copyaux(char *src, char *dst, uint len, uchar dir);
char *auxalloc(uint bytes);
char *auxalloc2(uint bytes);
void lockaux(char *p);
void freeallaux(void);
#endif
void hline(void);
void hlinechar(char c);
void confirm(void);
void err(enum errtype severity, const char *fmt, ...);
void flushall(void);
int  readdiskblock(uchar device, uint blocknum, char *buf);
int  writediskblock(uchar device, uint blocknum, char *buf);
void fixcase(char *in, char *out, uchar minvers, uchar vers, uchar len);
void lowercase(char *p, uchar len, uchar *minvers, uchar *vers);
void uppercase(char *p, uchar len, uchar *minvers, uchar *vers);
void initialcase(uchar mode, char *p, uchar len, uchar *minvers, uchar *vers);
void firstblk(char *dirname, uchar *device, uint *block);
void readdatetime(uchar time[4], struct datetime *dt);
void writedatetime(uchar pd25, struct datetime *dt, uchar time[4]);
uint askfix(void);
#ifdef FREELIST
int  readfreelist(uchar device);
int  isfree(uint blk);
void markfree(uint blk);
void marknotfree(uint blk);
int  isused(uint blk);
void markused(uint blk);
void checkblock(uint blk, char *msg);
#endif
#ifdef CHECK
int  seedlingblocks(uchar device, uint keyblk, uint *blkcnt);
int  saplingblocks(uchar device, uint keyblk, uint *blkcnt);
int  treeblocks(uchar device, uint keyblk, uint *blkcnt);
int  forkblocks(uchar device, uint keyblk, uint *blkcnt);
int  subdirblocks(uchar device, uint keyblk, struct pd_dirent *ent,
                  uint blocknum, uint blkentries, uint *blkcnt);
#endif
void enqueuesubdir(uint blocknum, uint subdiridx);
int  readdir(uint device, uint blocknum);
#ifdef SORT
void buildsorttable(char s, uchar callidx);
int  cmp_name_asc(const void *a, const void *b);
int  cmp_name_desc(const void *a, const void *b);
int  cmp_name_asc_ci(const void *a, const void *b);
int  cmp_name_desc_ci(const void *a, const void *b);
int  cmp_datetime_asc(const void *a, const void *b);
int  cmp_datetime_desc(const void *a, const void *b);
int  cmp_type_asc(const void *a, const void *b);
int  cmp_type_desc(const void *a, const void *b);
int  cmp_dir_beg(const void *a, const void *b);
int  cmp_dir_end(const void *a, const void *b);
int  cmp_blocks_asc(const void *a, const void *b);
int  cmp_blocks_desc(const void *a, const void *b);
int  cmp_eof_asc(const void *a, const void *b);
int  cmp_eof_desc(const void *a, const void *b);
int  cmp_noop(const void *a, const void *b);
void sortlist(char s);
#endif
void printlist(void);
uint blockidxtoblocknum(uint idx);
void copydirblkptrs(uint blkidx);
void copydirent(uint srcblk, uint srcent, uint dstblk, uint dstent, uint device);
void sortblock(uint device, uint dstblk);
int  writedir(uchar device);
void freeblocks(void);
void subtitle(char *s);
void interactive(void);
void processdir(uint device, uint blocknum);
#ifdef FREELIST
void checkfreeandused(uchar device);
void zeroblock(uchar device, uint blocknum);
void zerofreeblocks(uchar device, uint freeblks);
#endif
#ifdef CMDLINE
void usage(void);
void parseargs(void);
#endif

enum errtype {WARN, NONFATAL, FATAL, FATALALLOC, FATALBADARG, FINISHED};

#ifdef AUXMEM

/* Aux memory copy routine */
#define FROMAUX 0
#define TOAUX   1
void copyaux(char *src, char *dst, uint len, uchar dir) {
	char **a1 = (char**)0x3c;
	char **a2 = (char**)0x3e;
	char **a4 = (char**)0x42;
	*a1 = src;
	*a2 = src + len - 1; // AUXMOVE moves length+1 bytes!!
	*a4 = dst;
	if (dir == TOAUX) {
		__asm__("sec");       // Copy main->aux
		__asm__("jsr $c311"); // AUXMOVE
	} else {
		__asm__("clc");       // Copy aux->main
		__asm__("jsr $c311"); // AUXMOVE
	}
}

/* Extremely simple aux memory allocator */
char *auxalloc(uint bytes) {
	char *p = auxp;
	auxp += bytes;
	if (auxp > (char*)ENDAUX1)
		return auxalloc2(bytes);
	return p;
}

/* Extremely simple aux memory allocator */
char *auxalloc2(uint bytes) {
	char *p = auxp2;
	auxp2 += bytes;
	if (auxp2 > (char*)ENDAUX2)
		err(FATAL, err_noaux);
	return p;
}

/* Lock aux memory below address provided */
void lockaux(char *p) {
	auxlockp = p;
}

/* Free all aux memory above lock address */
void freeallaux() {
	auxp = (char*)auxlockp;
	auxp2 = (char*)STARTAUX2;
}

#endif

/* Horizontal line */
void hline(void) {
	hlinechar('-');
}

void hlinechar(char c) {
	uint i;
	for (i = 0; i < 80; ++i)
		putchar(c);
}


/****************************************************************************/
/* LANGUAGE CARD BANK 2 0xd400-x0dfff 3KB                                   */
/****************************************************************************/
#pragma code-name (push, "LC")

void confirm() {
	puts("[Press any key to restart system]");
	getchar();
}


/*
 * Display error message
 */
void err(enum errtype severity, const char *fmt, ...) {
	va_list v;
	uint rv = 0;
	rebootafterexit(); // Necessary if we were called from BASIC
	if (severity == FINISHED) {
		hline();
		if (errcount == 0)
			printf("DONE - no errors found.\n");
		else
			printf("DONE - %u errors\n", errcount);
		hline();
		confirm();
		exit(EXIT_SUCCESS);
	}
	++errcount;

	switch (severity) {
	case FATAL:
		rv = EXIT_FATAL_ERR;
	case FATALALLOC:
		rv = EXIT_ALLOC_ERR;
	case FATALBADARG:
		rv = EXIT_BAD_ARG;
	}

	fputs(((rv > 0) ? "  ** " : "  "), stdout);
	va_start(v, fmt);
	vprintf(fmt, v);
	va_end(v);
	putchar('\n');
	if (rv > 0) {
		printf("Stopping after %u errors\n", errcount);
		confirm();
		exit(rv);
	}
}

/*
 * Disable GSOS block cache and flush any unwritten changes
 */
void flushall(void) {
//	short ff[2];
//	ResetCacheGS(0); /* Disable block caching */
//	ff[0] = 1;
//	ff[1] = 0;
//	FlushGS(ff);
}

/*
 * Read block from disk using ProDOS call
 * buf must point to buffer with at least 512 bytes
 */
int readdiskblock(uchar device, uint blocknum, char *buf) {
	int rc;
#ifdef CHECK
#ifdef FREELIST
	if (flloaded)
		if (isfree(blocknum))
			err(NONFATAL, err_blfree1, blocknum);
#endif
#endif
//	BlockRec br;
//	br.blockDevNum = device;
//	br.blockDataBuffer = buf;
//	br.blockNum = blocknum;
//	READ_BLOCK(&br);
//	int rc = toolerror();
//	if (rc) {
//		err(FATAL, "Blk read failed, err=%x", rc);
//		return -1;
//	}
	rc = dio_read(dio_hdl, blocknum, buf);
	if (rc)
		err(FATAL, err_rdblk2, blocknum, rc);
	return 0;
}

/*
 * Write block from disk using ProDOS call
 * buf must point to buffer with at least 512 bytes
 */
int writediskblock(uchar device, uint blocknum, char *buf) {
	int rc;
	if ((strcmp(currdir, "LIB") == 0) ||
	    (strcmp(currdir, "LIBRARIES") == 0)) {
		printf("Not writing lib dir %s\n", currdir);
		return 0;
	}
	flushall();
//	DIORecGS dr;
//	dr.pCount = 6;
//	dr.devNum = device;
//	dr.buffer = buf;
//	dr.requestCount = BLKSZ;
//	dr.startingBlock = blocknum;
//	dr.blockSize = BLKSZ;
//	DWriteGS(&dr);
//	if (dr.transferCount != BLKSZ) {
//		err(FATAL, "Blk write failed");
//		return -1;
//	}
	rc = dio_write(dio_hdl, blocknum, buf);
	if (rc)
		err(FATAL, err_wtblk2, blocknum, rc);
	return 0;
}

/*
 * Uses the vers and minvers fields of the directory entry
 * as a bitmap representing which characters are upper and which are
 * lowercase
 */
void fixcase(char *in, char *out, uchar minvers, uchar vers, uchar len) {
	uint i;
	uchar idx = 0;
	if (!(vers & 0x80)) {
		for (idx = 0; idx < NMLEN; ++idx)
			out[idx] = in[idx];
		out[len] = '\0';
		return;
	}
	vers <<= 1;
	for (i = 0; i < 7; ++i) {
		out[idx] = ((vers & 0x80) ? tolower(in[idx]) : in[idx]);
		++idx;
		vers <<= 1;
	}
	for (i = 0; i < 8; ++i) {
		out[idx] = ((minvers & 0x80) ? tolower(in[idx]) : in[idx]);
		++idx;
		minvers <<= 1;
	}
	out[len] = '\0';
}

/*
 * Convert filename pointed to by p into lower case (which is recorded
 * as a bitmap in the vers and minvers fields.
 */
void lowercase(char *p, uchar len, uchar *minvers, uchar *vers) {
	uint i;
	uchar idx = 0;
	*vers = 0x01;
	*minvers = 0x00;
	for (i = 0; i < 7; ++i) {
		*vers <<= 1;
		if ((idx < len) && isalpha(p[idx++]))
			*vers |= 0x01;
	}
	for (i = 0; i < 8; ++i) {
		*minvers <<= 1;
		if ((idx < len) && isalpha(p[idx++]))
			*minvers |= 0x01;
	}
}

/*
 * Convert filename pointed to by p into upper case (which is recorded
 * as a bitmap in the vers and minvers fields.
 */
void uppercase(char*, uchar, uchar *minvers, uchar *vers) {
	*vers = 0x00;
	*minvers = 0x00;
}

/*
 * Convert filename pointed to by p into to have first letter capitalized
 * (which is recorded as a bitmap in the vers and minvers fields.
 * If mode = 0 then just uppercase the initial char ("Read.me")
 * otherwise camel-case the name ("Read.Me")
 */
void initialcase(uchar mode, char *p, uchar len, uchar *minvers, uchar *vers) {
	uint i;
	uchar idx = 0;
	uchar capsflag = 1;
	*vers = 0x01;
	*minvers = 0x00;
	for (i = 0; i < 7; ++i) {
		*vers <<= 1;
		if ((idx < len) && isalpha(p[idx++]))
			if (!capsflag)
				*vers |= 0x01;
		if ((mode == 1) && !isalpha(p[idx-1]))
			capsflag = 1;
		else
			capsflag = 0;
	}
	for (i = 0; i < 8; ++i) {
		*minvers <<= 1;
		if ((idx < len) && isalpha(p[idx++]))
			if (!capsflag)
				*minvers |= 0x01;
		if ((mode == 1) && !isalpha(p[idx-1]))
			capsflag = 1;
		else
			capsflag = 0;
	}
}

//segment "extra";

/*
 * Read the first block of a directory and deduce the device ID and block
 * number of the first block of the directory.
 */
void firstblk(char *dirname, uchar *device, uint *block) {
	struct pd_dirhdr *hdr;
	struct pd_dirent *ent;
	int fp;
	uint len;
	uint parentblk, parententry, parententlen;
	uchar slot, drive;
	uchar *lastdev = (uchar*)0xbf30; /* Last device accessed by ProDOS */

	fp = open(dirname, O_RDONLY);
	if (!fp) {
		err(FATAL, err_odir1, dirname);
		goto ret;
	}

	len = read(fp, buf, BLKSZ);
	if (len != BLKSZ) {
		err(FATAL, err_rddir1, dirname);
		goto ret;
	}

//	struct stat st;
//	if (stat(dirname, &st) == -1)
//		err(FATAL, "Can't stat %s", dirname);
//
//	if (!S_ISDIR(st.st_mode))
//		err(FATAL, "%s is not a directory", dirname);
//
//	*device = st.st_dev;

	/*
	 * lastdev is in the following format:
	 * ProDOS 2.5+  DSSS00DD (supports drives 1-8 for each slot)
	 * ProDOS 2.x   DSSS0000 (supports drives 1-2 for each slot)
	 */
	*device = *lastdev;
	slot = (*lastdev & 0x70) >> 4;
	drive = ((*lastdev & 0x80) >> 7) + ((*lastdev & 0x03) << 1) + 1;
	clrscr();
	printf("[Slot %u, Drive %u]\n", slot, drive);
	*device = slot + (drive - 1) * 8;
	dio_hdl = dio_open(*device);

	hdr = (struct pd_dirhdr*)(buf + PTRSZ);

	/* Detect & handle volume directory */
	if ((hdr->typ_len & 0xf0) == 0xf0) {
		*block = 2;
		goto ret;
	}

#ifdef CHECK
	if ((hdr->typ_len & 0xf0) != 0xe0) {
		err(NONFATAL, err_stype2, hdr->typ_len & 0xf0, "dir");
		goto ret;
	}
#endif

	/* Handle subdirectory */
	parentblk    = hdr->parptr[0] + 256U * hdr->parptr[1];
	parententry  = hdr->parentry;
	parententlen = hdr->parentlen;

	/* Read parent directory block */
	if (readdiskblock(*device, parentblk, buf) == -1)
		err(FATAL, err_rdpar);

	ent = (struct pd_dirent *)(buf + PTRSZ + (parententry-1) * parententlen);

	*block = ent->keyptr[0] + 256U * ent->keyptr[1];

ret:
	if (fp)
		close(fp);
}

/****************************************************************************/
/* END OF LANGUAGE CARD BANK 2 0xd400-x0dfff 3KB SEGMENT                    */
/****************************************************************************/
#pragma code-name (pop)

/*
 * Parse mtime or ctime fields and populate the fields of the datetime struct
 * Supports the legacy ProDOS date/time format as used by ProDOS 1.0->2.4.0
 * and also the new format introduced with ProDOS 2.5.
 */
void readdatetime(uchar time[4], struct datetime *dt) {
	uint d = time[0] + 256U * time[1];
	uint t = time[2] + 256U * time[3];
	if (!(t & 0xe000)) {
		/* ProDOS 1.0 to 2.4.2 date format */
		dt->year   = (d & 0xfe00) >> 9;
		dt->month  = (d & 0x01e0) >> 5;
		dt->day    = d & 0x001f;
		dt->hour   = (t & 0x1f00) >> 8;
		dt->minute = t & 0x003f;
		dt->ispd25format = 0;
		if (dt->year < 40) /* See ProDOS-8 Tech Note 48 */
			dt->year += 2000;
		else
			dt->year += 1900;
	} else {
		/* ProDOS 2.5.0+ */
		dt->year   = t & 0x0fff;
		dt->month  = ((t & 0xf000) >> 12) - 1;
		dt->day    = (d & 0xf800) >> 11;
		dt->hour   = (d & 0x07c0) >> 6; 
		dt->minute = d & 0x003f;
		dt->ispd25format = 1;
	}
}

/*
 * Write the date and time stored in struct datetime in ProDOS on disk format,
 * storing the bytes in array time[].  If pd25 is 0 then use legacy format
 * (ProDOS 1.0-2.4.2) otherwise use the new date and time format introduced
 * with ProDOS 2.5
 */
void writedatetime(uchar pd25, struct datetime *dt, uchar time[4]) {
	uint d, t;
	if (pd25 == 0) {
		/* ProDOS 1.0 to 2.4.2 date format */
		uint year = dt->year;
		if (year > 2039)		/* 2039 is last year */
			year = 2039;
		if (year < 1940)		/* 1940 is first year */
			year = 1940;
		if (year >= 2000)
			year -= 2000;
		if (year >= 1900)
			year -= 1900;
		d = (year << 9) | (dt->month << 5) | dt->day; 
		t = (dt->hour << 8) | dt->minute;
	} else {
		/* ProDOS 2.5.0+ */
		t = ((dt->month + 1) << 12) | dt->year;
		d = (dt->day << 11) | (dt->hour << 6) | dt->minute; 
	}
	time[0] = d & 0xff;
	time[1] = (d >> 8) & 0xff;
	time[2] = t & 0xff;
	time[3] = (t >> 8) & 0xff;
}

/*
 * Determine whether or not to perform a fix
 * Return 0 not to perform fix, 1 to perform fix
 */
uint askfix(void) {
	if (strlen(fixopts) == 0)
		return 0;
	switch (fixopts[0]) {
	case '?':
		fputs("Fix (y/n)? ", stdout);
		if (tolower(getchar()) == 'y')
			return 1;
		return 0;
	case 'y':
		return 1;
	default:
		return 0;
	}
}

#ifdef FREELIST

/*
 * Read the free list
 */
int readfreelist(uchar device) {
	uint i, flblk, flsize;
	char *p;
	freelist = (uchar*)malloc(FLSZ);
	if (!freelist)
		err(FATALALLOC, err_nomem);
	bzero(freelist, FLSZ);
	usedlist = (uchar*)malloc(FLSZ);
	if (!usedlist)
		err(FATALALLOC, err_nomem);
	bzero(usedlist, FLSZ);
	markused(0); /* Boot block */
	markused(1); /* SOS boot block */
	if (readdiskblock(device, 2, buf) == -1) {
		err(NONFATAL, err_rdblk1, 2);
		return -1;
	}
	flblk = buf[0x27] + 256U * buf[0x28];
	totblks = buf[0x29] + 256U * buf[0x2a];
	if (doverbose)
		printf("Volume has %u blocks\n", totblks);
	flsize = totblks / 4096U;
	if ((flsize % 4096) >0)
		++flsize;
	p = (char*)freelist;
	for (i = 0; i < flsize; ++i) {
		markused(flblk);
		if (readdiskblock(device, flblk++, p) == -1) {
			err(NONFATAL, err_rdfl);
			return -1;
		}
		p += BLKSZ;
	}
	flloaded = 1;
	return 0;
}

/*
 * Determine if block blk is free or not
 */
int isfree(uint blk) {
	uint idx = blk / 8;
	uint bit = blk % 8;
	return (freelist[idx] << bit) & 0x80 ? 1 : 0;
}

/*
 * Mark a block as free
 */
void markfree(uint blk) {
	uint idx = blk / 8;
	uint bit = blk % 8;
	freelist[idx] |= (0x80 >> bit);
}

/*
 * Mark a block as not free
 */
void marknotfree(uint blk) {
	uint idx = blk / 8;
	uint bit = blk % 8;
	freelist[idx] &= ~(0x80 >> bit);
}

/*
 * Determine if block blk is used or not
 */
int isused(uint blk) {
	uint idx = blk / 8;
	uint bit = blk % 8;
	return (usedlist[idx] << bit) & 0x80 ? 1 : 0;
}

/*
 * Mark a block as used
 */
void markused(uint blk) {
	uint idx = blk / 8;
	uint bit = blk % 8;
	usedlist[idx] |= (0x80 >> bit);
}

/*
 * Perform all the operations to check a block which is used by
 * a directory or file.  Complains if the block is on the free-list
 * and also if we have encountered this block in a previous file or dir.
 */
void checkblock(uint blk, char *msg) {
	if (isfree(blk))
		err(WARN, err_blfree2, msg, blk);
	if (isused(blk))
		err(WARN, err_blused2, msg, blk);
	markused(blk);
}

#endif

#ifdef CHECK

/*
 * Count the blocks in a seedling file
 */
#ifdef FREELIST
int seedlingblocks(uchar, uint keyblk, uint *blkcnt) {
	checkblock(keyblk, "Data");
#else
int seedlingblocks(uchar, uint, uint *blkcnt) {
#endif
	*blkcnt = 1;
	return 0;
}

/*
 * Count the blocks in a sapling file
 */
int saplingblocks(uchar device, uint keyblk, uint *blkcnt) {
	uint i, p;
#ifdef FREELIST
	checkblock(keyblk, "Data");
#endif
	if (readdiskblock(device, keyblk, buf) == -1) {
		err(NONFATAL, err_rdblk1, keyblk);
		return -1;
	}
	*blkcnt = 1;
	for (i = 0; i < 256; ++i) {
		p = buf[i] + 256U * buf[i+256];
		if (p) {
#ifdef FREELIST
			checkblock(p, "Data");
#endif
			++(*blkcnt);
		}
	}
	return 0;
}

/*
 * Count the blocks in a tree file
 */
int treeblocks(uchar device, uint keyblk, uint *blkcnt) {
	uint i, p, b;
#ifdef FREELIST
	checkblock(keyblk, "Tree index");
#endif
	if (readdiskblock(device, keyblk, buf2) == -1) {
		err(NONFATAL, err_rdblk1, keyblk);
		return -1;
	}
	*blkcnt = 1;
	for (i = 0; i < 256; ++i) {
		p = buf2[i] + 256U * buf2[i+256];
		if (p) {
			if (saplingblocks(device, p, &b) == 0)
				*blkcnt += b;
			else
				return -1;
		}
	}
	return 0;
}

/*
 * Count the blocks in a GSOS fork file
 * See http://1000bit.it/support/manual/apple/technotes/pdos/tn.pdos.25.html
 */
int forkblocks(uchar device, uint keyblk, uint *blkcnt) {
	uint count, d_blks, r_blks, d_keyblk, r_keyblk;
	uchar d_type, r_type;
#ifdef FREELIST
	checkblock(keyblk, "Fork key");
#endif
	if (readdiskblock(device, keyblk, buf) == -1) {
		err(NONFATAL, err_rdblk1, keyblk);
		return -1;
	}
	*blkcnt = 1;

	d_type = buf[0x00];
	d_keyblk = buf[0x01] + 256U * buf[0x02];
	d_blks = buf[0x03] + 256U * buf[0x04];
	r_type = buf[0x100];
	r_keyblk = buf[0x101] + 256U * buf[0x102];
	r_blks = buf[0x103] + 256U * buf[0x104];

	/* Data fork */
	switch (d_type) {
	case 0x1:
		/* Seedling */
		seedlingblocks(device, d_keyblk, &count);
		break;
	case 0x2:
		/* Sapling */
		saplingblocks(device, d_keyblk, &count);
		break;
	case 0x3:
		/* Tree */
		treeblocks(device, d_keyblk, &count);
		break;
	default:
		err(NONFATAL, err_stype2, d_type, "data fork");
		count = 0;
		break;
	}
	if (d_blks != count) {
		if (count != 0) {
			err(NONFATAL, err_forksz3, "Data", d_blks, count);
// TODO: Need to rethink the fix mode here ... it was buggy anyhow
//			if (askfix() == 1) {
//				buf[0x03] = count & 0xff;
//				buf[0x04] = (count >> 8) & 0xff;
//			}
		}
	}
	*blkcnt += count;

	/* Resource fork */
	switch (r_type) {
	case 0x1:
		/* Seedling */
		seedlingblocks(device, r_keyblk, &count);
		break;
	case 0x2:
		/* Sapling */
		saplingblocks(device, r_keyblk, &count);
		break;
	case 0x3:
		/* Tree */
		treeblocks(device, r_keyblk, &count);
		break;
	default:
		err(NONFATAL, err_stype2, r_type, "res fork");
		count = 0;
		break;
	}
	if (r_blks != count) {
		if (count != 0) {
			err(NONFATAL, err_forksz3, "Res", r_blks, count);
			if (askfix() == 1) {
// TODO: Need to rethink the fix mode here ... it was buggy anyhow
//				buf[0x103] = count & 0xff;
//				buf[0x104] = (count >> 8) & 0xff;
			}
		}
	}
	*blkcnt += count;
	return 0;
}

/*
 * Count the blocks in a subdirectory
 */
int  subdirblocks(uchar device, uint keyblk, struct pd_dirent *ent,
                  uint blocknum, uint blkentries, uint *blkcnt) {

	struct pd_dirhdr *hdr;
	uchar parentry, parentlen;
	uint parblk;
	char *dirname;

#ifdef FREELIST
	if (!dorecurse)
		checkblock(keyblk, "Directory");
#endif
	if (readdiskblock(device, keyblk, buf) == -1) {
		err(NONFATAL, err_rdblk1, keyblk);
		return -1;
	}
	*blkcnt = 1;
	hdr = (struct pd_dirhdr*)(buf + PTRSZ);
	parentry = hdr->parentry;
	parentlen = hdr->parentlen;
	parblk = hdr->parptr[0] + 256U * hdr->parptr[1];

	if (parblk != blocknum) {
		err(NONFATAL, err_parblk3, "blk", parblk, blocknum);
		if (askfix() == 1) {
			hdr->parptr[0] = blocknum & 0xff;
			hdr->parptr[1] = (blocknum >> 8) & 0xff;
		}
	}

	if (parentry != blkentries) {
		err(NONFATAL, err_parblk3, "entry", parentry, blkentries);
		if (askfix() == 1) {
			hdr->parentry = blkentries;
		}
	}
	if (parentlen != ENTSZ) {
		err(NONFATAL, err_parblk3, "entry size", parentlen, ENTSZ);
		if (askfix() == 1) {
			hdr->parentlen = ENTSZ;
		}
	}
	dirname = buf + 0x05;
	if (strncmp(dirname, ent->name, NMLEN)) {
		err(NONFATAL, err_sdname);
	}

	blocknum = buf[0x02] + 256U * buf[0x03];
	while (blocknum) {
#ifdef FREELIST
		if (!dorecurse)
			checkblock(blocknum, "Directory");
#endif
		if (readdiskblock(device, blocknum, buf) == -1) {
			err(NONFATAL, err_rdblk1, blocknum);
			return -1;
		}
		++(*blkcnt);
		blocknum = buf[0x02] + 256U * buf[0x03];
	}
	return 0;
}

#endif

/*
 * Record the keyblock of a subdirectory to be processed subsequently
 * blocknum is the block number of the subdirectory keyblock
 * subdiridx is a sequential counter of the subdirs in the current directory
 */
void enqueuesubdir(uint blocknum, uint subdiridx) {
	static struct dirblk *prev;
	struct dirblk *p = (struct dirblk*)malloc(sizeof(struct dirblk));
	if (!p)
		err(FATALALLOC, err_nomem);
	p->blocknum = blocknum;
	if (subdiridx == 0) {     /* First subdir is inserted at head of list */
		p->next = dirs;
		dirs = p;
	} else {                  /* Subsequent subdirs follow the previous */
		p->next = prev->next;
		prev->next = p;
	}
	prev = p;
}

/*
 * Read a directory, store the raw directory blocks in a linked list.
 * device is the device number containing the directory
 * blocknum is the block number of the first block of the directory
 */
int readdir(uint device, uint blocknum) {
	static char namebuf[NMLEN+1];
	struct pd_dirhdr *hdr;
	struct block *curblk;
	ulong eof;
	uint filecount, idx, subdirs, blks, keyblk, hdrblk, count, entries;
	uchar blkentries, pd25, i;
	uint errsbefore = errcount;
	uint blkcnt = 1;
	uint hdrblknum = blocknum;

	numfiles = 0;

	blocks = (struct block*)malloc(sizeof(struct block));
	if (!blocks)
		err(FATALALLOC, err_nomem);
	curblk = blocks;
	curblk->next = NULL;
	curblk->blocknum = blocknum;

#ifdef AUXMEM
	curblk->data = auxalloc(BLKSZ);
#endif

#ifdef FREELIST
	checkblock(blocknum, "Directory");
#endif
	if (readdiskblock(device, blocknum, dirblkbuf) == -1) {
		err(NONFATAL, err_rdblk1, blocknum);
		return 1;
	}

	hdr = (struct pd_dirhdr*)(dirblkbuf + PTRSZ);

	entsz      = hdr->entlen;
	entperblk  = hdr->entperblk;
	filecount  = hdr->filecnt[0] + 256U * hdr->filecnt[1];

	fixcase(hdr->name, currdir,
	        hdr->vers, hdr->minvers, hdr->typ_len & 0x0f);

	hlinechar('=');
	printf("Directory %s (%u", currdir, filecount);
	printf(" %s)\n", filecount == 1 ? "entry" : "entries");
	hline();

#ifdef CHECK
	if (entsz != ENTSZ) {
		err(NONFATAL, err_entsz2, entsz, ENTSZ);
		return 1;
	}
	if (entperblk != ENTPERBLK) {
		err(NONFATAL, err_entblk2, entperblk, ENTPERBLK);
		return 1;
	}
#endif
	idx = entsz + PTRSZ; /* Skip header */
	blkentries = 2;
	entries = 0;
	subdirs = 0;

	while (1) {
		uint errsbeforeent = errcount;
		struct pd_dirent *ent = (struct pd_dirent*)(dirblkbuf + idx);

		if (ent->typ_len != 0) {

			if (strlen(caseopts) > 0) {
				switch (caseopts[0]) {
				case 'u':
					uppercase(ent->name,
					          ent->typ_len & 0x0f,
					          &(ent->vers),
					          &(ent->minvers));
					break;
				case 'l':
					lowercase(ent->name,
					          ent->typ_len & 0x0f,
					          &(ent->vers),
					          &(ent->minvers));
					break;
				case 'i':
					initialcase(0,
					            ent->name,
					            ent->typ_len & 0x0f,
					            &(ent->vers),
					            &(ent->minvers));
					break;
				case 'c':
					initialcase(1,
					            ent->name,
					            ent->typ_len & 0x0f,
					            &(ent->vers),
					            &(ent->minvers));
					break;
				default:
					err(FATALBADARG, err_invopt, "case");
				}
			}

			if (strlen(dateopts) > 0) {
				struct datetime ctime, mtime;
				readdatetime(ent->ctime, &ctime);
				readdatetime(ent->mtime, &mtime);
				switch (dateopts[0]) {
				case 'n':
					pd25 = 1;
					break;
				case 'o':
					pd25 = 0;
					break;
				default:
					err(FATALBADARG, err_invopt, "date");
				}
				writedatetime(pd25, &ctime, ent->ctime);
				writedatetime(pd25, &mtime, ent->mtime);
			}

			fixcase(ent->name, namebuf,
			        ent->vers, ent->minvers, ent->typ_len & 0x0f);

			switch (ent->typ_len & 0xf0) {
			case 0x10:
				fputs("Seed  ", stdout);
				break;
			case 0x20:
				fputs("Sapl  ", stdout);
				break;
			case 0x30:
				fputs("Tree  ", stdout);
				break;
			case 0x40:
				fputs("Pasc  ", stdout);
				break;
			case 0x50:
				fputs("Fork  ", stdout);
				break;
			case 0xd0:
				fputs("Dir   ", stdout);
				break;
			default:
				fputs("????  ", stdout);
				break;
			}
			fputs(namebuf, stdout);

			blks = ent->blksused[0] + 256U * ent->blksused[1];
			eof = ent->eof[0] + 256L * ent->eof[1] + 65536L * ent->eof[2];

			keyblk = ent->keyptr[0] + 256U * ent->keyptr[1];
			hdrblk = ent->hdrptr[0] + 256U * ent->hdrptr[1];
#ifdef CHECK
			if (hdrblk != hdrblknum) {
				err(NONFATAL, err_hdrblk2, hdrblk, hdrblknum);
				if (askfix() == 1) {
					ent->hdrptr[0] = hdrblknum & 0xff;
					ent->hdrptr[1] = (hdrblknum >> 8)&0xff;
				}
			}
#endif
			switch (ent->typ_len & 0xf0) {
			case 0xd0:
				/* Subdirectory */
				enqueuesubdir(keyblk, subdirs++);
#ifdef CHECK
				subdirblocks(device, keyblk, ent,
				             blocknum, blkentries, &count);
#endif
				break;
#ifdef CHECK
			case 0x10:
				/* Seedling */
				seedlingblocks(device, keyblk, &count);
				break;
			case 0x20:
				/* Sapling */
				saplingblocks(device, keyblk, &count);
				break;
			case 0x30:
				/* Tree */
				treeblocks(device, keyblk, &count);
				break;
			case 0x40:
				/* Pascal area */
				puts(" Pascal area!!");
				// TODO: Check name is PASCAL.AREA type 0xef
				count = 0;
				break;
			case 0x50:
				/* File with resource fork */
				forkblocks(device, keyblk, &count);
				break;
			default:
				err(NONFATAL, err_stype2, ent->typ_len & 0xf0, "entry");
				count = 0;
#endif
			}
#ifdef CHECK
			if (blks != count) {
				if (count != 0) {
					err(NONFATAL, err_used2, blks, count);
					if (askfix() == 1) {
						ent->blksused[0] = count & 0xff;
						ent->blksused[1] = (count >> 8) & 0xff;
					}
				}
			}
#endif
			++numfiles;
			if (numfiles == maxfiles) {
				err(NONFATAL, err_many);
				return 1;
			}
			if (errcount == errsbeforeent) {
				for (i = 0; i < 53 - strlen(namebuf); ++i)
					putchar(' ');
#ifdef CHECK
				printf("%5u blocks   [ OK ]", blks);
#else
				printf("%5u blocks\n", blks);
#endif
			} else
				putchar('\n');
			++entries;
		}
		if (blkentries == entperblk) {
			blocknum = dirblkbuf[0x02] + 256U * dirblkbuf[0x03];
#ifdef AUXMEM
			copyaux(dirblkbuf, curblk->data, BLKSZ, TOAUX);
#else
			memcpy(curblk->data, dirblkbuf, BLKSZ);
#endif
			if (blocknum == 0) {
				break;
			}
			curblk->next = (struct block*)malloc(sizeof(struct block));
			if (!curblk->next)
				err(FATALALLOC, err_nomem);
			curblk = curblk->next;
			curblk->next = NULL;
			curblk->blocknum = blocknum;
			++blkcnt;

#ifdef AUXMEM
			curblk->data = auxalloc(BLKSZ);
#endif

#ifdef FREELIST
			checkblock(blocknum, "Directory");
#endif
			if (readdiskblock(device, blocknum, dirblkbuf) == -1) {
				err(NONFATAL, err_rdblk1, blocknum);
				return 1;
			}

			blkentries = 1;
			idx = PTRSZ;
		} else {
			++blkentries;
			idx += entsz;
		}
	}
	if (filecount != entries) {
		err(NONFATAL, err_count2, filecount, entries);
		if (askfix() == 1) {
			hdr->filecnt[0] = entries & 0xff;
			hdr->filecnt[1] = (entries >> 8) & 0xff;
		}
	}
#ifdef AUXMEM
	copyaux(dirblkbuf, curblk->data, BLKSZ, TOAUX);
#else
	memcpy(curblk->data, dirblkbuf, BLKSZ);
#endif

	return errcount - errsbefore;
}

#ifdef SORT

/*
 * Build filelist[], the table used by the sorting algorithm.
 * s - character representing the sorting mode
 * callidx - if 0, the routine populates the table, otherwise it updates
 *           and existing table
 */
void buildsorttable(char s, uchar callidx) {
	static char namebuf[NMLEN+1];
	uint off;
	uchar entry, i;
	struct datetime dt;
	struct pd_dirent *ent;
	uint idx = 0;
	struct block *b = blocks;
	uchar firstent = 2; /* Skip first entry of first block */
	uchar blkidx = 1;

	while (b) {
#ifdef AUXMEM
		copyaux(b->data, dirblkbuf, BLKSZ, FROMAUX);
#else
		memcpy(dirblkbuf, b->data, BLKSZ);
#endif
		for (entry = firstent; entry <= ENTPERBLK; ++entry) {
			off = PTRSZ + (entry - 1) * entsz;
			ent = (struct pd_dirent*)(dirblkbuf + off);

			if (ent->typ_len != 0) {

				if (callidx == 0) {
					/* Build filelist[] on first call for each dir */
					filelist[idx].blockidx = blkidx;
					filelist[idx].entrynum = entry;
				} else {
					/* On subsequent calls Find existing entry in list and update it */
					for (idx = 0; idx < numfiles; ++idx)
						if ((filelist[idx].blockidx == blkidx) &&
						    (filelist[idx].entrynum == entry))
							break;
				}
				switch (tolower(s)) {
				case 'n':
				case 'i':
					fixcase(ent->name, namebuf,
					        ent->vers, ent->minvers, ent->typ_len & 0x0f);
					bzero(filelist[idx].name, NMLEN);
					for (i = 0; i < (ent->typ_len & 0x0f); ++i)
						filelist[idx].name[i] = namebuf[i];
					break;
				case 'd':
				case 't':
					filelist[idx].type = ent->type;
					break;
				case 'b':
					filelist[idx].blocks =
					  ent->blksused[0] + 256U * ent->blksused[1];
					break;
				case 'e':
					filelist[idx].eof = 
					  ent->eof[0] + 256L * ent->eof[1] + 65536L * ent->eof[2];
					break;
				case 'c':
					readdatetime(ent->ctime, &dt);
				case 'm':
					readdatetime(ent->mtime, &dt);
					sprintf(filelist[idx].datetime, "%04d%02d%02d%02d%02d",
			        		dt.year, dt.month, dt.day, dt.hour, dt.minute);
					break;
				}
				++idx;
			}
		}
		b = b->next;
		++blkidx;
		firstent = 1;
	}
	if (callidx == 0)
		numfiles = idx;
}

/*
 * Compare - filename sort in ascending order
 */
int cmp_name_asc(const void *a, const void *b) {
	return strncmp(((struct fileent*)a)->name,
	               ((struct fileent*)b)->name, NMLEN);
}

/*
 * Compare - filename sort in descending order
 */
int cmp_name_desc(const void *a, const void *b) {
	return strncmp(((struct fileent*)b)->name,
	               ((struct fileent*)a)->name, NMLEN);
}

/*
 * Compare - filename sort in ascending order - case insensitive
 */
int cmp_name_asc_ci(const void *a, const void *b) {
	return strncasecmp(((struct fileent*)a)->name,
	                   ((struct fileent*)b)->name, NMLEN);
}

/*
 * Compare - filename sort in descending order - case insensitive
 */
int cmp_name_desc_ci(const void *a, const void *b) {
	return strncasecmp(((struct fileent*)b)->name,
	                   ((struct fileent*)a)->name, NMLEN);
}

/*
 * Compare - date/time sort in ascending order
 */
int cmp_datetime_asc(const void *a, const void *b) {
	return strncmp(((struct fileent*)a)->datetime,
	               ((struct fileent*)b)->datetime, 16);
}

/*
 * Compare - date/time sort in descending order
 */
int cmp_datetime_desc(const void *a, const void *b) {
	return strncmp(((struct fileent*)b)->datetime,
	               ((struct fileent*)a)->datetime, 16);
}

/*
 * Compare - type sort in ascending order
 * Uses the order field to make qsort() stable
 */
int cmp_type_asc(const void *a, const void *b) {
	struct fileent *aa = (struct fileent*)a;
	struct fileent *bb = (struct fileent*)b;
	int rc = aa->type - bb->type;
	return rc != 0 ? rc : aa->order - bb->order;
}
/*
 * Compare - type sort in descending order
 * Uses the order field to make qsort() stable
 */
int cmp_type_desc(const void *a, const void *b) {
	struct fileent *aa = (struct fileent*)a;
	struct fileent *bb = (struct fileent*)b;
	int rc = bb->type - aa->type;
	return rc != 0 ? rc : aa->order - bb->order;
}

/*
 * Compare - sort with directories at the beginning
 * Uses the order field to make qsort() stable
 */
int cmp_dir_beg(const void *a, const void *b) {
	struct fileent *aa = (struct fileent*)a;
	struct fileent *bb = (struct fileent*)b;
	if ((aa->type == 0x0f) && (bb->type != 0x0f))
		return -1;
	if ((bb->type == 0x0f) && (aa->type != 0x0f))
		return 1;
	return aa->order - bb->order;
}

/*
 * Compare - sort with directories at the end
 * Uses the order field to make qsort() stable
 */
int cmp_dir_end(const void *a, const void *b) {
	struct fileent *aa = (struct fileent*)a;
	struct fileent *bb = (struct fileent*)b;
	if ((aa->type == 0x0f) && (bb->type != 0x0f))
		return 1;
	if ((bb->type == 0x0f) && (aa->type != 0x0f))
		return -1;
	return aa->order - bb->order;
}

/*
 * Compare - sort in increasing order of blocks used
 */
int  cmp_blocks_asc(const void *a, const void *b) {
	struct fileent *aa = (struct fileent*)a;
	struct fileent *bb = (struct fileent*)b;
	int rc = aa->blocks - bb->blocks;
	return rc != 0 ? rc : aa->order - bb->order;
}

/*
 * Compare - sort in decreasing order of blocks used
 */
int  cmp_blocks_desc(const void *a, const void *b) {
	struct fileent *aa = (struct fileent*)a;
	struct fileent *bb = (struct fileent*)b;
	int rc = bb->blocks - aa->blocks;
	return rc != 0 ? rc : aa->order - bb->order;
}

/*
 * Compare - sort in increasing order of EOF position
 */
int  cmp_eof_asc(const void *a, const void *b) {
	struct fileent *aa = (struct fileent*)a;
	struct fileent *bb = (struct fileent*)b;
	long diff = aa->eof - bb->eof;
	if (diff == 0)
		return aa->order - bb->order;
	if (diff > 0)
		return 1;
	else
		return -1;
}

/*
 * Compare - sort in decreasing order of EOF position
 */
int  cmp_eof_desc(const void *a, const void *b) {
	struct fileent *aa = (struct fileent*)a;
	struct fileent *bb = (struct fileent*)b;
	long diff = bb->eof - aa->eof;
	if (diff == 0)
		return aa->order - bb->order;
	if (diff > 0)
		return 1;
	else
		return -1;
}

/*
 * No-op sort which just keeps items in the same order
 */
int  cmp_noop(const void *a, const void *b) {
	struct fileent *aa = (struct fileent*)a;
	struct fileent *bb = (struct fileent*)b;
	return aa->order - bb->order;
}

/*
 * Sort filelist[]
 * s defines the field to sort on
 */
void sortlist(char s) {
	uint i;
	char ss = tolower(s);

	/*
	 * We only populate the order field when NOT sorting by name.
	 * This lets us save two bytes by overflowing the name field into the
	 * order field.
	 */
	if ((ss != 'n') && (ss != 'i')) {
		for (i = 0; i < numfiles; ++i) {
			filelist[i].order = i;
		}
	}

	switch (s) {
	case 'n':
		qsort(filelist, numfiles, sizeof(struct fileent),
		      cmp_name_asc);
		break;
	case 'N':
		qsort(filelist, numfiles, sizeof(struct fileent),
		      cmp_name_desc);
		break;
	case 'i':
		qsort(filelist, numfiles, sizeof(struct fileent),
		      cmp_name_asc_ci);
		break;
	case 'I':
		qsort(filelist, numfiles, sizeof(struct fileent),
		      cmp_name_desc_ci);
		break;
	case 'c':
	case 'm':
		qsort(filelist, numfiles, sizeof(struct fileent),
		      cmp_datetime_asc);
		break;
	case 'C':
	case 'M':
		qsort(filelist, numfiles, sizeof(struct fileent),
		      cmp_datetime_desc);
		break;
	case 't':
		qsort(filelist, numfiles, sizeof(struct fileent),
		      cmp_type_asc);
		break;
	case 'T':
		qsort(filelist, numfiles, sizeof(struct fileent),
		      cmp_type_desc);
		break;
	case 'd':
		qsort(filelist, numfiles, sizeof(struct fileent),
		      cmp_dir_beg);
		break;
	case 'D':
		qsort(filelist, numfiles, sizeof(struct fileent),
		      cmp_dir_end);
		break;
	case 'b':
		qsort(filelist, numfiles, sizeof(struct fileent),
		      cmp_blocks_asc);
		break;
	case 'B':
		qsort(filelist, numfiles, sizeof(struct fileent),
		      cmp_blocks_desc);
		break;
	case 'e':
		qsort(filelist, numfiles, sizeof(struct fileent),
		      cmp_eof_asc);
		break;
	case 'E':
		qsort(filelist, numfiles, sizeof(struct fileent),
		      cmp_eof_desc);
		break;
	case '.':
		qsort(filelist, numfiles, sizeof(struct fileent),
		      cmp_noop);
		break;
	default:
		err(FATALBADARG, err_invopt, "sort");
	}
}

#endif

/*
 * Convert block index to block number
 * Block index is 1-based (1,2,3 ...)
 */
uint blockidxtoblocknum(uint idx) {
	uint i;
	struct block *p = blocks;
	for (i = 1; i < idx; ++i)
		p = p->next;
	return p->blocknum;
}

/*
 * Copy the 4 bytes of pointers from the directory block with index idx
 * to the start of dirblkbuf[]; zeroes the rest of dirblkbuf[].
 */
void copydirblkptrs(uint idx) {
	uint i;
	struct block *p = blocks;
	for (i = 1; i < idx; ++i)
		p = p->next;
	bzero(dirblkbuf, BLKSZ);
#ifdef AUXMEM
	copyaux(p->data, dirblkbuf, PTRSZ, FROMAUX);
#else
	memcpy(dirblkbuf, p->data, PTRSZ);
#endif
}

/*
 * Copy a file entry from one srcblk, srcent to dstblk, dstent
 * All indices are 1-based.
 * dstblk is written to dirblkbuf[]
 */
void copydirent(uint srcblk, uint srcent, uint dstblk, uint dstent, uint device) {
	struct block *source = blocks;
	struct pd_dirent *ent;
	struct pd_dirhdr *hdr;
	char *srcptr, *dstptr;
	uint parentblk;

	if (dodebug) {
		printf("  from dirblk %03u entry %02u", srcblk, srcent);
		printf("    to dirblk %03u entry %02u\n", dstblk, dstent);
	}

	while (--srcblk > 0)
		source = source->next;

	srcptr =  source->data + PTRSZ + (srcent-1) * entsz;
	dstptr =  dirblkbuf + PTRSZ + (dstent-1) * entsz;

#ifdef AUXMEM
	copyaux(srcptr, dstptr, entsz, FROMAUX);
#else
	memcpy(dstptr, srcptr, entsz);
#endif

	/* For directories, update the parent dir entry number */
	ent = (struct pd_dirent*)dstptr;
	if ((ent->typ_len & 0xf0) == 0xd0) {
		uint block = ent->keyptr[0] + 256U * ent->keyptr[1];
		if (readdiskblock(device, block, buf) == -1)
			err(NONFATAL, err_updsdir1, "read");
		hdr = (struct pd_dirhdr*)(buf + PTRSZ);
		parentblk = blockidxtoblocknum(dstblk);
		hdr->parptr[0] = parentblk & 0xff;
		hdr->parptr[1] = (parentblk >> 8) & 0xff;
		hdr->parentry = dstent;
		if (dowrite) {
			if (writediskblock(device, block, buf) == -1)
				err(NONFATAL, err_updsdir1, "write");
		}
	}
}

/*
 * Build sorted directory block dstblk (1,2,3...) using the sorted list in
 * filelist[]. Note that the block and entry numbers are 1-based indices.
 */
void sortblock(uint device, uint dstblk) {
	uint i, firstlistent, lastlistent;
	uchar destentry;
	copydirblkptrs(dstblk);
	if (dstblk == 1) {
		copydirent(1, 1, 1, 1, device); /* Copy directory header */
		destentry = 2; /* Skip header on first block */
		firstlistent = 0;
		lastlistent = entperblk - 2;
	} else {
		destentry = 1;
		firstlistent = (dstblk - 1) * entperblk - 1;
		lastlistent = firstlistent + entperblk - 1;
	}
	if (lastlistent > numfiles - 1)
		lastlistent = numfiles - 1;
	for (i = firstlistent; i <= lastlistent; ++i) {
		copydirent(filelist[i].blockidx, filelist[i].entrynum,
		           dstblk, destentry++, device);
	}
}

/*
 * Build each sorted directory block in turn, then write them
 * out to disk.
 */
int writedir(uchar device) {
	uint dstblk = 1;
	struct block *b = blocks;
	while (b) {
		sortblock(device, dstblk++);
		if (writediskblock(device, b->blocknum, dirblkbuf) == -1) {
			err(NONFATAL, err_wtblk1, b->blocknum);
			return 1;
		}
		b = b->next;
	}
	return 0;
}

/*
 * Walk through the linked list freeing memory
 */
void freeblocks(void) {
	struct block *i = blocks, *j;
	while (i) {
		j = i->next;
		free(i);
		i = j;
	}
	blocks = NULL;
}

void subtitle(char *s) {
	uchar i;
	putchar('\n');
	hlinechar('_');
	revers(1);
	fputs(s, stdout);
	revers(0);
	for (i = strlen(s); i < 79; ++i)
		putchar(' ');
	putchar('|');
}

void interactive(void) {
	char w, l, d, f, wrt;
#ifdef FREELIST
	char z;
#endif
	uchar level;

	doverbose = 1;

	revers(1);
	hlinechar(' ');
	fputs("S O R T D I R  v0.71 alpha                  Use ^ to return to previous question", stdout);
	hlinechar(' ');
	revers(0);

q1:
	putchar('\n');
	revers(1);
	fputs("Enter start path>", stdout);
	revers(0);
	putchar(' ');
	scanf("%s", buf);
	getchar(); // Eat the carriage return

q2:
	dowholedisk = dorecurse = 0;
	subtitle("What to process"); 
	do {
		fputs("| [-] Directory  | [t] Tree                |  [v] Volume   |                   |", stdout);
		w = getchar();
	} while (strchr("-tv^", w) == NULL);
	if (w == '^')
		goto q1;
	switch (w) {
	case 't':
		dorecurse = 1;
		break;
	case 'v':
		dorecurse = 1;
		dowholedisk = 1;
	}

q3:
	subtitle("Multi-level directory sort");
    //     12345678901234567890123456789012345678901234567890123456789012345678901234567890
	fputs("| Lower case option ascending order, upper case option descending order        |", stdout);
	fputs("| [nN] Name      | [iI] Name (case-insens) | [tT] Type     | [dD] Directories  |", stdout);
	fputs("| [cC] Creation  | [mM] Modification       | [bB] Blocks   | [eE] EOF Position |", stdout);
	fputs("| [-]  Done      |                         |               |                   |", stdout);
	for (level = 0; level < NLEVELS; ++level) {
		do {
			printf("\nLevel %d > ", level+1);
			sortopts[level] = getchar();
		} while (strchr("-nNiItTdDcCmMbBeE^", sortopts[level]) == NULL);
		if (sortopts[level] == '-') {
			sortopts[level] = '\0';
			break;
		}
		if (sortopts[level] == '^')
			goto q2;
	}
	sortopts[NLEVELS] = '\0';

q4:
	subtitle("Filename case conversion");
	do {
    //         12345678901234567890123456789012345678901234567890123456789012345678901234567890
		fputs("| [-] No change  |                         |               |                   |", stdout);
		fputs("| [l] Lowercase  | [u] Uppercase           | [i] Initial   | [c] Camelcase     |", stdout);
		l = getchar();
	} while (strchr("-luic^", l) == NULL);
	if (l == '^')
		goto q3;
	if (l != '-')
		caseopts[0] = l;

q5:
	subtitle("Date format conversion");
	do {
		fputs("| [-] No change  | [n] 'New' ProDOS 2.5+   | [o] 'Old' legacy format           |",stdout);
		d = getchar();
	} while (strchr("-no^", d) == NULL);
	if (d == '^')
		goto q4;
	if (d != '-')
		dateopts[0] = d;

q6:	
	subtitle("Attempt to fix errors?");
	do {
		fputs("| [-] Never fix  | [?] Ask before fixing   | [a] Always fix                    |", stderr); 
		f = getchar();
	} while (strchr("-?a^", f) == NULL);
	if (f == '^')
		goto q5;
	fixopts[0] = ((f == '-') ? 'n' : f);

#ifdef FREELIST
q7:
	if (w == 'v') {
		subtitle("Zero free space?");
		do {
			fputs("| [-] No         | [z] Zero free blocks    |                                   |", stderr);
			z = getchar();
		} while (strchr("-z^", z) == NULL);
		if (z == '^')
			goto q6;
		if (z == 'z')
			dozero = 1;
	}
#endif

q8:	
	subtitle("Confirm write to disk");
	do {
		fputs("| [-] No         | [w] Write to disk       |                                   |", stderr);
		wrt = getchar();
	} while (strchr("-w^", wrt) == NULL);
	if (wrt == '^')
#ifdef FREELIST
		goto q7;
#else
		goto q6;
#endif
	if (wrt == 'w')
		dowrite = 1;
}


/*
 * Performs all actions for a single directory
 * blocknum is the keyblock of the directory to process
 */
void processdir(uint device, uint blocknum) {
	uchar i, errs;
	flushall();
	errs = readdir(device, blocknum);
	if ((strlen(fixopts) == 0) && errs) {
		err(NONFATAL, err_nosort);
		goto done;
	}
#ifdef SORT
	if (strlen(sortopts) > 0) {
		if (doverbose)
			fputs("Sorting: ", stdout);
		for (i = 0; i < strlen(sortopts); ++i) {
			if (doverbose)
				printf("[%c] ", sortopts[i]);
			buildsorttable(sortopts[i], i);
			sortlist(sortopts[i]);
		}
		if (doverbose)
			putchar('\n');
		if (dowrite) {
			puts("Writing dir ...");
			errs = writedir(device);
		} else {
			revers(1);
			fputs("Not writing dir", stdout);
			revers(0);
			putchar('\n');
		}
	}
#endif
done:
	freeblocks();
#ifdef AUXMEM
	freeallaux();
#endif
}

#ifdef FREELIST

/*
 * Iterate through freelist[] and usedlist[] and see if all is well.
 * If we have visited all files and directories on the volume, every
 * block should either be marked free or marked used.
 */
void checkfreeandused(uchar device) {
	uint i, freeblks = 0;
	printf("Total blks\t%u\n", totblks);
	for (i = 0; i < totblks; ++i)
		if (isfree(i))
			++freeblks;
	printf("Free blks\t%u\n", freeblks);
//	printf("Percentage full\t\t%.1f\n",
//	       100.0 * (float)(totblks - freeblks) / totblks);
	for (i = 0; i < totblks; ++i) {
		uint idx = i / 8;
		if (!(freelist[idx] ^ usedlist[i]))	/* Speed-up */
			continue;
		if (isfree(i)) {
			if (isused(i)) {
				err(NONFATAL, err_blfree1, i);
				if (askfix() == 1)
					marknotfree(i);
			}
		} else {
			if (!isused(i)) {
				err(NONFATAL, err_blused1, i);
				if (askfix() == 1)
					markfree(i);
			}
		}
	}
	if (dozero)
		zerofreeblocks(device, freeblks);
}

/*
 * Zero block blocknum
 */
void zeroblock(uchar device, uint blocknum) {
	bzero(buf, BLKSZ);
//	DIORecGS dr;
//	dr.pCount = 6;
//	dr.devNum = device;
//	dr.buffer = buf;
//	dr.requestCount = BLKSZ;
//	dr.startingBlock = blocknum;
//	dr.blockSize = BLKSZ;
//	DWriteGS(&dr);
//	if (dr.transferCount != BLKSZ)
//		err(FATAL, "Block write failed");
}

/*
 * Zero all free blocks on the volume
 */
void zerofreeblocks(uchar device, uint freeblks) {
	uint i, step = freeblks / 60, ctr = 0;
	puts("Zeroing free blocks ...");
	for (i = 0; i < totblks; ++i)
		if (isfree(i)) {
			zeroblock(device, i);
			++ctr;
			if (ctr == step) {
				putchar('=');
				fflush(stdout);
				ctr = 0;
			}
		}
	puts("\nDone zeroing!");
}

#endif

#ifdef CMDLINE

void usage(void) {
	printf("usage: sortdir [-s xxx] [-n x] [-rDwvVh] path\n\n");
	printf("  Options: -s xxx  Directory sort options\n");
	printf("           -n x    Filename upper/lower case options\n");
	printf("           -d x    Date format conversion options\n");
	printf("           -f x    Fix mode\n");
	printf("           -r      Recursive descent\n");
	printf("           -D      Whole-disk mode (implies -r)\n");
	printf("           -w      Enable writing to disk\n");
	printf("           -z      Zero free space\n");
	printf("           -v      Verbose output\n");
	printf("           -V      Verbose debugging output\n");
	printf("           -h      This help\n");
	printf("\n");
	printf("-nx: Upper/lower case filenames, where x is:\n");
	printf("  l  convert filenames to lower case           eg: read.me\n");
	printf("  u  convert filenames to upper case           eg: READ.ME\n");
	printf("  i  convert filenames to initial upper case   eg: Read.me\n");
	printf("  c  convert filenames to camel case           eg: Read.Me\n");
	printf("\n");
	printf("-dx: Date/time on-disk format conversion, where x is:\n");
	printf("  n  convert to new (ProDOS 2.5+) format\n");
	printf("  o  convert to old (ProDOS 1.0-2.4.2, GSOS) format\n");
	printf("\n");
	printf("-sxxx: Dir sort, where xxx is a list of fields to sort\n");
	printf("on. The sort options are processed left-to-right.\n");
	printf("  n  sort by filename ascending\n");
	printf("  N  sort by filename descending\n");
	printf("  i  sort by filename ascending - case insensitive\n");
	printf("  I  sort by filename descending - case insensitive\n");
	printf("  c  sort by create date/time ascending\n");
	printf("  C  sort by create date/time descending\n");
	printf("  m  sort by modify date/time ascending\n");
	printf("  M  sort by modify date/time descending\n");
	printf("  t  sort by type ascending\n");
	printf("  T  sort by type descending\n");
	printf("  d  sort directories to top\n");
	printf("  D  sort directories to bottom\n");
	printf("  b  sort by blocks used ascending\n");
	printf("  B  sort by blocks used descending\n");
	printf("  e  sort by EOF position ascending\n");
	printf("  E  sort by EOF position descending\n");
	printf("\n");
	printf("-fx: Fix mode, where x is:\n");
	printf("  ?  prompt for each fix\n");
	printf("  n  never fix\n");
	printf("  y  always fix (be careful!)\n");
	printf("\n");
	printf("e.g.: sortdir -w -s nf .\n");
	printf("Will sort the current directory first by name (ascending),\n");
	printf("then sort directories to the top, and will write the\n");
	printf("sorted directory to disk.\n");
	err(FATAL, err_usage);
}

#define MAXNUMARGS 10

int argc;
char *argv[MAXNUMARGS];

void parseargs() {
	char *p;
	char i = 0, s = 0, prev = ' ';
	argc = 1;
	for (p = (char*)0x200; p <= (char*)0x27f; ++p) {
		*p &= 0x7f;
		if ((*p == 0) || (*p == 0x0d)) {
			argv[argc - 1] = buf + s;
			break;
		}
		if (*p == ' ') {
			if (prev != ' ') {
				buf[i++] = '\0';
				argv[argc - 1] = buf + s;
				s = i;
				++argc;
			}
		} else {
			buf[i++] = *p;
		}
		prev = *p;
	}
}

#endif

//int main(int argc, char *argv[]) {
int main() {
#ifdef CMDLINE
	int opt;
#endif
	uchar dev;
	uint blk;

	uchar *pp;
	pp = (uchar*)0xbf98;
	if (!(*pp & 0x02))
		err(FATAL, err_80col);
#ifdef AUXMEM
	if ((*pp & 0x30) != 0x30)
		err(FATAL, err_128K);
#endif

	// Clear system bit map
	for (pp = (uchar*)0xbf58; pp <= (uchar*)0xbf6f; ++pp)
		*pp = 0;

	videomode(VIDEOMODE_80COL);

	_heapadd((void*)0x0800, 0x1800);
	//printf("\nHeap: %u %u\n", _heapmemavail(), _heapmaxavail());

	buf =  (char*)malloc(sizeof(char) * BLKSZ);
	buf2 =  (char*)malloc(sizeof(char) * BLKSZ);
	dirblkbuf = (char*)malloc(sizeof(char) * BLKSZ);
	//printf("\nHeap: %u %u\n", _heapmemavail(), _heapmaxavail());
	maxfiles = _heapmaxavail() / sizeof(struct fileent);
	printf("[%u]\n", maxfiles);
	filelist = (struct fileent*)malloc(sizeof(struct fileent) * maxfiles);

#ifdef CMDLINE
	parseargs();
#endif

#ifdef CMDLINE
	if (argc == 1)
		interactive();
	else {
		if (argc < 2)
			usage();
		while ((opt = getopt(argc, argv, "DrwvVzs:n:f:d:h")) != -1) {
			switch (opt) {
			case 'D':
				dowholedisk = 1;
				dorecurse = 1;
				break;
			case 'r':
				dorecurse = 1;
				break;
			case 'w':
				dowrite = 1;
				break;
			case 'v':
				doverbose = 1;
				break;
			case 'V':
				dodebug = 1;
				break;
			case 'z':
				dozero = 1;
				dowholedisk = 1;
				dorecurse = 1;
				break;
			case 's':
				strncpy(sortopts, optarg, NLEVELS);
				break;
			case 'n':
				strncpy(caseopts, optarg, 1);
				break;
			case 'f':
				strncpy(fixopts, optarg, 1);
				break;
			case 'd':
				strncpy(dateopts, optarg, 1);
				break;
			case 'h':
			default:
				usage();
		}
	}

	if (optind != argc - 1)
		usage();
	}
#else

	interactive();

#endif

	if (((strlen(caseopts) > 0) || (strlen(fixopts) > 0) ||
	     (strlen(dateopts) > 0)) && (strlen(sortopts) == 0))
		strncpy(sortopts, ".", 1);

#ifdef CMDLINE
	firstblk(((argc == 1) ? buf : argv[optind]), &dev, &blk);
#else
	firstblk(buf, &dev, &blk);
#endif

#ifdef FREELIST
	readfreelist(dev);
#endif
	if (dowholedisk)
		processdir(dev, 2);
	else
		processdir(dev, blk);
	if (dorecurse) {
		while (dirs) {
			struct dirblk *d = dirs;
			blk = dirs->blocknum;
			dirs = d->next;
			free(d);
			processdir(dev, blk);
		}
	}
#ifdef FREELIST
	if (dowholedisk)
		checkfreeandused(dev);

	free(freelist);
	free(usedlist);
#endif
	err(FINISHED, "");
	return 0; // Just to shut up warning
}

