module main(
    input wire ck_ss,
    input wire ck_sck,
    input wire ck_mosi,
    output wire ck_miso,
    output wire ready,
    output wire bf // TODO: Implement full indicator signal
);

    wire [3:0] opcode;
    wire [3:0] options;

    reg [7:0] in_data;
    reg [255:0] data_buff = 256'd0;

    reg [2:0] rx_pos = 3'd0;
    reg [2:0] tx_pos = 3'd7;
    reg [7:0] response;

    reg full = 1'b0;
    reg rx_done;
    reg tx_done;    
    reg miso_bit;

    reg [6:0] state;
    localparam [6:0]
        ST_READY   = 7'b0000000,
        ST_RX_CMD  = 7'b0000001,
        ST_DECODE  = 7'b0000010,
        ST_RX_DATA = 7'b0000011,
        ST_TX      = 7'b0000100,
        ST_FAILED  = 7'b1111111;


    initial state = ST_READY;
    assign ready = (state == ST_READY);
    assign bf = full;
    assign opcode = in_data[7:4];
    assign options = in_data[3:0];
    assign ck_miso = miso_bit;


    // Recieve data on MOSI 
    always @(posedge ck_sck) begin

        case (state)
            ST_READY: begin
                if (!ck_ss) begin
                    state <= ST_RX_CMD;
                end
            end
            
            ST_RX_CMD: begin
                 if (rx_pos == 3'd7) begin
                    state <= ST_DECODE;
                end
            end

            ST_DECODE: begin
                case (opcode)
                    4'd0: begin // 
                        state <= ST_RX_DATA;
                    end

                    4'd1: begin
                        response <= 8'b00000000;
                        state <= ST_TX;
                    end
                    default: begin
                        response <= 8'b11111111;
                        state <= ST_TX;
                    end
                endcase
            end

            ST_RX_DATA: begin
                if(rx_done) begin
                    state <= ST_TX;
                end
            end
            
            ST_TX: begin
                if (tx_done) begin
                    state <= ST_READY;
                end
            end

            default: begin
                // do nothing
            end

            ST_FAILED: begin
                $display("ERROR: A fatal error occured, could not recover");
                $finish(1);
            end
        endcase
    end

    // Recieve data on MOSI
    always @(posedge ck_sck) begin

        if (state == ST_RX_CMD) begin
            in_data <= {in_data[6:0], ck_mosi};
            if (rx_pos == 3'd7)
                rx_pos <= 3'd0;
            else
                rx_pos <= rx_pos + 1'b1;
        end 
        
        else if (state == ST_RX_DATA) begin
            data_buff <= {data_buff[254:0], ck_mosi};
            if (rx_pos == 3'd7)
                rx_pos <= 3'd0;
            else
                rx_pos <= rx_pos + 1'b1;

            if (rx_pos == 3'd7 &&  {data_buff[6:0], ck_mosi} == 8'hFF) begin
                rx_done <= 1'b1;
            end
            else begin
                rx_done <= 1'b0;
            end
        end
        
    end
    

    // Send data on MISO
    always @(negedge ck_sck) begin
        if (state == ST_TX) begin
            miso_bit <= response[tx_pos];
            
            if (tx_pos == 3'd0) begin
                tx_done <= 1'b1;
            end else begin
                tx_pos <= tx_pos - 1;
                tx_done <= 1'b0;
            end
        end
    end
    
    
endmodule
