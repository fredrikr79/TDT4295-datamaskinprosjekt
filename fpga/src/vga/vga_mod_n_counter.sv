module vga_mod_n_counter #(
    parameter int unsigned N = 10,
    parameter bit AT_POSEDGE = 1,
    parameter bit AT_NEGEDGE = 0,
    localparam int unsigned WIDTH = (N > 1) ? $clog2(N) : 1,
    parameter bit [WIDTH-1:0] START_VALUE = 0
) (
    input  logic                      clk,
    input  logic                      reset,
    input  logic                      enable,
    output logic unsigned [WIDTH-1:0] count
);
  function automatic void increment();
    if (reset) begin
      count <= START_VALUE;
    end else if (enable) begin
      if (count == WIDTH'(N - 1)) count <= '0;
      else count <= count + 1'b1;
    end
  endfunction

  generate
    if (AT_POSEDGE && !AT_NEGEDGE) begin : gen_posedge

      always_ff @(posedge clk) begin
        increment();
      end

    end else if (!AT_POSEDGE && AT_NEGEDGE) begin : gen_negedge

      always_ff @(negedge clk) begin
        increment();
      end

    end else if (AT_POSEDGE && AT_NEGEDGE) begin : gen_both

      always @(posedge clk or negedge clk) begin
        increment();
      end

    end else begin : gen_none
      initial begin
        count <= '0;
      end
    end
  endgenerate

endmodule
