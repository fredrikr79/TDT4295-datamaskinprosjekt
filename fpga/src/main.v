    module main #(
        parameter DEBUG = 1'd1
    )(
        input  wire       clk,        // Onboard 100MHz Clock (Pin E3) for the ILA Hub
        input  wire       ck_ss,
        input  wire       ck_sck,
        input  wire       ck_rst,

        // Octal SPI data bus
        inout  wire [7:0] octo_spi,
        inout  wire       acknack,

        output wire [3:0] led,
        output wire       led0_r,
        output wire       led0_g,
        output wire       led0_b,
        output wire       ready,
        
        output wire       out_almost_empty,
        output wire       out_empty,
        output wire       in_almost_full,
        output wire       in_full
    );


    // FSM STATES
    localparam [7:0]
        ST_BOOT      =  8'b00000100,
        ST_READY     =  8'b00001010,
        ST_RX        =  8'b00010111,   
        ST_DECODE    =  8'b00011011,
        ST_BUSY      =  8'b00110101,
        ST_TX_IDLE   =  8'b00111001,   
        ST_TX_SEND   =  8'b01010001,   
        ST_MEMORY    =  8'b01011001,
        ST_FAILED    =  8'b11111111;

    // KNOWN COMMANDS
    localparam [3:0]
        ECHO = 4'hE;

    // STATE / CMD TRACKERS
    reg [7:0] state = ST_BOOT;
    reg [3:0] opcode = 4'hz; 

    // RESET
    reg rst_en = 1'b0; // programatic reset
    wire reset = ~ck_rst || rst_en;
    wire reset_s = ~ck_rst_s || rst_en;

    // READY
    assign ready =
        (state == ST_READY) ||
        (state == ST_TX_IDLE);

    // OCTO_SPI
    assign octo_spi = (~ck_ss && tx_armed_s) ? tx_out : 8'bz;

    // CDC synchronizations CK_SCK -> CLK
    reg [2:0] ck_ss_sync = 3'b000;   
    reg [2:0] ck_rst_sync = 3'b000;
    reg [2:0] out_empty_sync = 3'b000;
    
    wire ck_ss_s = ck_ss_sync[2];
    wire ck_rst_s = ck_rst_sync[2];
    wire out_empty_s = out_empty_sync[2];

    always @(posedge clk) begin
        if(reset) begin
            ck_ss_sync <= 3'b111;
            ck_rst_sync <= 3'b000;
            out_empty_sync <= 3'b111;
        end
        else begin
            ck_ss_sync <= {ck_ss_sync[1:0], ck_ss};
            ck_rst_sync <= {ck_rst_sync[1:0], ck_rst}; 
            out_empty_sync <= {out_empty_sync[1:0], out_empty}; 
        end
    end

    // RX signals
    wire in_w_en;
    wire in_almost_empty;
    wire in_empty;
    wire [7:0] in_fifo;
    wire in_wr_rst_busy;
    wire in_rd_rst_busy;
    wire in_wr_ack;

    reg in_r_en = 1'b0;

    // Event driven RX Capture
    wire rx_wr_gate = (~ck_ss) && ((state == ST_READY) | (state == ST_RX) | (state == ST_TX_IDLE));
    wire rx_event_trigger = rx_event_sync[2] ^ rx_event_sync[1];
    
    reg [7:0] rx_capture = 8'h00;
    reg       rx_event = 1'b0;
    reg [2:0] rx_event_sync = 3'b000;
    
    always @(posedge clk) begin
        if(reset_s)
            rx_event_sync <= 3'b000;
        else
            rx_event_sync <= {rx_event_sync[1:0], rx_event};
    end


    fifo_generator_1 in_queue(
        .din(rx_capture),
        .dout(in_fifo),
        .almost_full(in_almost_full),
        .full(in_full),
        .almost_empty(in_almost_empty),
        .empty(in_empty),
        .wr_en(in_w_en),
        .rd_en(in_r_en),
        .wr_ack(in_wr_ack),
        .rst(reset_s),
        .wr_clk(clk),
        .rd_clk(clk),
        .wr_rst_busy(in_wr_rst_busy),
        .rd_rst_busy(in_rd_rst_busy)
    );

    // CDC synchronizations CLK -> CK_SCK
    always @(posedge ck_sck) begin
        if (reset_s) begin
            rx_event      <= 1'b0;
            tx_event      <= 1'b0;
            rx_capture <= 8'h00;
        end else begin
            
            if (rx_wr_gate) begin
                rx_capture <= octo_spi;
                rx_event <= ~rx_event;
            end
            
            if (tx_rd_gate) begin
                tx_event <= ~tx_event;
            end
            
        end
    end

    // ACK logic
    reg [2:0] ack_sync = 3'b000;
    always @(posedge clk) begin
        if (reset) begin
            ack_sync      <= 3'b000;
        end
        ack_sync <= {ack_sync[1:0], in_wr_ack};
    end

    assign acknack = ack_sync[2];

    // OUTPUT QUEUE
    wire out_r_en;
    wire out_almost_full;
    wire out_full;
    wire [7:0] out_data;
    wire out_wr_rst_busy;
    wire out_rd_rst_busy;
    
    reg out_w_en;
    reg [7:0] out_fifo = 8'h00;
    reg tx_armed = 1'b0;

    fifo_generator_2 out_queue(
        .din(out_fifo),
        .dout(out_data),
        .almost_full(out_almost_full),
        .full(out_full),
        .almost_empty(out_almost_empty),
        .empty(out_empty),
        .wr_en(out_w_en),
        .rd_en(out_r_en),
        .rst(reset_s),
        .wr_clk(clk),
        .rd_clk(clk),
        .wr_rst_busy(out_wr_rst_busy),
        .rd_rst_busy(out_rd_rst_busy)
    );

    // TX arming
    wire tx_armed_s = tx_armed_sync[2];
    
    // Syncronization for TX stage 
    reg [2:0] tx_armed_sync = 3'b000;
    always @(posedge clk) begin
        if (reset) begin
            tx_armed_sync <= 3'b000;
        end
        tx_armed_sync <= {tx_armed_sync[1:0], tx_armed};
    end
    
    // Synchronization for event driven TX (event = ck_SCK pulse)
    wire tx_event_trigger = tx_event_sync[2] ^ tx_event_sync[1];
    wire tx_stage_allowed = (state == ST_TX_SEND);
    wire tx_rd_gate = (~ck_ss) && tx_stage_allowed;

    reg       tx_event = 1'b0;
    reg [2:0] tx_event_sync = 3'b000;

    always @(posedge clk) begin
        if(reset_s)
            tx_event_sync <= 3'b000;
        else
            tx_event_sync <= {tx_event_sync[1:0], tx_event};
    end

    // initialize outgoing event driven buffer, and set conditions for preloading during TX handshake
    reg [7:0] tx_out = 8'h00;
    reg tx_first_loaded = 1'b0;
    wire tx_load = (state == ST_TX_SEND) && !out_empty_s && in_empty && !tx_first_loaded && !out_empty;

    always @(posedge clk) begin
        if(reset_s) begin
            tx_out   <= 8'h00;
            tx_first_loaded <= 1'b0;
            tx_armed <= 1'b0;
        end
        if(tx_load) begin
            tx_out <= out_data;
            tx_first_loaded <= 1'b1;
        end    
        else if(tx_event_trigger) begin
            tx_out <= out_data;
        end
        if (state == (ST_TX_SEND) && !ck_ss_s && !out_empty_s && in_empty)
            tx_armed <= 1'b1;
        else if (state == (ST_TX_SEND) && ck_ss_s && in_empty) begin
            tx_armed  <= 1'b0;
            tx_first_loaded <= 1'b0;
        end
    end

    // MCU WRITE CONDITIONS
    assign in_w_en = rx_event_trigger && ~in_full;

    // MCU READ CONDITIONS
    assign out_r_en = (tx_load || tx_event_trigger) && ~out_empty;

    //State machine
    always @(posedge clk) begin
        
        if (reset_s) begin
           // FSM
            state          <= ST_BOOT;
            opcode         <= 4'h0;

            // Input FIFO control
            in_r_en        <= 1'b0;

            // Output FIFO control
            out_w_en       <= 1'b0;
            out_fifo       <= 8'h00;

        end

        else begin

            in_r_en <= 1'b0;
            out_w_en <= 1'b0;

            case (state)

                ST_BOOT: begin
                    if (~in_full && ~out_full && ~in_wr_rst_busy && ~in_rd_rst_busy && ~out_rd_rst_busy && ~out_wr_rst_busy) begin
                        state <= ST_READY;
                        opcode <= 4'h0;
                        in_r_en <= 1'b0;
                        out_w_en <= 1'b0; 
                    end
                end
                
                ST_READY: begin
                    if (~ck_ss_s) begin
                        state <= ST_RX;
                    end
                end

                ST_RX: begin
                    if (ck_ss_s && !in_empty) begin
                        in_r_en <= 1'b1; // CONSUME CMD (AGAIN)
                        state <= ST_DECODE;
                    end
                end
        
                ST_DECODE: begin
                    // USING FWFT FIFO's allowing us to peek at the read bus without popping.
                    // Each dispatched command decides for itself if it should pop the command
                    // By setting in_r_en <= 1'b1;
                    opcode <= in_fifo[7:4];

                    case (in_fifo[7:4])
                    
                        ECHO: begin
                            state <= ST_BUSY;
                        end
                        
                        default : begin
                            state <= ST_FAILED;
                        end
                                
                    endcase
                end

                ST_BUSY: begin
                    case (opcode)
                        ECHO: begin
                            if(!out_full && !in_empty) begin
                                out_fifo <= in_fifo;
                                out_w_en <= 1'b1;
                                in_r_en <= 1'b1;
                            end
                            else if (in_empty) begin
                                state <= ST_TX_IDLE;
                            end 
                        end
                        default: begin
                            state <= ST_FAILED;
                        end
                    endcase
                end
                
                ST_TX_IDLE: begin // Wait for TX stage payload from MCU
                    if(~ck_ss_s && ~in_empty) begin
                        in_r_en <= 1'b1; // TX byte consumed
                        state <= ST_TX_SEND;     
                    end
                end

                ST_TX_SEND: begin
                    if (ck_ss_s) begin
                        if (!in_empty)
                            in_r_en <= 1'b1; // Remove stale data from IDLE Stage
                        else if(in_empty) begin
                            state <= ST_READY; // Reset device
                        end
                    end
                end

                ST_MEMORY: begin
                    //TODO implement
                    state <= ST_READY;
                end

                ST_FAILED: begin
                    if(!out_full) begin
                        out_fifo <= 8'hFF;
                        out_w_en <= 1'b1;
                        state <= ST_TX_IDLE;
                    end
                end

                default: begin
                    state <= ST_READY;
                end
            endcase

        end
    end
        
    // DEBUG
    assign led[0] = ready;
    assign led[1] = ~in_empty;
    assign led[2] = ~out_empty;
    assign led[3] = (state == ST_FAILED);

    assign led0_r = state[2];
    assign led0_g = state[1];
    assign led0_b = state[0];


    //DEBUG 
    generate
        if (DEBUG) begin : gen_debug_ila
            ila_1 ila_inst(
                .clk(clk),
                .probe0(octo_spi),
                .probe1(state),
                .probe2(ck_sck),
                .probe3(ck_ss),
                .probe4(ck_rst),
                .probe5(acknack),
                .probe6(ready),
                .probe7(out_r_en),
                .probe8(out_w_en),
                .probe9(in_w_en),
                .probe10(in_r_en),
                .probe11(ck_ss_s),
                .probe12(in_empty),
                .probe13(out_empty),
                .probe14(in_fifo),
                .probe15(out_fifo),
                .probe16(rx_capture),
                .probe17(out_data),
                .probe18(tx_out),
                .probe19(opcode)
            );
        end
    endgenerate


    endmodule
