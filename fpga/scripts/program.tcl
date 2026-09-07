# Get the directory containing this script
set script_dir [file dirname [file normalize [info script]]]

# Project root:
# fpga/scripts/program.tcl
#       ↑
#     fpga/
set project_root [file normalize [file join $script_dir ../]]

set bitstream [file join $project_root build main.bit]

puts "Project root: $project_root"
puts "Bitstream:    $bitstream"

if {![file exists $bitstream]} {
    puts "ERROR: Bitstream not found:"
    puts "       $bitstream"
    exit 1
}

puts "Opening hardware manager..."

open_hw_manager
connect_hw_server
open_hw_target

set device [lindex [get_hw_devices] 0]

puts "Programming device: $device"

set_property PROGRAM.FILE $bitstream $device

program_hw_devices $device

refresh_hw_device $device

puts "========================================"
puts "PROGRAMMING COMPLETE"
puts "========================================"

close_hw_target
disconnect_hw_server
close_hw_manager