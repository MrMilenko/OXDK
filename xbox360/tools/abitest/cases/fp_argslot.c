// Does a float consume an argument slot? Every integer after it depends on the answer.
extern void mixed(int a, float b, int c, int d, int e, int f, int g, int h, int i);
void callmixed(void) { mixed(1, 2.0f, 3, 4, 5, 6, 7, 8, 9); }
