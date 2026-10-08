// Brain storming on how to implement velocity.

// package particle_pkg;
//   localparam int INT_BITS = 10;
//   localparam int FRAC_BITS = 8;                             // What part of position are fractional, 11 integer bits, 8 fractional bits = 1/256
//   typedef logic signed [INT_BITS + FRAC_BITS:0] fixed_t;    // 11 int + 8 frac

//   typedef struct packed {
//     fixed_t vel_x, vel_y;
//   } particle_vel_t;

//   function automatic fixed_t to_fixed(real v);
//     return fixed_t'(int'(v * (1 << FRAC_BITS)));            // Shifts float to be int
//   endfunction

//   localparam fixed_t GRAVITY = to_fixed(0.25);              // Stored as 64; 64/256 = 0.25 px/frame^2
// endpackage


// ALt 2
// Have different levels of velocity. Allocate 8 bits for 256 levels of velocity? We definetly don't have space in our BRAM blocks for that looking at the cell_pkg implementation.
