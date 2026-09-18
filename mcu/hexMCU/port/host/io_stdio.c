/* ======================================================================
 * port/host/io_stdio.c -- io.h backend for the native build.
 *
 * Stands in for the UART: raw-ish terminal in, unbuffered stdout out.
 * The terminal state we change belongs to the shell that launched us, so
 * every exit path has to put it back.
 * ====================================================================== */

/* termios / read() are POSIX, not ISO C -- ask for them explicitly so this
 * file builds under -std=c11 as well as -std=gnu11. */
#define _POSIX_C_SOURCE 200809L

#include "io.h"

#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

static struct termios saved;
static bool           raw_active;     /* did we change termios?          */
static int            saved_flags;    /* stdin O_* flags as we found them */
static bool           flags_saved;
static bool           stdin_is_tty;

/* Both tcsetattr() and fcntl() are async-signal-safe, so this is callable
 * from a signal handler as well as from atexit(). */
static void restore_tty(void)
{
    if (raw_active) {
        tcsetattr(STDIN_FILENO, TCSANOW, &saved);
        raw_active = false;
    }
    /* O_NONBLOCK lives on the open file description, which we share with
     * the parent shell. Leave it set and the shell inherits a non-blocking
     * stdin, which makes it misbehave in ways that look unrelated to us. */
    if (flags_saved) {
        fcntl(STDIN_FILENO, F_SETFL, saved_flags);
        flags_saved = false;
    }
}

static void on_sigint(int sig)
{
    (void)sig;
    restore_tty();
    _exit(130);        /* 128 + SIGINT, the usual shell convention */
}

void io_init(void)
{
    stdin_is_tty = isatty(STDIN_FILENO);

    /* Non-blocking ALWAYS, tty or pipe. io_getc() is called from the main
     * loop, and a blocking read() there would park the whole program --
     * transport_poll() would never run and no transfer could ever finish.
     * On hardware io_getc() just checks a ring buffer and returns; this is
     * what makes the host behave the same way. */
    int fl = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (fl != -1) {
        saved_flags = fl;
        flags_saved = true;
        fcntl(STDIN_FILENO, F_SETFL, fl | O_NONBLOCK);
    }

    /* stdout unbuffered so output appears as it is written, like a UART,
     * and so it is not lost if we exit or abort mid-line. */
    setvbuf(stdout, NULL, _IONBF, 0);

    /* Registered even for piped input: the flags restore above still
     * needs to run. Ordering matters -- nothing may exit between the
     * fcntl/tcgetattr above and these registrations. */
    atexit(restore_tty);
    signal(SIGINT, on_sigint);

    if (!stdin_is_tty) return;                /* piped input: no termios */

    if (tcgetattr(STDIN_FILENO, &saved) != 0) return;

    struct termios raw = saved;
    raw.c_lflag &= ~(ICANON | ECHO);          /* ISIG kept: ctrl-C works */
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0)
        raw_active = true;
}

void io_write(const char *buf, uint16_t n)
{
    fwrite(buf, 1, n, stdout);
    fflush(stdout);                           /* no buffering: match UART */
}

int io_getc(void)
{
    unsigned char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);

    if (n == 1) return c;

    /* On a tty with VMIN=0/VTIME=0, read() returns 0 when there is simply
     * nothing pending -- that is the normal idle case, not EOF. On a pipe
     * or a redirected file, 0 really is EOF, and returning -1 forever
     * would spin the main loop with no way to ever end the run. */
    if (n == 0 && !stdin_is_tty) exit(0);

    return -1;                                /* idle, or EAGAIN */
}