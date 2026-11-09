module example_blinking (
    input wire       clk,
    output logic     led_b_n = 1'b1,
    output logic     led_g_n = 1'b1,
    output logic     led_r_n = 1'b1
);



logic [24:0] count;
mod_n_counter #(
    .N(20000000)
) counter
(
    .clk(clk),
    .reset(1'b0),
    .enable(1'b1),
    .count(count)
);

always_ff @(posedge clk) begin
    if (count == 25'd19999999/3) begin
        // led_g <= ~led_g;
        led_g_n <= ~led_g_n;
    end
    // end else if (count == 25'd19999999*2/3) begin
    //     led_b <= 1'b0;
    //     led_g <= ~led_g;
    // end else if (count == 25'd19999999) begin
    //     led_g <= 1'b0;
    //     led_r <= ~led_r;
    // end
end
endmodule
