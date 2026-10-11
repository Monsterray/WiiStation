#include <stdio.h>
#include "../deps/rcheevos/include/rc_hash.h"
#include "../deps/rcheevos/include/rc_consoles.h"
int main(int argc, char **argv) {
    char hash[33];
    if (argc != 2) return 2;
    if (!rc_hash_generate_from_file(hash, RC_CONSOLE_PLAYSTATION, argv[1])) {
        fprintf(stderr, "Cannot identify PS1 executable\n");
        return 1;
    }
    puts(hash);
    return 0;
}
