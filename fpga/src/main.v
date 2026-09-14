module main #(
    parameter DEBUG = 1'd1
)(
    input  wire       ck_ss,
    input  wire       ck_sck,
    input  wire       ck_rst,

    // DEBUG
    input wire [3:0] sw,
    input wire [3:0] btn,

    // Octal SPI data bus
    inout  wire [7:0]  octo_spi,

    output wire [3:0] led,
    output wire       led0_r,
    output wire       led0_g,
    output wire       led0_b,
    output wire       ready,
    output wire       bf
);

    // ------------------------------------------------------------
    // CMD parts
    // ------------------------------------------------------------
    wire [3:0] opcode;
    wire [3:0] options;

    // ------------------------------------------------------------
    // Buffers
    // ------------------------------------------------------------
    reg [7:0] in_data;
    reg [7:0] response = 8'd0;
    reg [255:0] data_buff = 256'd0;


    // ------------------------------------------------------------
    // Flags
    // ------------------------------------------------------------
    reg full = 1'b0;
    reg rx_done = 1'b0;
    reg tx_done = 1'b0;

    // ------------------------------------------------------------
    // Octal SPI output control
    // ------------------------------------------------------------
    reg [7:0] io_out;
    reg       io_oe;

    assign octo_spi = io_oe ? io_out : 8'hFF;

    // ------------------------------------------------------------
    // State declarations
    // ------------------------------------------------------------
    reg [6:0] state;


    localparam [6:0]
        ST_READY   = 7'b0000000,
        ST_DECODE  = 7'b0000010,
        ST_RX_DATA = 7'b0000011,
        ST_TX      = 7'b0000100,
        ST_FAILED  = 7'b1111111;

    initial state = ST_READY;

    // ------------------------------------------------------------
    // Outputs
    // ------------------------------------------------------------
    assign ready  = (state == ST_READY || state == ST_TX);
    assign bf     = full;
    assign opcode = DEBUG ? sw : in_data[7:4];
    assign options = in_data[3:0];
    assign led = response[7:4];
    assign led0_r = state[2];
    assign led0_g = state[1];
    assign led0_b = state[0];

    // State machine
    always @(posedge ck_sck) begin

        if (ck_rst == 1'b0) begin
            state <= ST_READY;
            in_data <= 8'b0;
            response <= 8'b0;
            data_buff <= 256'b0;
            io_oe <= 1'b0;
            io_out <= 8'b0;
        end
        
        if (DEBUG && btn[0]) begin

            // Manual state advance
            case (state)

                ST_READY: begin
                    state <= ST_DECODE;
                end

                ST_DECODE: begin
                    state <= ST_RX_DATA;
                end

                ST_RX_DATA: begin
                    state <= ST_TX;
                end

                ST_TX: begin
                    state <= ST_READY;
                end

                default: begin
                    state <= ST_READY;
                end

            endcase
        end else begin
            
            
            case (state)
            
            ST_READY: begin
                rx_done <= 1'b0;
                tx_done <= 1'b0;
                
                if (!ck_ss) begin
                    in_data <= octo_spi;
                    state <= ST_DECODE;
                end
            end

            // Command dispatch
            ST_DECODE: begin
                
                case (opcode)
                
                    4'b0011: begin
                        rx_done <= 1'b0;
                        state   <= ST_RX_DATA;
                    end
                
                    4'h01: begin
                        response <= 8'b10101010;
                        tx_done  <= 1'b0;
                        state    <= ST_TX;
                    end

                    default: begin
                        response <= 8'b11111111;
                        tx_done  <= 1'b0;
                        state    <= ST_TX;
                    end
                    
                endcase
            end
            
            // Receive data.
            ST_RX_DATA: begin
                
                data_buff <= {data_buff[247:0], octo_spi};
                
                // Check received byte for FF terminator.
                if (octo_spi == 8'hFF) begin
                    rx_done <= 1'b1;
                    state   <= ST_TX;
                    tx_done <= 1'b0;
                end
                else begin
                    rx_done <= 1'b0;
                end
                
            end
            
            // Transmit response.
            ST_TX: begin
                if (tx_done) begin
                    state <= ST_READY;
                end
            end
            
            ST_FAILED: begin
                $display(
                    "ERROR: A fatal error occurred, could not recover"
                    );
                    $finish(1);
                end
                
                default: begin
                    state <= ST_FAILED;
                end
                
            endcase
        end
    end
        
    // Octal SPI transmit
    always @(negedge ck_sck) begin

        if (!ck_rst) begin
            io_oe <= 1'b0;
            io_out <= 8'b0;
        end

        else if (state == ST_TX && !ck_ss) begin

            io_oe  <= 1'b1;
            io_out <= response;

        end
        else begin
            io_oe <= 1'b0;
        end

    end

endmodule
