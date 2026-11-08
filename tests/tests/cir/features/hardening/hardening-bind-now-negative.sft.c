//type:fn
//options:--gcc -Wl,-z,now:--gcc -Wl,-z,relro
//require:BACK_END_TYPE 3
/* eccp rejects linker options (-Wl,...) as unknown: the link is the gcc
   driver's with its defaults (see hardening-relro.sft.c). */
int main(void) { return 0; }
