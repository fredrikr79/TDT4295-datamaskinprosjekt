module memory_initializer #(
  parameter int unsigned StartupDelayCycles = config_pkg::GlobalClockFrequency / 1000 * 300
)(
    input clk,
    reset,
    enable,
    controller_busy,
    controller_data_on_cable,
    output logic finished,
    controller_toggle,
    output memory_pkg::transaction_e next_transaction,
    output memory_pkg::address_t next_address,
    output memory_pkg::message_length_t next_length,
    output logic [7:0] write
);

  localparam int NInitTransactions = 9;
  localparam int TransactionCount  = $clog2(NInitTransactions + 1);
  typedef enum logic [2:0] {
    POWER_UP_DELAY,
    READY,
    WAIT_BUSY,
    WAIT_DONE
  } init_state_t;
  typedef struct packed {
    memory_pkg::transaction_e transaction;
    memory_pkg::address_t address;
    memory_pkg::message_length_t length;
    logic [memory_pkg::MaxMessageLength-1:0][7:0] write_data;
  } memory_meta_data_t;

  localparam memory_meta_data_t InitTransactions[NInitTransactions] = '{
      '{
          transaction: memory_pkg::WRITE_ENABLE,
          address: 32'b0,
          length: 0,
          write_data: '{default: 'h00}
      },
      // Write to configuration registers 0
      '{
          transaction: memory_pkg::WRITE_ANY_REGISTER,
          address: 'h00000004,
          length: 2,
          write_data: '{
            0: {1'b1, memory_pkg::DriveStrength, 4'b1111},
            1: {memory_pkg::InitialLatency, memory_pkg::FixedLatency, memory_pkg::UseHybridBurst, memory_pkg::BurstLength},
            default: 8'b0
          }
      },
      '{
          transaction: memory_pkg::WRITE_ENABLE,
          address: 32'b0,
          length: 0,
          write_data: '{default: 'h00}
      },
      '{
          transaction: memory_pkg::WRITE_ANY_REGISTER,
          address: 'h00400004,
          length: 2,
          write_data: '{
            0: {1'b1, memory_pkg::DriveStrength, 4'b1111},
            1: {memory_pkg::InitialLatency, memory_pkg::FixedLatency, memory_pkg::UseHybridBurst, memory_pkg::BurstLength},
            default: 8'b0
          }
      },
      // Write to configuration registers 1
      '{
          transaction: memory_pkg::WRITE_ENABLE,
          address: 32'b0,
          length: 0,
          write_data: '{default: 'h00}
      },
      '{
          transaction: memory_pkg::WRITE_ANY_REGISTER,
          address: 'h00000006,
          length: 2,
          write_data: '{
            0: 8'hFF,
            1: {1'b1, 1'b1, 1'b0, 3'b000, 2'b00},
            default: 'h00
          }
      },
      '{
          transaction: memory_pkg::WRITE_ENABLE,
          address: 32'b0,
          length: 0,
          write_data: '{default: 'h00}
      },
      '{
          transaction: memory_pkg::WRITE_ANY_REGISTER,
          address: 'h00400006,
          length: 2,
          write_data: '{
            0: 8'hFF,
            1: {1'b1, 1'b1, 1'b0, 3'b000, 2'b00},
            default: 'h00
          }
      },
      '{
          transaction: memory_pkg::WRITE_ENABLE,
          address: 32'b0,
          length: 0,
          write_data: '{default: 'h00}
      }
  };

  // Ensure 0 delay case is handled
  localparam int unsigned StartupCountWidth = (StartupDelayCycles > 1)
    ? $clog2(StartupDelayCycles)
    : 1;
  logic [StartupCountWidth-1:0] start_delay_cycles_count;

  logic [TransactionCount-1:0] i_current_transactions;
  memory_pkg::message_length_t current_byte_written;
  init_state_t state;

  always_ff @(posedge clk) begin
    if (reset) begin
      finished <= 0;
      controller_toggle <= 0;
      next_transaction <= memory_pkg::NONE;
      next_address <= 0;
      next_length <= 0;
      write <= 0;
      i_current_transactions <= 0;
      current_byte_written <= 0;
      start_delay_cycles_count <= 0;
      state <= (StartupDelayCycles == 0 ? READY : POWER_UP_DELAY);
    end else if (enable) begin
      case (state)
        POWER_UP_DELAY: begin
          if(start_delay_cycles_count == StartupCountWidth'(StartupDelayCycles - 1))
            state <= READY;
          else
            start_delay_cycles_count <= start_delay_cycles_count + 1'b1;
        end
        READY: begin
          if (i_current_transactions == TransactionCount'(NInitTransactions)) begin
            finished <= 1;
          end else if (!controller_busy) begin
            next_transaction <= InitTransactions[i_current_transactions].transaction;
            next_address <= InitTransactions[i_current_transactions].address;
            next_length <= InitTransactions[i_current_transactions].length;
            write <= InitTransactions[i_current_transactions].write_data[0];
            controller_toggle <= ~controller_toggle;
            current_byte_written <= 1;
            state <= WAIT_BUSY;
          end
        end
        WAIT_BUSY: if (controller_busy) state <= WAIT_DONE;
        WAIT_DONE: begin
          if (!controller_busy) begin
            i_current_transactions <= i_current_transactions + 1'b1;
            state <= READY;
          end else if (controller_data_on_cable) begin
            write <= InitTransactions[i_current_transactions].write_data[current_byte_written];
            current_byte_written <= current_byte_written + 1'b1;
          end
        end
        default:   state <= READY;
      endcase
    end
  end
endmodule
