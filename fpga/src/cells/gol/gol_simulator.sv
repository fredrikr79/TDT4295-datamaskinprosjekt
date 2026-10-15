// Implementation taken and modified from: https://github.com/hrvach/Life_MiSTer/blob/master/Life.sv
module gol_simulator (
    input  wire clk,
    input  wire reset,
    input  wire en,
    output wire alive
);
  import gol_pkg::*;

  // The 3x3 window
  // r1p1  r1p2  pixel_out_row1     <- row above
  // r2p1  r2p2  pixel_out_row2     <- r2p2 is the cell under evaluation
  // r3p1  r3p2  pixel_out_fifo     <- row below
  logic output_pixel = 1'b0;
  logic r1p1 = 1'b0, r1p2 = 1'b0;
  logic r2p1 = 1'b0, r2p2 = 1'b0;
  logic r3p1 = 1'b0, r3p2 = 1'b0;

  logic pixel_out_row1, pixel_out_row2, pixel_out_fifo;

  logic [31:0] lfsr = 32'hACE1_2345; // Linear feedback shift register acting as pseudo-randomizer.
  always_ff @(posedge clk) begin
    if (en) lfsr <= {lfsr[30:0], lfsr[31] ^ lfsr[21] ^ lfsr[1] ^ lfsr[0]};
  end
  
  wire seed_bit = lfsr[31] & lfsr[17];

  // Intial state of grid, fills grid with random values
  logic seeding = 1'b1;
  logic [SeedCountBits-1:0] seed_count = '0;
  always_ff @(posedge clk) begin
    if (reset) begin
      seeding <= 1'b1;
      seed_count <= '0;
    end 
    else if (en && seeding) begin
      seed_count <= seed_count + 1'b1;
      if (seed_count == SeedCountBits'(FrameLen - 1)) seeding <= 1'b0;
    end
  end

  gol_ring_buffer #(
      .Depth(RingDepth)
  ) fb_shift_reg (
      .clk (clk),
      .en  (en),
      .data_in(seeding ? seed_bit : output_pixel),
      .data_out(pixel_out_fifo)
  );

  gol_row_delay #(
      .Depth(RowDelay)
  ) row1 (
      .clk (clk),
      .en  (en),
      .data_in(r2p1),
      .data_out(pixel_out_row1)
  );

  gol_row_delay #(
      .Depth(RowDelay)
  ) row2 (
      .clk (clk),
      .en  (en),
      .data_in(r3p1),
      .data_out(pixel_out_row2)
  );

  
  logic [3:0] neighbour_count;
  always_comb begin
    neighbour_count = 4'(r1p1) + 4'(r1p2) + 4'(pixel_out_row1)
                    + 4'(r2p1)            + 4'(pixel_out_row2)
                    + 4'(r3p1) + 4'(r3p2) + 4'(pixel_out_fifo);
  end

  always_ff @(posedge clk) begin
    if (en) begin

      // r1p1 <-r1p2 <-pixel_out_row1
      // r2p1 <-r2p2 <-pixel_out_row2
      // r3p1 <-r3p2 <-pixel_out_fifo
      r1p1 <= r1p2;
      r1p2 <= pixel_out_row1;
      r2p1 <= r2p2;
      r2p2 <= pixel_out_row2;
      r3p1 <= r3p2;
      r3p2 <= pixel_out_fifo;

      // OR-ing the centre into bit 0 makes "2 neighbours and alive"
      // compare equal to 3 as well.
      output_pixel <= (neighbour_count | 4'(r2p2)) == 4'd3;
    end
  end

  assign alive = pixel_out_fifo;
endmodule
