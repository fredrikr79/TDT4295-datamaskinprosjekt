module main(
    input wire clk,
    input wire [3:0] sw,
    output reg [3:0] led
);

    reg [26:0] counter;

    always @(posedge clk) begin
       counter <=  counter + sw;
    
        if (counter >= 26'd49_999_999) begin
             counter <= 0;
             led <= led + 1'b1;
        end
    end
endmodule