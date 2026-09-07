// The most direct demonstration: the program writes a known value into what it thinks
// is its own buffer, and returns to that value.
extern void sink(char *);
void selfclobber(void) { char buf[64]; sink(buf); *(volatile unsigned *)(buf + 56) = 0x11223344; }
