// The tenth argument is written into the top parameter slot, which must not be the
// same address as the saved return address. This one branches to 0x40240000.
extern void d10(double,double,double,double,double,double,double,double,double,double);
void outgoing(void) { d10(1,2,3,4,5,6,7,8,9,10); }
