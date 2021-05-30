; Fully disassembled and analyzed source to SMT
; NS.CLOCK.SYSTEM by M.G. - 04/20/2017
; Assembles to a binary match for SMT code if
; IncludeJunk is set, see the .if IncludeJunk for details.

; other notes:
; * uses direct block access to read volume directory,
;   so won't launch from an AppleShare volume.

; Build instructions:
; ca65 ns.clock.system.s -l ns.clock.system.lst
; ld65 -t none -o ns.clock.system ns.clock.system.o
; put ns.clock.system as a SYS file on a ProDOS disk.

        .setcpu "6502"

IncludeJunk = 0                       ; original file had a bunch of garbage
                                      ; setting this to 1 includes it
; ----------------------------------------------------------------------------
; Zero page
POINTER         := $A5                          ; generic pointer used everywhere
ENTNUM          := $A7                          ; current file entry # in block (zero-based)
LENGTH          := $A8                          ; generic length byte used everywhere

; entry points
PRODOS          := $BF00
INIT            := $FB2F
HOME            := $FC58
CROUT           := $FD8E
PRBYTE          := $FDDA
COUT            := $FDED
SETNORM         := $FE84
SETKBD          := $FE89
SETVID          := $FE93

; buffers & other spaces
INBUF           := $0200                        ; input buffer
PATHBUF         := $0280                        ; path buffer
RELOCT          := $1000                        ; relocation target
BLOCKBUF        := $1800                        ; block & I/O buffer
SYSEXEC         := $2000                        ; location of SYS executables
SOFTEV          := $03F2                        ; RESET vector

; global Page entries
CLKENTRY        := $BF06                        ; clock routine entry point
DEVNUM          := $BF30                        ; most recent accessed device
MEMTABL         := $BF58                        ; system bitmap
DATELO          := $BF90
DATEHI          := $BF91
TIMELO          := $BF92
TIMEHI          := $BF93
MACHID          := $BF98                        ; machine ID

; I/O and hardware
ROMIn2          := $C082                        ; access to read ROM/no write LC RAM
LCBank1         := $C08B                        ; Access twice to write LC bank 1
KBD             := $C000                        ; keyboard
INTCXROMOFF     := $C006                        ; Disable internal $C100-$CFFF ROM
INTCXROMON      := $C007                        ; Enable interal $C100-$CFFF ROM
KBDSTR          := $C010                        ; keyboard strobe
INTCXROM        := $C015                        ; Read state of $C100-$CFFF soft switch
CLR80VID        := $C00C                        ; Turn off 80-column mode
CLRALTCHAR      := $C00E                        ; Turn off alt charset
SLOT3ROM        := $C300                        ; SLOT 3 ROM
C8OFF           := $CFFF                        ; C8xx Slot ROM off

; Misc
CLKCODEMAX      := $7D

; Macro to define ASCII string with high bit set.
.macro  hasc Arg
  .repeat .strlen(Arg), I
    .byte   .strat(Arg, I) | $80
  .endrep
.endmacro

; ----------------------------------------------------------------------------
; relocate ourself from SYSEXEC to RELOCT
; note that we .org the whole thing, including this routine, at the target
; address and jump to it after the first page is relocated.
        .org    RELOCT                          ; note code initially runs at SYSEXEC
.proc   Relocate
        sec
        bcs     :+                              ; skip version info
        .byte   $04, $21, $91                   ; version date in BCD
:       ldx     #$05                            ; page counter, do $0500 bytes
        ldy     #$00                            ; byte counter
from:   lda     SYSEXEC,y                       ; start location
to:     sta     RELOCT,y                        ; end location
        iny                                     ; next byte offset
        bne     from                            ; if not zero, copy byte
        inc     from+2                          ; otherwise increment source address high byte
        inc     to+2                            ; and destination address high byte
        dex                                     ; dec page counter
        beq     Main                            ; when done start main code
        jmp     from                            ; live jump... into relocated code after first page loop
.endproc
; ----------------------------------------------------------------------------
.proc   Main
        ; figure out length of our name and stick in LENGTH
        lda     #$00
        sta     LENGTH                          ; zero length
        ldx     PATHBUF                         ; length pf path
        beq     Main1                           ; skip if length = 0 (no path)
:       inc     LENGTH                          ; length += 1 for each non-/
        dex                                     ; previous char in path
        beq     CopyNm                          ; nothing left?  Copy our name.
        lda     PATHBUF,x                       ; get character
        ; check for /... kinda wtf, as cmp #$2f would work...
        eor     #$2F                            ; roundabout check for '/'
        asl     a                               ; upper/lower case
        bne     :-                              ; keep examining if not '/'
        ; now save our name (assuming we weren't lied to)
CopyNm: ldy     #$00                            ; init destination offset
:       iny                                     ; increment destination offset
        inx                                     ; inc source offset
        lda     PATHBUF,x                       ; get source char
        sta     MyName,y                        ; write to save location
        cpy     LENGTH                          ; got it all?
        bcc     :-                              ; nope, copy more
        sty     MyName                          ; save length
        ; done moving stuff
Main1:  cld
        bit     ROMIn2                          ; make sure ROM enabled
        ; letter of the law with RESET vector
        lda     #<Main1
        sta     SOFTEV
        lda     #>Main1
        sta     SOFTEV+1
        eor     #$A5
        sta     SOFTEV+2
        lda     #$95                            ; control code
        jsr     COUT                            ; to quit 80-column firmware
        ldx     #$FF                            ; reset
        txs                                     ; stack pointer
        ; get video & keyboard I/O to known state
        sta     CLR80VID
        sta     CLRALTCHAR
        jsr     SETVID
        jsr     SETKBD
        jsr     SETNORM
        jsr     INIT
        ; initialize memory map
        ldx     #$17                            ; there are $18 bytes total
        lda     #$01                            ; last byte gets $01 (protect global page)
:       sta     MEMTABL,x
        lda     #$00                            ; all but first byte get $00 (no protection)
        dex
        bne     :-
        lda     #$CF                            ; first byte protect ZP, stack, text page 1
        sta     MEMTABL
        lda     MACHID
        and     #$88                            ; mask in bits indicating machine with lower case
        bne     :+                              ; has lower case, skip over next few instructions
        lda     #$DF                            ; mask value
        sta     CaseCv                          ; to make print routine convert to upper case  
:       lda     MACHID
        and     #$01                            ; mask in clock bit 
        beq     FindClock                       ; no clock yet, proceed
        jsr     HOME
        jsr     iprint
        .byte   $8D                             ; CR
        hasc    "Previous Clock Installed!"
        .byte   $87,$8D,$00                     ; BELL, CR, done
        jmp      NextSys
.endproc
; go hunting for clock
.proc   FindClock
        ldy     #$03                            ; save old date and time
:       lda     DATELO,y                        ; because we are going to overwrite
        sta     DTSave,y                        ; during our probes
        dey
        bpl     :-
        ; We are going to try slot ROMs first
        lda     #>C8OFF
        ldy     #<C8OFF
        sta     CDRdSwH
        sty     CDRdSwL
        sta     CDRsSwH
        sty     CDRsSwL
        lda     #$00
        sta     Count                           ; init counter
        ; check slot 3 in system default state first
        lda     #$03                            ; initial check slot 3 for Count=0
CkSlot: ora     #$C0                            ; accumulator has current value of Count after first iteration
        sta     CDStRmH
CkOthr: sta     CDRstA2
        sta     CDUnlck
        sta     CDRdClk
        lda     #$03                            ; Try to read clock 3 times
        sta     ClkTry
GetDT:  jsr     ClockDrv                        ; read clock
        lda     DATEHI                          ; now see if ProDOS time and date
        ror     a                               ; values make sense
        lda     DATELO
        rol     a
        rol     a
        rol     a
        rol     a
        and     #$0F  
        beq     ClkNxt
        cmp     #$0D
        bcs     ClkNxt
        lda     DATELO
        and     #$1F
        beq     ClkNxt
        cmp     #$20
        bcs     ClkNxt
        lda     TIMEHI
        cmp     #$18
        bcs     ClkNxt
        lda     TIMELO
        cmp     #$3C
        bcs     ClkNxt
        dec     ClkTry
        bne     GetDT                           ; try again if successful this time
        beq     InstallDriver                   ; found clock!
ClkNxt: inc     Count
        lda     Count
        cmp     #$08
        bcc     CkSlot                          ; if Count < 8
        bne     NoClock                         ; if Count != (thus >) 8
        ; try internal $C100-$CFFF ROM
        ; modify driver to save CX ROM status, enable it, and restore it
        lda     #>INTCXROM
        ldy     #<INTCXROM
        sta     CDRdSwH
        sty     CDRdSwL
        ldy     #<INTCXROMON
        sta     CDStRmH
        sty     CDStRmL
        dey                                     ; y = $06 now
        sta     CDRsSwH                           ; and refers to INTCXROMOFF
        sty     CDRsSwL
        lda     #$C8                            ; now reference $C800 space
        bne     CkOthr                          ; and look for clock
.endproc
.proc NoClock
        ldy     #$03                            ; restore old date/time because
:       lda     DTSave,y                        ; we didn't find clock anywhere
        sta     DATELO,y
        dey
        bpl     :-
        jsr     HOME
        jsr     iprint
        .byte   $8D
        hasc    "No-SLot Clock Not Found."      ; typo is original
        .byte   $8D, $8D
        hasc    "Clock Not Installed!"
        .byte   $87, $8D, $00
        ; launch next .system file
        jmp     NextSys
.endproc
DTSave: .byte   $00,$00,$00,$00                 ; Saved ProDOS Date/Time values
ClkTry: .byte   $03                             ; Counter for clock read tries left
Count:  .byte   $00                             ; Count of slots/locations checked
; install driver, this does it the right way and looks at the vector
; and puts the driver there
.proc   InstallDriver
        ; this code corrects reference to DS121x unlock sequence
        lda     CLKENTRY+1
        sta     POINTER
        clc
        adc     #(DSUnlk-ClockDrv)
        sta     CDULSqL
        lda     CLKENTRY+2
        sta     POINTER+1
        adc     #$00
        sta     CDULSqH
        ; done correcting
        lda     LCBank1                           ; make LC RAM writable
        lda     LCBank1
        ldy     #DrvSize                          ; copy driver
:       lda     ClockDrv,y
        sta     (POINTER),y
        dey
        bpl     :-
        lda     MACHID                           ; current MACHID
        ora     #$01                             ; set clock installed bit
        sta     MACHID                           ; and put back
        lda     #$4C                             ; JMP
        sta     CLKENTRY                         ; into clock driver entry point
        jsr     CLKENTRY                         ; may as well give it a whirl
        bit     ROMIn2                           ; LC write protect
        jsr     HOME
        jsr     iprint
        .byte   $8d
        hasc    "No-Slot Clock Installed  "
        .byte   $00
        ; print today's date
        lda     DATEHI
        ror     a
        pha
        lda     DATELO
        pha
        rol     a
        rol     a
        rol     a
        rol     a
        and     #$0F  
        jsr     PrDec 
        lda     #$AF  
        jsr     COUT  
        pla
        and     #$1F  
        jsr     PrDec 
        lda     #$AF  
        jsr     COUT  
        pla
        jsr     PrDec 
        jsr     CROUT 
.endproc
; This starts the process of finding & launching next system file
; unfortunately it also uses block reads and can't be run from an AppleShare
; volume. 
.proc   NextSys
        ; set reset vector to DoQUit
        lda     #<DoQuit
        sta     SOFTEV
        lda     #>DoQuit
        sta     SOFTEV+1
        eor     #$A5
        sta     SOFTEV+2
        lda     DEVNUM                          ; last unit number accessed
        sta     PL_READ_BLOCK+1                 ; put in parameter list
        jsr     GetBlock                        ; get first volume directory block
        lda     BLOCKBUF+$23                    ; get entry_length
        sta     SMENTL                          ; modify code
        lda     BLOCKBUF+$24                    ; get entries_per_block
        sta     SMEPB                           ; modify code
        lda     #$01                            ; 
        sta     ENTNUM                          ; init current entry number as second (from 0)
        lda     #<(BLOCKBUF+$2B)                ; set pointer to that entry
        sta     POINTER                         ; making assumptions as we go
        lda     #>(BLOCKBUF+$2B)
        sta     POINTER+1
        ; loop to examine file entries...
FEntLp: ldy     #$10                            ; offset of file_type
        lda     (POINTER),y
        cmp     #$FF                            ; SYS?
        bne     NxtEnt                          ; nope..
        ldy     #$00                            ; offset of storage_type & name_length
        lda     (POINTER),y
        and     #$30                            ; mask interesting bits of storage_type
        beq     NxtEnt                          ; if not 1-3 (standard file organizations)
        lda     (POINTER),y                     ; get storage_type and name_length again
        and     #$0F                            ; mask in name_length
        sta     LENGTH                          ; save for later
        tay                                     ; and use as index for comparison
        ; comparison loop
        ldx     #$06                            ; counter for size of ".SYSTEM"
:       lda     (POINTER),y                     ; get file name byte
        cmp     system,x                        ; compare to ".SYSTEM"
        bne     NxtEnt                          ; no match
        dey
        dex
        bpl     :-
        ; if we got here, have ".SYSTEM" file
        ldy     MyName                          ; length of our own file name
        cpy     LENGTH                          ; matches?
        bne     CkExec                          ; nope, see if we should exec
        ; loop to check if this is our own name
:       lda     (POINTER),y
        cmp     MyName,y
        bne     CkExec                          ; no match, see if we should exec
        dey
        bne     :-
        ; if we got here, we have found our own self
        sec
        ror     FdSelf                          ; flag it
        ; go to next entry
NxtEnt: lda     POINTER                         ; low byte of entry pointer
        clc                                     ; ready for addition
        adc     #$27                            ; add entry length that is
SMENTL  = * - 1                                 ; self-modifed
        sta     POINTER                         ; save it
        bcc     :+                              ; no need to do high byte if no carry
        inc     POINTER+1                       ; only increment if carry
:       inc     ENTNUM                          ; next entry number
        lda     ENTNUM                          ; and get it
        cmp     #$0D                            ; check against number of entries that is
SMEPB   = * - 1                                 ; self-modified
        bcc     FEntLp                          ; back to main search if not done with this block
        lda     BLOCKBUF+$02                    ; update PL_BLOCK_READ for next directory block
        sta     PL_READ_BLOCK+4
        lda     BLOCKBUF+$03
        sta     PL_READ_BLOCK+5
        ora     PL_READ_BLOCK+4                 ; see if pointer is $00
        beq     NoSys                           ; error if we hit the end and found nothing..
        jsr     GetBlock                        ; get next volume directory block
        lda     #$00
        sta     ENTNUM                          ; update current entry number
        lda     #<(BLOCKBUF+$04)                ; and reset pointer
        sta     POINTER
        lda     #>(BLOCKBUF+$04)
        sta     POINTER+1
        jmp     FEntLp                          ; go back to main loop
CkExec: bit     FdSelf                          ; did we find our own name yet?
        bpl     NxtEnt                          ; nope... go to next entry
        ldx     PATHBUF                         ; get length of path in path buffer
        beq     CpyNam                          ; skip looking for / if zero
:       dex                                     ; 
        beq     CpyNam                          ; done if zero
        lda     PATHBUF,x                       ; 
        eor     #$2F                            ; is '/'?
        asl     a                               ; in roundabout way
        bne     :-                              ; no slash
        ; copy file name onto path, x already has position
CpyNam: ldy     #$00
:       iny                                     ; next source byte offset
        inx                                     ; next dest byte offset
        lda     (POINTER),y                     ; get filename char
        sta     PATHBUF,x                       ; put in path
        cpy     LENGTH                          ; copied all the chars?
        bcc     :-                              ; nope
        stx     PATHBUF                         ; update length of path
        jmp     LaunchSys                       ; try to launch it!
NoSys:  jsr     iprint
        .byte   $8D, $8D, $8D
        hasc    "* Unable to find next '.SYSTEM' file *"
        .byte   $8D, $00
        ; wait for keyboard then quit to ProDOS
        bit     KBDSTR
:       lda     KBD
        bpl     :-   
        bit     KBDSTR
        jmp     DoQuit
.endproc
; ----------------------------------------------------------------------------
; inline print routine
; print chars after JSR until $00 encountered
; converts case via CaseCv ($FF = no conversion, $DF = to upper)
.proc   iprint
        pla
        sta     POINTER
        pla
        sta     POINTER+1
        bne     next
:       cmp     #$E1
        bcc     noconv
        and     CaseCv
noconv: jsr     COUT
next:   inc     POINTER
        bne     nohi
        inc     POINTER+1
nohi:   ldy     #$00
        lda     (POINTER),y
        bne     :-
        lda     POINTER+1
        pha
        lda     POINTER
        pha
        rts
.endproc
; ----------------------------------------------------------------------------
; print one or two decimal digits
.proc   PrDec
        ldx     #$B0                            ; tens digit
        cmp     #$0A
        bcc     onedig
:       sbc     #$0A                            ; repeated subtraction, carry is already set
        inx
        cmp     #$0A                            ; less than 10 yet?
        bcs     :-                              ; nope
onedig: pha
        cpx     #$B0
        beq     nozero                          ; skip printing leading zero
        txa
        jsr     COUT
nozero: pla
        ora     #$B0
        jsr     COUT
        rts
.endproc
CaseCv: .byte   $FF	                        ; default case conversion byte = none
; ----------------------------------------------------------------------------
.proc   DoQuit
        jsr     PRODOS
        .byte   $65                             ; MLI QUIT
        .word   PL_QUIT
        brk                                     ; crash into monitor if QUIT fails
        rts                                     ; (!) if that doesn't work, go back to caller
PL_QUIT:
        .byte   $04                             ; param count
        .byte   $00                             ; quit type - $00 is only type
        .word   $0000                           ; reserved
        .byte   $00                             ; reserved
        .word   $0000                           ; reserved
.endproc
.proc   GetBlock
        jsr     PRODOS
        .byte   $80                             ; READ_BLOCK
        .word   PL_READ_BLOCK
        bcs     LaunchFail
        rts
.endproc
PL_READ_BLOCK:
        .byte   $03
        .byte   $60                             ; unit number
        .word   BLOCKBUF
        .word   $0002                           ; first volume directory block
; ----------------------------------------------------------------------------
; launch next .SYSTEM file
.proc   LaunchSys
        jsr     PRODOS
        .byte   $C8                             ; OPEN
        .word   PL_OPEN
        bcs     LaunchFail
        lda     PL_OPEN+$05                     ; copy ref number
        sta     PL_READ+$01                     ; into READ parameter list
        jsr     PRODOS
        .byte   $CA                             ; READ
        .word   PL_READ
        bcs     LaunchFail
        ; bug the first:  Close should be done every time the OPEN call is successful
        ; but only done when the READ succeeds
        ; bug the second:  Others may not consider this a bug, but we close all open
        ; files, even if we didn't open them.  That's not very polite.
        jsr     PRODOS
        .byte   $CC                             ; CLOSE
        .word   PL_CLOSE
        bcs     LaunchFail
        jmp     SYSEXEC
.endproc
; ----------------------------------------------------------------------------
; failed to launch next .SYSTEM file
.proc   LaunchFail
        pha
        jsr     iprint
        .byte   $8D, $8D, $8D
        hasc    "**  Disk Error $"
        .byte   $00
        pla
        jsr     PRBYTE
        jsr     iprint
        hasc    "  **"
        .byte   $8D, $00
        ; wait for keyboard, then quit to ProDOS
        bit     KBDSTR
:       lda     KBD
        bpl     :-
        bit     KBDSTR
        jmp     DoQuit
.endproc
; ----------------------------------------------------------------------------
PL_OPEN:
        .byte   $03
        .word   PATHBUF
        .word   BLOCKBUF
        .byte   $01                             ; ref num (default 1 wtf?)
PL_READ:
        .byte   $04
        .byte   $01                             ; ref num
        .word   SYSEXEC                         ; data buffer
        .word   $FFFF                           ; request count
        .word   $0000                           ; transfer count
PL_CLOSE:
        .byte   $01
        .byte   $00                             ; ref num $00 = all files
; ----------------------------------------------------------------------------
FdSelf: .byte   $00                             ; bit 7 set if we found our name in volume dir
system: .byte   ".SYSTEM"
MyName: .byte   $0F,"NS.CLOCK.SYSTEM"           ; overwritten in the usual case
; actual clock driver follows
ClockDrv:
        php
        sei
        lda     $C00B                           ; Workaround for Ultrawarp bug
        lda     C8OFF
CDRdSwL = * - 2                                 ; may modify to read INTCXROM state
CDRdSwH = * - 1
        ; we save the value in case we have been modified to read state
        ; of a soft switch
        pha
        sta     SLOT3ROM                        ; enable C8xx ROM for slot, default $C300 ROM
                                                ; seems to me that during probing, it might accidentally
                                                ; turn on a SoftCard-compatible CP/M card and crash...
CDStRmL = * - 2                                 ; may modify to set INTCXROMON
CDStRmH = * - 1
        lda     SLOT3ROM+$04                    ; will be modified to correct loc
CDRstA2 = * - 1
        ldx     #$08
ubytlp: lda     DSUnlk,X
CDULSqL = * - 2                                 ; to be patched later when relocated into ProDOS
CDULSqH = * - 1
        sec                                     ; bit 7 of a is going to be set
        ror     a                               ; first bit in byte of unlock seq in carry & b7 = 1
ubitlp: pha                                     ; remaining bits are in a, so save it
        lda     #$00
        rol     a                               ; move unlock bit into low bit
        tay                                     ; put into y
        lda     SLOT3ROM,Y                      ; NSC looks for unlock on A0 line
CDUnlck = * - 1                                 ; may be patched to a different ROM loc
        pla                                     ; restore unlock seq in progress
        lsr     a                               ; next bit into cary
        bne     ubitlp                          ; do again until that 1 bit we set above rolls off
        dex                                     ; next byte of unlock sequence
        bne     ubytlp
        ldx     #$08                            ; loop counter to read 8 bytes of clock data
rbytlp: ldy     #$08                            ; loop counter to read 8 bits of clock data
rbitlp: pha
        lda     SLOT3ROM+$04                    ; get data bit (NSC clocks reads off of A2)
CDRdClk = * - 1                                 ; may be patched for different loc
        ror     a                               ; put into carry
        pla
        ror     a                               ; then rotate into relative i/p buffer loc
        dey
        bne     rbitlp                          ; next bit
        pha                                     ; now BCD->Binary
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        tay
        pla
        and     #$0F
        clc                                     ; repeated addition for tens
:       dey
        bmi     notens                          ; no tens digit
        adc     #$0A
        bne     :-
notens: sta     INBUF-1,X                       ; update value to binary equiv
        bne     rbytlp                          ; next byte
        ; put values into ProDOS time locations
        lda     INBUF+4
        sta     TIMEHI
        lda     INBUF+5
        sta     TIMELO
        lda     INBUF+1
        asl     a
        asl     a
        asl     a
        asl     a
        asl     a
        ora     INBUF+2
        sta     DATELO 
        lda     INBUF
        rol     a
        sta     DATEHI
        pla                                     ; Bit 7 clear if we need to hit soft switch
        bmi     :+
        sta     C8OFF                           ; safely default to release C8xx ROM space
CDRsSwL = * - 2                                 ; may be modified to set INTCXROMOFF
CDRsSwH = * - 1
:       plp
        rts
; Dallas unlock sequence
DSUnlk  = * - 1
        .byte   $5C, $A3, $3A, $C5, $5C, $A3, $3A, $C5
DrvSize = * - ClockDrv
.assert DrvSize < CLKCODEMAX, error, "NS CLOCK driver too big"

; ----------------------------------------------------------------------------
; the rest of this looks like junk that was accidentally saved with the file
; there are no references from the previous code.  Didn't feel like disassembling.

.if IncludeJunk
        .setcpu "6502"

BASWARM         := $D43F                ; warm start BASIC
FNDLIN          := $D61A               ; BASIC search for line
SETPTRS         := $D665
L008D           := $008D
L0EA1           := $0EA1
L161F           := $161F
L2020           := $2020
L434F           := $434F
L522F           := $522F
L9A17           := $9A17
LAC08           := $AC08
LAC0D           := $AC0D
LAC1F           := $AC1F
LAC6B           := $AC6B
LAD50           := $AD50
LAFD3           := $AFD3
LAFD7           := $AFD7
LB1D3           := $B1D3
LB531           := $B531
LBE70           := $BE70
LFFFF           := $FFFF

        .byte   $00
        stz     $F5,x                           ; 147C 74 F5                    t.
        .byte   $D3                             ; 147E D3                       .
        adc     $8DB3                           ; 147F 6D B3 8D                 m..
        lsr     $BE,x                           ; 1482 56 BE                    V.
        jmp     L2020                           ; 1484 4C 20 20                 L  
; ----------------------------------------------------------------------------
        bbr2    $43,L14DE                       ; 1487 2F 43 54                 /CT
        rol     $2031                           ; 148A 2E 31 20                 .1 
        sta     $AE00                           ; 148D 8D 00 AE                 ...
        lda     $CABB,x                         ; 1490 BD BB CA                 ...
        stx     $74                             ; 1493 86 74                    .t
        jsr     LAC0D                           ; 1495 20 0D AC                  ..
        ldx     $20A9                           ; 1498 AE A9 20                 .. 
        bbr2    $48,L14DF                       ; 149B 2F 48 41                 /HA
        eor     ($44)                           ; 149E 52 44                    RD
        and     ($20),y                         ; 14A0 31 20                    1 
        sta     $A400                           ; 14A2 8D 00 A4                 ...
        lda     #$00                            ; 14A5 A9 00                    ..
        beq     L14BE                           ; 14A7 F0 15                    ..
        lda     #$00                            ; 14A9 A9 00                    ..
        sta     $BE44                           ; 14AB 8D 44 BE                 .D.
        jsr     L522F                           ; 14AE 20 2F 52                  /R
        eor     ($4D,x)                         ; 14B1 41 4D                    AM
        jsr     L008D                           ; 14B3 20 8D 00                  ..
        jsr     LAC08                           ; 14B6 20 08 AC                  ..
        bcs     L150B                           ; 14B9 B0 50                    .P
        jsr     SETPTRS                           ; 14BB 20 65 D6                  e.
L14BE:  sta     $D8                             ; 14BE 85 D8                    ..
        jsr     L0EA1                           ; 14C0 20 A1 0E                  ..
        bbr2    $4E,L1515                       ; 14C3 2F 4E 4F                 /NO
        rol     $4C53                           ; 14C6 2E 53 4C                 .SL
        bbr4    $54,L14FA                       ; 14C9 4F 54 2E                 OT.
        .byte   $43                             ; 14CC 43                       C
        jmp     L434F                           ; 14CD 4C 4F 43                 LOC

; ----------------------------------------------------------------------------
        .byte   $4B                             ; 14D0 4B                       K
        jsr     L008D                           ; 14D1 20 8D 00                  ..
        lda     #$FF                            ; 14D4 A9 FF                    ..
        jsr     L522F                           ; 14D6 20 2F 52                  /R
        eor     ($4D,x)                         ; 14D9 41 4D                    AM
        jsr     L008D                           ; 14DB 20 8D 00                  ..
L14DE:  .byte   $76                             ; 14DE 76                       v
L14DF:  bbs1    $4C,L1525                       ; 14DF 9F 4C 43                 .LC
        tay                                     ; 14E2 A8                       .
        jsr     LAC08                           ; 14E3 20 08 AC                  ..
        bcs     L150B                           ; 14E6 B0 23                    .#
        jsr     SETPTRS                           ; 14E8 20 65 D6                  e.
        jsr     L9A17                           ; 14EB 20 17 9A                  ..
        lda     #$00                            ; 14EE A9 00                    ..
        sta     $24                             ; 14F0 85 24                    .$
        jmp     BASWARM                           ; 14F2 4C 3F D4                 L?.

; ----------------------------------------------------------------------------
        jsr     LB531                           ; 14F5 20 31 B5                  1.
        bcs     L150B                           ; 14F8 B0 11                    ..
L14FA:  jsr     LAC1F                           ; 14FA 20 1F AC                  ..
        bcs     L150B                           ; 14FD B0 0C                    ..
        sty     $6B                             ; 14FF 84 6B                    .k
        sty     $69                             ; 1501 84 69                    .i
        sty     $6D                             ; 1503 84 6D                    .m
        stx     $6C                             ; 1505 86 6C                    .l
        stx     $6A                             ; 1507 86 6A                    .j
        stx     $6E                             ; 1509 86 6E                    .n
L150B:  rts                                     ; 150B 60                       `

; ----------------------------------------------------------------------------
        lda     #$01                            ; 150C A9 01                    ..
        ldx     #$FC                            ; 150E A2 FC                    ..
        jsr     LB1D3                           ; 1510 20 D3 B1                  ..
        bcs     L150B                           ; 1513 B0 F6                    ..
L1515:  lda     #$D1                            ; 1515 A9 D1                    ..
        jsr     LBE70                           ; 1517 20 70 BE                  p.
        bcs     L150B                           ; 151A B0 EF                    ..
        lda     $67                             ; 151C A5 67                    .g
        sta     $BED7                           ; 151E 8D D7 BE                 ...
        adc     $BEC8                           ; 1521 6D C8 BE                 m..
        .byte   $8D                             ; 1524 8D                       .
L1525:  cli                                     ; 1525 58                       X
        ldx     $68A5,y                         ; 1526 BE A5 68                 ..h
        sta     $BED8                           ; 1529 8D D8 BE                 ...
        adc     $BEC9                           ; 152C 6D C9 BE                 m..
        sta     $BE59                           ; 152F 8D 59 BE                 .Y.
        bcs     L1536                           ; 1532 B0 02                    ..
        cmp     $74                             ; 1534 C5 74                    .t
L1536:  lda     #$0E                            ; 1536 A9 0E                    ..
        bcs     L150B                           ; 1538 B0 D1                    ..
        ldx     $BEC8                           ; 153A AE C8 BE                 ...
        ldy     $BEC9                           ; 153D AC C9 BE                 ...
        jsr     LAFD7                           ; 1540 20 D7 AF                  ..
        bcs     L150B                           ; 1543 B0 C6                    ..
        jsr     LAFD3                           ; 1545 20 D3 AF                  ..
        bcs     L150B                           ; 1548 B0 C1                    ..
        jsr     LAC6B                           ; 154A 20 6B AC                  k.
        ldx     $BE59                           ; 154D AE 59 BE                 .Y.
        ldy     $BE58                           ; 1550 AC 58 BE                 .X.
        stx     $B0                             ; 1553 86 B0                    ..
        sty     $AF                             ; 1555 84 AF                    ..
        rts                                     ; 1557 60                       `

; ----------------------------------------------------------------------------
        sec                                     ; 1558 38                       8
        lda     $67                             ; 1559 A5 67                    .g
        sbc     $BEB9                           ; 155B ED B9 BE                 ...
        sta     $3C                             ; 155E 85 3C                    .<
        lda     $68                             ; 1560 A5 68                    .h
        sbc     $BEBA                           ; 1562 ED BA BE                 ...
        sta     $3D                             ; 1565 85 3D                    .=
        ora     $3C                             ; 1567 05 3C                    .<
        clc                                     ; 1569 18                       .
        beq     L15B1                           ; 156A F0 45                    .E
        ldx     $67                             ; 156C A6 67                    .g
        lda     $68                             ; 156E A5 68                    .h
L1570:  stx     $3A                             ; 1570 86 3A                    .:
        sta     $3B                             ; 1572 85 3B                    .;
        ldy     #$01                            ; 1574 A0 01                    ..
        lda     ($3A),y                         ; 1576 B1 3A                    .:
        dey                                     ; 1578 88                       .
        ora     ($3A),y                         ; 1579 11 3A                    .:
        beq     L15B1                           ; 157B F0 34                    .4
        lda     ($3A),y                         ; 157D B1 3A                    .:
        adc     $3C                             ; 157F 65 3C                    e<
        tax                                     ; 1581 AA                       .
        sta     ($3A),y                         ; 1582 91 3A                    .:
        iny                                     ; 1584 C8                       .
        lda     ($3A),y                         ; 1585 B1 3A                    .:
        adc     $3D                             ; 1587 65 3D                    e=
        sta     ($3A),y                         ; 1589 91 3A                    .:
        clc                                     ; 158B 18                       .
        bcc     L1570                           ; 158C 90 E2                    ..
        lda     $BE57                           ; 158E AD 57 BE                 .W.
        and     #$08                            ; 1591 29 08                    ).
        clc                                     ; 1593 18                       .
        beq     L15B1                           ; 1594 F0 1B                    ..
        lda     $BE68                           ; 1596 AD 68 BE                 .h.
        sta     $50                             ; 1599 85 50                    .P
        lda     $BE69                           ; 159B AD 69 BE                 .i.
        sta     $51                             ; 159E 85 51                    .Q
        jsr     FNDLIN                           ; 15A0 20 1A D6                  ..
        clc                                     ; 15A3 18                       .
        lda     $9B                             ; 15A4 A5 9B                    ..
        adc     #$FF                            ; 15A6 69 FF                    i.
        sta     $B8                             ; 15A8 85 B8                    ..
        lda     $9C                             ; 15AA A5 9C                    ..
        adc     #$FF                            ; 15AC 69 FF                    i.
        sta     $B9                             ; 15AE 85 B9                    ..
        clc                                     ; 15B0 18                       .
L15B1:  rts                                     ; 15B1 60                       `

; ----------------------------------------------------------------------------
        bcc     L15D6                           ; 15B2 90 22                    ."
        lda     #$FC                            ; 15B4 A9 FC                    ..
        sta     $BE6A                           ; 15B6 8D 6A BE                 .j.
        sta     $BEB8                           ; 15B9 8D B8 BE                 ...
        lda     #$C3                            ; 15BC A9 C3                    ..
        sta     $BEB7                           ; 15BE 8D B7 BE                 ...
        lda     $67                             ; 15C1 A5 67                    .g
        sta     $BEA5                           ; 15C3 8D A5 BE                 ...
        sta     $BEB9                           ; 15C6 8D B9 BE                 ...
        lda     $68                             ; 15C9 A5 68                    .h
        sta     $BEA6                           ; 15CB 8D A6 BE                 ...
        sta     $BEBA                           ; 15CE 8D BA BE                 ...
        jsr     LAD50                           ; 15D1 20 50 AD                  P.
        bcs     L161F                           ; 15D4 B0 49                    .I
L15D6:  lda     #$02                            ; 15D6 A9 02                    ..
        ldx     #$FC                            ; 15D8 A2 FC                    ..
        jsr     LB1D3                           ; 15DA 20 D3 B1                  ..
        bcs     L161F                           ; 15DD B0 40                    .@
        lda     $AF                             ; 15DF A5 AF                    ..
        sec                                     ; 15E1 38                       8
        sbc     $67                             ; 15E2 E5 67                    .g
        tax                                     ; 15E4 AA                       .
        sta     $BEC8                           ; 15E5 8D C8 BE                 ...
        lda     $B0                             ; 15E8 A5 B0                    ..
        sbc     $68                             ; 15EA E5 68                    .h
        tay                                     ; 15EC A8                       .
        sta     $BEC9                           ; 15ED 8D C9 BE                 ...
        lda     #$00                            ; 15F0 A9 00                    ..
        sta     $BECA                           ; 15F2 8D CA BE                 ...
        lda     $67                             ; 15F5 A5 67                    .g
        sta     $BED7                           ; 15F7 8D D7 BE                 ...
        lda     $68                             ; 15FA A5 68                    .h
        sta     $BED8                           ; 15FC 8D D8 BE                 ...
        .byte   $20                             ; 15FF 20                        
.endif
