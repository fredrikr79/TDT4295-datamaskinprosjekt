module fb_line_buffer #(
    parameter int unsigned LineBytes = mem_map_pkg::ScreenWidth  // 640
) (
    input logic clk,
    input logic reset,

    input logic fill_we,
    input logic [7:0] fill_data,
    input logic swap_buffer,

    input logic [$clog2(LineBytes)-1:0] read_x,
    input logic read_en,  // High while the beam is actually fetching pixels

    output logic [7:0] read_data,
    output logic underrun  // Used by testbench to measure if fetching lines is behind schedule.
);

  localparam int unsigned AddrBits = $clog2(LineBytes);  // 10
  localparam int unsigned MemDepth = 2 * LineBytes;  // 1280
  localparam int unsigned MemAddrBits = $clog2(MemDepth);  // 11

  // Both halves live in one array so this infers as a single block RAM.
  logic [7:0] line_store[MemDepth];

  function automatic logic [MemAddrBits-1:0] byte_index(input logic half,
                                                        input logic [AddrBits-1:0] offset);
    return MemAddrBits'((half ? LineBytes : 0) + 32'(offset));
  endfunction


  logic display_half;

  logic [AddrBits:0] fetched_valid_bytes;
  // Register used for underrun testbench validation
  logic [AddrBits:0] display_valid_bytes;

  logic [AddrBits-1:0] fetch_offset;
  assign fetch_offset = (32'(fetched_valid_bytes) >= LineBytes)
                      ? AddrBits'(LineBytes - 1)
                      : fetched_valid_bytes[AddrBits-1:0];

  always_ff @(posedge clk) begin
    if (reset) begin
      display_half <= 1'b0;
      fetched_valid_bytes <= '0;
      display_valid_bytes <= '0;
    end else if (swap_buffer) begin
      display_half <= ~display_half;
      display_valid_bytes <= fetched_valid_bytes;
      fetched_valid_bytes <= '0;
    end else if (fill_we) begin
      if (32'(fetched_valid_bytes) < LineBytes) fetched_valid_bytes <= fetched_valid_bytes + 1'b1;
    end
  end

  always_ff @(posedge clk) begin
    if (fill_we) line_store[byte_index(~display_half, fetch_offset)] <= fill_data;
  end

  always_ff @(posedge clk) begin
    read_data <= line_store[byte_index(display_half, read_x)];
  end

  // Testbench test
  assign underrun = read_en && ({1'b0, read_x} >= display_valid_bytes);
endmodule
