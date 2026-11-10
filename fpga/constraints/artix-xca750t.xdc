## RGB LED
set_property -dict {PACKAGE_PIN A8 IOSTANDARD LVCMOS15} [get_ports led_r_n]
set_property -dict {PACKAGE_PIN A9 IOSTANDARD LVCMOS15} [get_ports led_g_n]
set_property -dict {PACKAGE_PIN B9 IOSTANDARD LVCMOS15} [get_ports led_b_n]

# User button
set_property -dict {PACKAGE_PIN A10 IOSTANDARD LVCMOS15} [get_ports user_btn]

# octo_spi 1
set_property -dict {PACKAGE_PIN E12 IOSTANDARD LVCMOS15} [get_ports ck_sck]     
set_property -dict {PACKAGE_PIN E11 IOSTANDARD LVCMOS15} [get_ports ck_ss]
set_property -dict {PACKAGE_PIN D13 IOSTANDARD LVCMOS15} [get_ports octo_spi[0]]
set_property -dict {PACKAGE_PIN C13 IOSTANDARD LVCMOS15} [get_ports octo_spi[1]]
set_property -dict {PACKAGE_PIN C11 IOSTANDARD LVCMOS15} [get_ports octo_spi[2]]
set_property -dict {PACKAGE_PIN C12 IOSTANDARD LVCMOS15} [get_ports octo_spi[3]]
set_property -dict {PACKAGE_PIN H14 IOSTANDARD LVCMOS15} [get_ports octo_spi[4]]
set_property -dict {PACKAGE_PIN G15 IOSTANDARD LVCMOS15} [get_ports octo_spi[5]]
set_property -dict {PACKAGE_PIN J15 IOSTANDARD LVCMOS15} [get_ports octo_spi[6]]
set_property -dict {PACKAGE_PIN J16 IOSTANDARD LVCMOS15} [get_ports octo_spi[7]]
set_property -dict {PACKAGE_PIN F14 IOSTANDARD LVCMOS15} [get_ports ck_rst]
set_property -dict {PACKAGE_PIN G14 IOSTANDARD LVCMOS15} [get_ports sync]
set_property -dict {PACKAGE_PIN H16 IOSTANDARD LVCMOS15} [get_ports ready]
set_property -dict {PACKAGE_PIN G16 IOSTANDARD LVCMOS15} [get_ports acknack]

# Fallback SPI
# set_property -dict {PACKAGE_PIN D16 IOSTANDARD LVCMOS15} [get_ports spi_mosi]
# set_property -dict {PACKAGE_PIN F15 IOSTANDARD LVCMOS15} [get_ports spi_miso]
# set_property -dict {PACKAGE_PIN E13 IOSTANDARD LVCMOS15} [get_ports spi_sck]
# set_property -dict {PACKAGE_PIN D11 IOSTANDARD LVCMOS15} [get_ports spi_cs]

# Extra unallocated IO
# set_property -dict {PACKAGE_PIN h14 IOSTANDARD LVCMOS15} [get_ports unallocated_io]
# set_property -dict {PACKAGE_PIN H13 IOSTANDARD LVCMOS15} [get_ports unallocated_io]
# set_property -dict {PACKAGE_PIN H12 IOSTANDARD LVCMOS15} [get_ports unallocated_io]
# set_property -dict {PACKAGE_PIN G12 IOSTANDARD LVCMOS15} [get_ports unallocated_io]
# set_property -dict {PACKAGE_PIN H11 IOSTANDARD LVCMOS15} [get_ports unallocated_io]

# Memory
set_property -dict {PACKAGE_PIN M6  IOSTANDARD LVCMOS33} [get_ports mem_data_bus[0]]
set_property -dict {PACKAGE_PIN N6  IOSTANDARD LVCMOS33} [get_ports mem_data_bus[1]]
set_property -dict {PACKAGE_PIN P8  IOSTANDARD LVCMOS33} [get_ports mem_data_bus[2]]
set_property -dict {PACKAGE_PIN R8  IOSTANDARD LVCMOS33} [get_ports mem_data_bus[3]]
set_property -dict {PACKAGE_PIN T7  IOSTANDARD LVCMOS33} [get_ports mem_ck]
set_property -dict {PACKAGE_PIN T8  IOSTANDARD LVCMOS33} [get_ports mem_cs]
set_property -dict {PACKAGE_PIN T9  IOSTANDARD LVCMOS33} [get_ports mem_data_bus[4]]
set_property -dict {PACKAGE_PIN T10 IOSTANDARD LVCMOS33} [get_ports mem_data_bus[5]]
set_property -dict {PACKAGE_PIN R5  IOSTANDARD LVCMOS33} [get_ports mem_data_bus[6]]
set_property -dict {PACKAGE_PIN T5  IOSTANDARD LVCMOS33} [get_ports mem_data_bus[7]]
set_property -dict {PACKAGE_PIN R6  IOSTANDARD LVCMOS33} [get_ports mem_rwds]
set_property -dict {PACKAGE_PIN R7  IOSTANDARD LVCMOS33} [get_ports mem_reset_n]

# VGA
set_property -dict {PACKAGE_PIN P5 IOSTANDARD LVCMOS33} [get_ports vga_r[0]]
set_property -dict {PACKAGE_PIN T3 IOSTANDARD LVCMOS33} [get_ports vga_r[1]]
set_property -dict {PACKAGE_PIN T4 IOSTANDARD LVCMOS33} [get_ports vga_r[2]]
set_property -dict {PACKAGE_PIN T2 IOSTANDARD LVCMOS33} [get_ports vga_r[3]]
set_property -dict {PACKAGE_PIN R3 IOSTANDARD LVCMOS33} [get_ports vga_r[4]]

set_property -dict {PACKAGE_PIN R1 IOSTANDARD LVCMOS33} [get_ports vga_g[0]]
set_property -dict {PACKAGE_PIN R2 IOSTANDARD LVCMOS33} [get_ports vga_g[1]]
set_property -dict {PACKAGE_PIN N4 IOSTANDARD LVCMOS33} [get_ports vga_g[2]]
set_property -dict {PACKAGE_PIN M5 IOSTANDARD LVCMOS33} [get_ports vga_g[3]]
set_property -dict {PACKAGE_PIN P3 IOSTANDARD LVCMOS33} [get_ports vga_g[4]]
set_property -dict {PACKAGE_PIN P4 IOSTANDARD LVCMOS33} [get_ports vga_g[5]]

set_property -dict {PACKAGE_PIN P1 IOSTANDARD LVCMOS33} [get_ports vga_b[0]]
set_property -dict {PACKAGE_PIN N1 IOSTANDARD LVCMOS33} [get_ports vga_b[1]]
set_property -dict {PACKAGE_PIN N2 IOSTANDARD LVCMOS33} [get_ports vga_b[2]]
set_property -dict {PACKAGE_PIN N3 IOSTANDARD LVCMOS33} [get_ports vga_b[3]]
set_property -dict {PACKAGE_PIN M1 IOSTANDARD LVCMOS33} [get_ports vga_b[4]]

set_property -dict {PACKAGE_PIN M2  IOSTANDARD LVCMOS33} [get_ports vga_hsync]
set_property -dict {PACKAGE_PIN M4  IOSTANDARD LVCMOS33} [get_ports vga_vsync]

# Occilator
## Clock signal
set_property -dict {PACKAGE_PIN N11 IOSTANDARD LVCMOS33} [get_ports clk_in]
create_clock -period 50.000 [get_ports clk_in]

# MCU clock
create_clock -period 40.000 -name ck_sck [get_ports ck_sck]
set_clock_groups -asynchronous \
    -group [get_clocks -of_objects [get_ports clk_in]] \
    -group [get_clocks ck_sck]
