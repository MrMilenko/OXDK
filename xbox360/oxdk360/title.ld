/* A title's layout. Page protection is per 64K, so anything the loader must treat
   differently starts on a 64K boundary: read-only data, then code, then writable data.
   Entry is mainCRTStartup out of xapilib, so the XDK's own startup runs first. */

SECTIONS
{
    . = 0x82000000;
    .pehdr : { *(.pehdr) }

    . = 0x82000400;
    .rdata : {
        *(.rdata) *(.rdata.*) *(SORT_BY_NAME(.rdata$*))
        *(.rodata) *(.rodata.*) *(.sdata2)

        /* The CRT's initialiser tables. Microsoft builds them out of .CRT$XXX input
           sections and relies on the linker sorting by name: .CRT$XIA is the start
           marker, .CRT$XIC and friends are the entries, .CRT$XIZ is the end marker,
           and mainCRTStartup walks __xi_a to __xi_z calling what it finds. Emitted in
           input order the markers end up adjacent, the table reads as empty and
           nothing runs: stdio never initialises and every fopen fails with EMFILE.

           clang puts C++ static constructors in .init_array instead, which that walk
           knows nothing about. .CRT$XCA is where __xc_a lives, so placing .init_array
           immediately after it puts our constructors inside the bounds the startup
           already walks, and they run alongside Microsoft's own. Without that a global
           C++ object is never constructed and its members read back as zeroes, which
           is how a std::map ends up walking into a null node. */
        . = ALIGN(4);
        *(SORT_BY_NAME(.CRT$XCA))
        __init_array_start = .;
        *(.init_array) *(.init_array.*) *(.ctors) *(.ctors.*)
        __init_array_end = .;
        *(SORT_BY_NAME(.CRT$*))
        *(.CRT)

        /* Same scheme, different table: .XBMOVIE$A and $ZZZZZZZZ bracket the entries
           between them, and unsorted the end marker lands before the entry so the table
           reads as empty. Any other $-grouped family the XDK adds wants a line here
           too; a family with no rule becomes an orphan and is placed by lld's own
           heuristic rather than by this script. */
        . = ALIGN(4);
        *(SORT_BY_NAME(.XBMOVIE$*))

        __fini_array_start = .;
        *(.fini_array) *(.fini_array.*) *(.dtors) *(.dtors.*)
        __fini_array_end = .;
    }

    /* Every section must start on a multiple of the PE SectionAlignment, which cxex
       sets to 0x1000. Letting .xdata follow .pdata directly puts it at an unaligned
       address and the loader refuses the image. */
    . = ALIGN(0x10000);
    .pdata : { *(.pdata) *(.pdata.*) }
    . = ALIGN(0x1000);
    .xdata : { *(.xdata) *(.xdata.*) *(SORT_BY_NAME(.xdata$*)) }

    . = ALIGN(0x10000);
    /* MSVC groups sections with a $ suffix and expects the linker to sort and
       concatenate them. Those names match neither .text nor .text.*, so without a rule
       here they become orphans and lld places them by its own heuristic. It currently
       puts them next to the section they resemble, which lands them inside the right
       64K protection page by luck rather than by design. */
    .text  : { *(.text) *(.text.*) *(SORT_BY_NAME(.text$*)) }

    . = ALIGN(0x10000);
    .data  : { *(.data) *(.data.*) *(SORT_BY_NAME(.data$*)) *(SORT_BY_NAME(.rtc$*)) *(.sdata) *(.bss) *(.bss.*) *(.sbss) *(COMMON) *(.tls*) }

    /DISCARD/ : { *(.debug*) *(.comment) *(.note*) *(.eh_frame*) *(.XBLD*) }
}
