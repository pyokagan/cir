#include "cir_internal.h"
#include <stdio.h>

int
main(int argc, char *argv[])
{
#if 0
    CirX64_test();
#endif
#if 1
    CirMachine__initBuiltin(&CirMachine__host);
    CirMachine__initBuiltin(&CirMachine__build);
    CirBuiltin_init(&CirMachine__host);

    int i;
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == 'l') {
            CirDl_loadLibrary(&argv[i][2]);
        } else {
            break;
        }
    }
    if (i >= argc)
        cir_fatal("not enough arguments");

    CirLex__init(argv[i], &CirMachine__host);
    cir__parse(&CirMachine__host);
    CirRender();
    return 0;
#endif
}
