package memory_pkg;
  typedef enum bit [7:0] {
    NONE = 'h00,
    RESET_ENABLE = 'h66,
    RESET = 'h99,
    DEEP_POWER_DOWN = 'hB9,
    DEEP_POWER_UP = 'h01,
    WRITE_ENABLE = 'h06,
    WRITE_DISABLE = 'h04,
    READ_ANY_REGISTER = 'h65,
    WRITE_ANY_REGISTER = 'h71,
    READ_ID = 'h9F,
    READ = 'hEE,
    WRITE = 'hDE
  } transaction_e;

  typedef enum bit [3:0] {
    IDLE = 0,
    STARTING_TRANSACTION = 1,
    SENDING_CMD = 2,
    SENDING_ADDRESS = 3,
    WAITING_ON_LATENCY = 4,
    READING = 5,
    WRITING = 6
  } state_e;

  typedef struct {
    logic rwds;
    logic [7:0] dq;
    state_e next_state;
  } transaction_output_t;

  localparam int unsigned MaxMessageLength = 64;

  localparam int unsigned MessageLengthRegisterWidth = (MaxMessageLength > 1) ? $clog2(
      MaxMessageLength
  ) : 1;

  typedef logic [MessageLengthRegisterWidth-1:0] message_length_t;

  function automatic message_length_t get_rw_length(input transaction_e transaction,
                                                    input message_length_t optional_length);
    case (transaction)
      READ, WRITE: return optional_length;
      READ_ANY_REGISTER, WRITE_ANY_REGISTER: return 2;
      READ_ID: return 4;
      default: return 0;
    endcase
  endfunction

  typedef logic [31:0] address_t;

  localparam int unsigned MemoryClockSlowDownFactor = 2;
  localparam int unsigned MemoryClockFrequency =
    config_pkg::GlobalClockFrequency / MemoryClockSlowDownFactor;

  localparam int unsigned LatencyCount = MemoryClockFrequency < 85_000_000 ? 3
  : MemoryClockFrequency < 104_000_000 ? 4
  : MemoryClockFrequency < 133_000_000 ? 5
  : MemoryClockFrequency < 166_000_000 ? 6
  : 7;
  localparam bit [4:0] InitialLatency =
      MemoryClockFrequency <  85_000_000 ? 'b1110
  : MemoryClockFrequency < 104_000_000 ? 'b1111
  : MemoryClockFrequency < 133_000_000 ? 'b0000
  : MemoryClockFrequency < 166_000_000 ? 'b0001
  : 'b0010;


  function automatic logic waiting_is_finished(input message_length_t amount_of_latency_ticks,
                                               input logic double_latency);
    memory_pkg::message_length_t latency_count;
    latency_count = 5'(MemoryClockSlowDownFactor)
                    * (double_latency ? 5'(2 * LatencyCount) : 5'(LatencyCount));

    return amount_of_latency_ticks > latency_count;
  endfunction
  function automatic logic transaction_is_finished(input message_length_t amount_of_rws,
                                                   input transaction_e transaction,
                                                   input message_length_t optional_length);
    return amount_of_rws == get_rw_length(transaction, optional_length) - 1;
  endfunction

  localparam bit UseFixedLatency = 0;
  localparam bit [1:0] BurstLength = 'b00; // burst length of 128 bits


  // Possible drive strengths
  // 000:  34 ohm
  // 001: 115 ohm
  // 010:  67 ohm
  // 011:  46 ohm
  // 100:  34 ohm
  // 101:  27 ohm
  // 110:  22 ohm
  // 111:  19 ohm
  localparam bit [2:0] DriveStrength = 'b000;

  // 1 - single ended clock
  // 0 - differential clock
  localparam bit MemoryClockType = 0;

  localparam int unsigned NumMemClients = 2;
  localparam int unsigned MemClientCellsID = 0;
  localparam int unsigned MemClientSpiID = 1;

endpackage
