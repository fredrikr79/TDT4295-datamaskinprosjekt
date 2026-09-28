#ifndef DISPLAY_HOST_H
#define DISPLAY_HOST_H

#include <stdbool.h>
#include <stdint.h>

#define DISPLAY_W 854 
#define DISPLAY_H 480

bool      display_init(const char *title, int scale);
bool      display_load_bmp(const char *path);   /* into the framebuffer */
uint32_t *display_framebuffer(void);            /* DISPLAY_W*DISPLAY_H XRGB8888 */
bool      display_poll(void);                   /* false once the window closes */
void      display_shutdown(void);

#endif /* DISPLAY_HOST_H */