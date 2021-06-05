# `NS.CLKUW.SYSTEM`

## No Slot Clock Driver for ProDOS

`NS.CLKUW.SYSTEM` is a version of the original No Slot Clock Driver
`NS.CLOCK.SYSTEM`, modified to work with the RM Ultrawarp accelerator.

`nsc.s` is the original No Slot Clock driver written by SMT, which has been
disassembled and commented by Github user mgcaret.  I took it from
[here](https://gist.github.com/mgcaret/ae2860c754fd029d2640107c4fe0bffd).

The RM Ultrawarp Apple II accelerator card has some sort of bug in the
banking of ROMs.  As a result of this problem, the original `NS.CLOCK.SYSTEM`
does not work on a system with Ultrawarp, unless the accelerator card is
completely disabled.  The symptom is that the NSC is detected and the ProDOS
driver is installed, but the time returned is all zeroes.  It does not appear
to be a problem related to the 13MHz clock speed of the Ultrawarp accelerator
because it also manifests if the Ultrawarp is active but with 1MHz clock.

`nsc_ultrawarp.s` is a modified version of nsc.s which includes a workaround
for this problem.  It has the following differences compared to `nsc.s`:

  - The original code searches for an NSC installed under peripheral card
    ROMs in each of the Apple II slots, and only then searches for a NSC
    installed in the normal place, which is underneath the CE or CX ROM on
    an Apple //e, or the Monitor ROM on an original Apple II or plus.  I
    removed the search through the slots, and now only look for an NSC
    installed under the CE/CX ROM or Monitor ROM.
  - In the original code, the driver code itself is modified in place to
    work with the NSC in the location where it was detected.  This is no
    longer required, so I removed the self-modifying code.  This makes the
    code easier to read, and also makes the clock driver a few bytes shorter,
    which is critical, as there was no space to add any additional code.
  - Added an additional instruction `LDA $C00B` at the beginning of the clock
    driver.  This causes the driver to work properly with the Ultrawarp
    active, either at full speed (13MHz) or 1MHz.  The reason adding this one
    read fixes the erroneous behaviour is not well understood.  It seems that
    the additional instruction causes the CE/CX ROM to be banked in.  Without
    this extra instruction, it seems the ROM was not banked in when running
    with the Ultrawarp.  This appears to be a hardware bug in the Ultrawarp.

Bobbi
May 29, 2021

