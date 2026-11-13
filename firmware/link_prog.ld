/*
 * The program (program.bin): code, data and bss in the SDRAM at PROG_BASE (hw.h), run
 * through the caches; the stack at the top of the boot RAM, which the boot loader no
 * longer needs, and the program's hottest code below it (.bootcode). The Pocket loads
 * program.bin with data slot 2, so .data needs no copy; start_prog.S copies .bootcode
 * into the boot RAM and clears .tbss and .bss. program.bin holds exactly __image_start ..
 * __image_end and starts with the header the boot loader checks.
 * link_check.py (the Makefile runs it on the map) checks that every section the two lists
 * below name is where the list puts it: a pattern that matches nothing is no error to the linker.
 */
OUTPUT_ARCH(riscv)
ENTRY(prog_entry)

MEMORY
{
    SDRAM (rwx) : ORIGIN = 0x13000000, LENGTH = 15M     /* above: the saves (save.h) */
    BOOT  (rwx) : ORIGIN = 0x00000000, LENGTH = 24K     /* the boot RAM below the stack: the loader that was here is done */
}

CODE_PAD = DEFINED(CODE_PAD) ? CODE_PAD : 0;
CODE_PAD2 = DEFINED(CODE_PAD2) ? CODE_PAD2 : 0;

SECTIONS
{
    .header : {
        __image_start = .;
        KEEP(*(.text.header))       /* start_prog.S: header, entry, trap entry */
        KEEP(*(.text.start))
    } > SDRAM

    /* The code the I-cache misses on most (link_boot.ld lists it), run from the boot RAM: a miss there takes some 11 clocks (a line a word a clock, no waiting for the SDRAM, which
       the geometry unit and the rasterizer share), one on the SDRAM 30 to 85 (docs/screamer2/performance.md, "Where the CPU waits"). Its image lies in program.bin right after
       the header, start_prog.S copies it. The section comes before .text in this script because .text takes every .text.* it is not told otherwise. */
    .bootcode : ALIGN(32) {
        __boot_start = .;
        INCLUDE link_boot.ld
        . = ALIGN(32);
        __boot_end = .;
    } > BOOT AT> SDRAM
    __boot_load = LOADADDR(.bootcode);

    .text (SIZEOF(.bootcode) ? LOADADDR(.bootcode) + SIZEOF(.bootcode) : ADDR(.header) + SIZEOF(.header)) : {
        . = . + CODE_PAD;           /* (0; tools/fw.sh CODE_PAD=n: the code moves n bytes, to see what the caches' alignment costs) */
        /* The code that runs for every piece of a frame, together: the I-cache is 8 KB and direct mapped, so two hot pieces
           of code that lie a multiple of 8 KB apart (mod 8 KB) throw each other out on every piece. With hw_piece (main.c)
           somewhere else, the frame of the race took 0.9 ms more than with a few bytes between the files, and a 40-byte function
           called for every piece from outside this block cost up to 1.7 ms (docs/screamer2/performance.md, section 7 and
           "Where the CPU waits"). Here they cannot collide while the block is under 8 KB (checked below); nothing a piece
           needs may be called from outside it. */
        __hot_start = .;
        *(.text.tl_mark) *(.text.hw_piece) *(.text.push_piece) *(.text.face_eye) *(.text.block_piece) *(.text.hand_over)
        __hot_end = .;
        *(.text.unlikely .text.unlikely.*)
        *(.text.startup .text.startup.*)
        *main.o(.text .text.*)
        . = . + CODE_PAD2;          /* (0; tools/fw.sh CODE_PAD2=n: the code after main.c moves n bytes) */
        *(.text .text.*)
    } > SDRAM

    .rodata : {
        *(.rodata .rodata.*)
    } > SDRAM

    .data : ALIGN(8) {
        *(.data .data.*)
        . = ALIGN(8);
        PROVIDE(__global_pointer$ = . + 0x800);
        *(.srodata.cst16) *(.srodata.cst8) *(.srodata.cst4) *(.srodata.cst2)
        *(.srodata .srodata.*)
        *(.sdata .sdata.*)
        . = ALIGN(8);
    } > SDRAM

    .tdata : ALIGN(8) {
        PROVIDE(__tls_base = .);
        *(.tdata .tdata.*)
        . = ALIGN(4);
        __image_end = .;
    } > SDRAM
    .tbss : ALIGN(8) {
        __bss_start = .;
        *(.tbss .tbss.*)
        *(.tcommon)
    } > SDRAM
    .tbss_space (NOLOAD) : {
        . = ADDR(.tbss) + SIZEOF(.tbss);
    } > SDRAM

    .bss (NOLOAD) : ALIGN(8) {
        *(.sbss .sbss.*)
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > SDRAM

    ASSERT(__hot_end - __hot_start <= 8192, "the hot code (link_prog.ld: tl_mark, hw_piece, push_piece, face_eye, block_piece, hand_over) is over 8 KB: it would collide with itself in the I-cache")

    __stack_top = 0x00008000;       /* top of the 32 KB boot RAM */

    /DISCARD/ : { *(.note.GNU-stack) *(.comment) }
}
