/* ======================================================================
 * host_main.c -- entry point for the native build.
 *
 * Stands in for CubeMX's main(): parse configuration, then call the same
 * myMain() the firmware calls. Nothing else belongs here.
 * ====================================================================== */
#include "host_hooks.h"
#include "display_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void myMain(void);

static void usage(const char *prog)
{
    printf("usage: %s [options]\n"
           "  --mhz=N        OCTOSPI clock in MHz (default 20)\n"
           "  --lanes=N      1, 4 or 8 (default 8)\n"
           "  --errors=N     %% of transfers that fail (default 0)\n"
           "  --card=PATH    FAT image to use as the SD card\n"
           "  --image=PATH   BMP shown at start (default host/test.bmp,\n"
           "                 --image= for none)\n"
           "  -h, --help     this text\n", prog);
}

static bool arg_val(const char *arg, const char *key, const char **out)
{
    size_t n = strlen(key);
    if (strncmp(arg, key, n) != 0) return false;
    *out = arg + n;
    return true;
}

int main(int argc, char **argv)
{
    /* Before any output: setvbuf is only valid before the first I/O. */
    setvbuf(stdout, NULL, _IONBF, 0);

    double      mhz    = 20.0;
    unsigned    lanes  = 8;
    unsigned    errors = 0;
    const char *card   = getenv("CARD_IMAGE");   /* NULL when unset */
    const char *image  = NULL;
#ifdef DEFAULT_IMAGE
    image = DEFAULT_IMAGE;
#endif
    const char *v;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if (arg_val(argv[i], "--mhz=", &v)) {
            mhz = strtod(v, NULL);
        } else if (arg_val(argv[i], "--lanes=", &v)) {
            lanes = (unsigned)strtoul(v, NULL, 10);
        } else if (arg_val(argv[i], "--errors=", &v)) {
            errors = (unsigned)strtoul(v, NULL, 10);
        } else if (arg_val(argv[i], "--card=", &v)) {
            card = v;
        } else if (arg_val(argv[i], "--image=", &v)) {
            image = (*v != '\0') ? v : NULL;
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    host_fpga_set_clock(mhz, lanes);
    host_fpga_set_error_rate(errors);

    /* Nothing opens the image here -- disk_initialize does, on the first
     * f_mount inside myMain. A bad path shows up as FR_NOT_READY there. */
    if (card) host_disk_set_image(card);

    if (display_init("hexmcu fake fpga", 2)) {
        atexit(display_shutdown);
        if (image) display_load_bmp(image);
    }

    myMain();
    return 0;
}