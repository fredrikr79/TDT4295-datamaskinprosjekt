module decoding_circuit (
    input vga_sync_params::x_coordinate_t horizontal_count,
    input vga_sync_params::y_coordinate_t vertical_count,
    output logic h_sync,
    output logic v_sync,
    output logic active_area
);
  import vga_sync_params::*;
  import config_pkg::*;
  assign active_area = 32'(horizontal_count) < ActiveWidth
      && 32'(vertical_count) < ActiveHeight;
  assign h_sync = !(
      ActiveWidth + RightPorch <= 32'(horizontal_count)
      && 32'(horizontal_count) < TotalWidth - LeftPorch
  );
  assign v_sync =  !(
      ActiveHeight + BottomPorch <= 32'(vertical_count)
      && 32'(vertical_count) < TotalHeight - TopPorch
  );

endmodule
