#include "cir_internal.h"
#include <stdio.h>

int
main(int argc, char *argv[])
{
#if 0
    CirX64_test();
#endif
#if 1
    if (argc != 2)
        cir_fatal("not enough arguments");

    CirMachine__initBuiltin(&CirMachine__host);
    CirMachine__initBuiltin(&CirMachine__build);
    CirLex__init(argv[1], &CirMachine__host);
    cir__parse(&CirMachine__host);
    CirRender();
    return 0;
#endif
}
