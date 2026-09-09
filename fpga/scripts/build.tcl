# Get the directory containing this script
set script_dir [file dirname [file normalize [info script]]]

# Project root is two directories above this script:
# fpga/scripts/build.tcl
#       ↑
#     fpga/
#       ↑
# project/
set project_root [file normalize [file join $script_dir ../]]

set src_dir         [file join $project_root src]
set constraints_dir [file join $project_root constraints]
set build_dir       [file join $project_root build]
set vivado_dir      [file join $build_dir vivado]

puts "Project root: $project_root"
puts "Source dir:   $src_dir"
puts "Build dir:    $build_dir"

create_project bloata $vivado_dir \
    -part xc7a35tcsg324-1 \
    -force

# Add Verilog files
proc find_verilog {dir} {
    set found [glob -nocomplain [file join $dir *.v]]
    foreach sub [glob -nocomplain -type d [file join $dir *]] {
        set found [concat $found [find_verilog $sub]]
    }
    return $found
}

set verilog_files [find_verilog $src_dir]
# Done adding verilog files


if {[llength $verilog_files] == 0} {
    puts "ERROR: No Verilog files found in $src_dir"
    exit 1
}

add_files $verilog_files

# Add constraints
set xdc_file [file join $constraints_dir arty_a7_35T.xdc]

if {[file exists $xdc_file]} {
    add_files -fileset constrs_1 $xdc_file
} else {
    puts "WARNING: No XDC file found at $xdc_file"
}

set_property top main [current_fileset]

# Synthesis
launch_runs synth_1 -jobs 4
wait_on_run synth_1

# Implementation + bitstream
launch_runs impl_1 -to_step write_bitstream -jobs 4
wait_on_run impl_1

file copy -force \
    [file join $vivado_dir bloata.runs impl_1 main.bit] \
    [file join $build_dir main.bit]

puts "========================================"
puts "BUILD COMPLETE"
puts "========================================"