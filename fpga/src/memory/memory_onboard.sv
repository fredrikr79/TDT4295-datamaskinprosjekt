module memory_onboard #(
    parameter int MEM_SIZE = 65536
) (
    // Input from FPGA
    input logic clk,
    input logic reset,
    input logic enable,
    input logic transaction_toggle,

    input memory_pkg::transaction_e next_transaction,
    input memory_pkg::address_t address,
    input memory_pkg::message_length_t length,
    input logic [7:0] write_data,

    // Info to FPGA
    output logic busy,
    output logic data_on_cable,
    output logic [7:0] read_data,

    // Memory pins (unused in behavioral implementation)
    output logic out_clk,
    output logic out_clk_n,
    output logic cs_n,
    output logic reset_n,
    inout wire [7:0] dq,
    inout wire rwds
);

    // ----------------------------------------
    // Internal byte-addressable memory
    // ----------------------------------------

    logic [7:0] memory [0:MEM_SIZE-1];

    // ----------------------------------------
    // State machine
    // ----------------------------------------

    typedef enum logic [1:0] {
        IDLE,
        SETUP,
        ACTIVE,
        TURNAROUND
    } mem_state_t;

    mem_state_t state = IDLE;

    // ----------------------------------------
    // Transaction registers
    // ----------------------------------------

    logic previous_toggle;

    memory_pkg::transaction_e transaction_type;

    memory_pkg::address_t transaction_address;
    memory_pkg::message_length_t transaction_length;

    logic [31:0] byte_count;
    logic [31:0] active_count;

    longint unsigned access_addr;

    always_comb begin
        access_addr = transaction_address;
        access_addr = access_addr + byte_count;
    end

    // ----------------------------------------
    // Status signals
    // ----------------------------------------

    assign busy = (state == SETUP) ||
                  (state == TURNAROUND);

    assign data_on_cable = (state == ACTIVE) && enable;

    // ----------------------------------------
    // Unused external memory pins
    // ----------------------------------------

    assign out_clk   = 1'b0;
    assign out_clk_n = 1'b1;
    assign cs_n      = 1'b1;
    assign reset_n   = ~reset;

    assign dq   = 8'hZZ;
    assign rwds = 1'bZ;

    // ----------------------------------------
    // Memory controller
    // ----------------------------------------

    always_ff @(posedge clk) begin

        if (reset) begin

            state               <= IDLE;
            previous_toggle    <= transaction_toggle;

            transaction_address <= '0;
            transaction_length  <= '0;

            byte_count          <= 32'd0;
            active_count        <= 32'd0;

            transaction_type    <= memory_pkg::READ;

            read_data           <= 8'h00;

        end else if (enable) begin

            case (state)

                // ==================================
                // IDLE
                // ==================================

                IDLE: begin

                    if (transaction_toggle != previous_toggle) begin

                        // Capture new transaction
                        previous_toggle     <= transaction_toggle;

                        transaction_address <= address;
                        transaction_length  <= length;
                        transaction_type    <= next_transaction;

                        byte_count   <= 32'd0;
                        active_count <= 32'd0;

                        if (length == 0)
                            state <= TURNAROUND;
                        else
                            state <= SETUP;

                    end

                end

                // ==================================
                // SETUP
                // ==================================

                SETUP: begin

                    byte_count   <= 32'd0;
                    active_count <= 32'd0;

                    state <= ACTIVE;

                end

                // ==================================
                // ACTIVE
                // ==================================

                ACTIVE: begin

                    case (transaction_type)

                        // --------------------------
                        // WRITE TRANSACTION
                        // --------------------------

                        memory_pkg::WRITE: begin

                            // Two-cycle pipeline allowance:
                            // The SPI procedure registers write_data
                            // after data_on_cable becomes active.

                            if (active_count >= 2) begin

                                if (access_addr < MEM_SIZE) begin

                                    memory[access_addr] <= write_data;

                                end

                                byte_count <= byte_count + 32'd1;

                                if (byte_count + 32'd1 >= transaction_length) begin

                                    state <= TURNAROUND;

                                end

                            end

                            active_count <= active_count + 32'd1;

                        end

                        // --------------------------
                        // READ TRANSACTION
                        // --------------------------

                        memory_pkg::READ: begin

                            if (access_addr < MEM_SIZE) begin

                                read_data <= memory[access_addr];

                            end else begin

                                read_data <= 8'h00;

                            end

                            byte_count <= byte_count + 32'd1;

                            if (byte_count + 32'd1 >= transaction_length) begin

                                state <= TURNAROUND;

                            end

                        end

                        default: begin

                            state <= TURNAROUND;

                        end

                    endcase

                end

                // ==================================
                // TURNAROUND
                // ==================================

                TURNAROUND: begin

                    state <= IDLE;

                end

                default: begin

                    state <= IDLE;

                end

            endcase

        end

    end

endmodule