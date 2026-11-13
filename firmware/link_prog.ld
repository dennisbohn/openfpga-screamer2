/*
 * The program (program.bin): code, data and bss in the SDRAM at PROG_BASE (hw.h), run
 * through the caches; the stack at the top of the boot RAM, which the boot loader no
 * longer needs. The Pocket loads program.bin with data slot 2, so .data needs no copy;
 * start_prog.S clears .tbss and .bss. program.bin holds exactly __image_start ..
 * __image_end and starts with the header the boot loader checks.
 */
OUTPUT_ARCH(riscv)
ENTRY(prog_entry)

MEMORY
{
    SDRAM (rwx) : ORIGIN = 0x13000000, LENGTH = 15M     /* above: the saves (save.h) */
    BOOT  (rwx) : ORIGIN = 0x00000000, LENGTH = 24K     /* the boot RAM below the stack: the loader that was here is done, the program puts its hottest code in (.bootcode) */
    BOOTIMG (r) : ORIGIN = 0x13058000, LENGTH = 24K     /* where program.bin carries that code's image to the start (past .tdata: the ASSERT below) */
}

CODE_PAD = DEFINED(CODE_PAD) ? CODE_PAD : 0;
CODE_PAD2 = DEFINED(CODE_PAD2) ? CODE_PAD2 : 0;

SECTIONS
{
    /* The code the I-cache misses on most (link_boot.ld lists it; it has to come before .text, which takes every .text.* it is not told otherwise), run from the boot RAM:
       a miss there takes some 11 clocks (a line a word a clock, no waiting for the SDRAM, which the geometry unit and the rasterizer share), one on the SDRAM 30 to 60 and more.
       Its image is in program.bin at BOOTIMG, start_prog.S copies it before it clears the bss (which lies over it). */
    .bootcode : ALIGN(32) {
        __boot_start = .;
        INCLUDE link_boot.ld
        . = ALIGN(32);
        __boot_end = .;
    } > BOOT AT> BOOTIMG
    __boot_load = LOADADDR(.bootcode);

    .text : {
        __image_start = .;
        KEEP(*(.text.header))       /* start_prog.S: header, entry, trap entry */
        KEEP(*(.text.start))
        . = . + CODE_PAD;           /* (0; tools/fw.sh CODE_PAD=n: the code moves n bytes, to see what the caches' alignment costs) */
        /* The code that runs for every piece of a frame, together: the I-cache is 8 KB and direct mapped, so two hot pieces
           of code that lie a multiple of 8 KB apart (mod 8 KB) throw each other out on every piece. With hw_piece (main.c)
           somewhere else, the frame of the race took 0.9 ms more than with a few bytes between the files
           (docs/screamer2/performance.md, section 7). Here they cannot collide while the block is under 8 KB (checked below) */
        __hot_start = .;
        *(.text.tl_mark) *(.text.hw_piece) *(.text.face_eye) *(.text.block_piece) *(.text.draw)
        __hot_end = .;
        *(.text.unlikely .text.unlikely.*)
        *(.text.startup .text.startup.*)
        *main.o(.text .text.*)
        . = . + CODE_PAD2;          /* (0; tools/fw.sh CODE_PAD2=n: the code after main.c moves n bytes, hw_piece against the track's code) */
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
        __tdata_end = .;
    } > SDRAM
    __image_end = SIZEOF(.bootcode) ? LOADADDR(.bootcode) + SIZEOF(.bootcode) : __tdata_end;
    ASSERT(!SIZEOF(.bootcode) || __tdata_end <= ORIGIN(BOOTIMG), "the program has grown over the boot code's image: move BOOTIMG up in link_prog.ld")
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

    ASSERT(__hot_end - __hot_start <= 8192, "the hot code (tl_mark, hw_piece, face_eye, block_piece, draw) is over 8 KB: it would collide with itself in the I-cache")

    __stack_top = 0x00008000;       /* top of the 32 KB boot RAM */

    /DISCARD/ : { *(.note.GNU-stack) *(.comment) }
}
