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
}

CODE_PAD = DEFINED(CODE_PAD) ? CODE_PAD : 0;
CODE_PAD2 = DEFINED(CODE_PAD2) ? CODE_PAD2 : 0;

SECTIONS
{
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

    ASSERT(__hot_end - __hot_start <= 8192, "the hot code (tl_mark, hw_piece, face_eye, block_piece, draw) is over 8 KB: it would collide with itself in the I-cache")

    __stack_top = 0x00008000;       /* top of the 32 KB boot RAM */

    /DISCARD/ : { *(.note.GNU-stack) *(.comment) }
}
