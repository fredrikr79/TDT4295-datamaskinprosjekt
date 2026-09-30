#pragma once
/* Host-build-only glue between platform_host.c and the fake FPGA.
 * Nothing in app/ may include this. */

#include <stdbool.h>

/* Level of the simulated FPGA_READY pin. Defined in platform_host.c,
 * driven by transport_host.c, read by plat_fpga_ready_pin(). */
extern bool host_ready_pin;

/* Fake FPGA tunables, so the CLI or argv can change them at runtime. */
void host_fpga_set_clock(double mhz, unsigned lanes);
void host_fpga_set_error_rate(unsigned percent);


// disk hooks
void        host_disk_set_image(const char *path);
void        host_disk_fail_next_reads(int n);
void        host_disk_fail_next_writes(int n);
const char *host_disk_image_path(void);
