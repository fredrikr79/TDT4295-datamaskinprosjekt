module main(
    input wire ck_ss,
    input wire ck_sck,
    input wire ck_mosi,
    output wire ck_miso,
    output wire ready, // TODO: Implement ready signal
    output wire full_indicator // TODO: Implement full indicator signal
);

    wire [3:0] opcode;
    wire [3:0] options;

    reg [7:0] in_data;
    reg [2:0] rx_pos;
    reg [2:0] tx_pos;
    reg [7:0] response;

    reg transmitting;    
    reg miso_bit;

    assign opcode = in_data[7:4];
    assign options = in_data[3:0];
    assign ck_miso = miso_bit;

    // Recieve data on MOSI 

    always @(posedge ck_sck) begin
        if (ck_ss) begin

            rx_pos <= 3'b0;
            in_data <= 8'b00000000;
            transmitting <= 1'b0;

        end 
        
        else if (!transmitting) begin

            in_data[7 - rx_pos] <= ck_mosi;

            if (rx_pos == 3'b111) begin
                rx_pos <= 3'd0;
                transmitting <= 1'b1;

            end 

            else begin
                rx_pos <= rx_pos + 3'd1;
            end
        end
    end

    
    
    // Decode opcode

    always @(*) begin
        case (opcode)
            4'b0000: 
                response = 8'h00; // TODO: Dummy response
            default:
                response = 8'hFF; // Default response for unknown opcodes
        endcase
    end


    // Send data on MISO
    always @(negedge ck_sck) begin
        if( ck_ss ) begin

            tx_pos <= 3'd0;
            miso_bit <= 1'b0;   
        end

        else if (transmitting) begin
            miso_bit <= response[7 - tx_pos];
            
            if (tx_pos == 3'b111) begin
                tx_pos <= 3'd0;
            end 
            
            else begin
                tx_pos <= tx_pos + 3'd1;
            end
        end
    end


endmodule
