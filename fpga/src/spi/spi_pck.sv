package spi_pck;

  // FSM STATES
  typedef enum bit [7:0] {
    ST_BOOT    = 8'b00000100,
    ST_READY   = 8'b00001010,
    ST_RX      = 8'b00010111,
    ST_DECODE  = 8'b00011011,
    ST_BUSY    = 8'b00110101,
    ST_FINISH  = 8'b00111011,
    ST_TX_IDLE = 8'b01000001,
    ST_TX_SEND = 8'b01001001,
    ST_MEMORY  = 8'b01010001,
    ST_FAILED  = 8'b11111100
  } state_e;

  // KNOWN COMMANDS
  typedef enum bit [3:0] {
    UNSET         = 4'd0000,
    ECHO          = 4'd0001,
    INFO          = 4'd0002,
    WRITE_CONS    = 4'd0003,
    READ_CONS     = 4'd0004,
    WRITE_BOX     = 4'd0005,
    READ_BOX      = 4'd0006,
    WRITE_SPRITE  = 4'd0007,
    SPRITE_WRITE  = 4'd0008,
    SPRITE_UPDATE = 4'd0009,
    SPRITE_DELETE = 4'd0010,
    START_SIM     = 4'd0011,
    STOP_SIM      = 4'd0012,
    FAILED        = 4'd0015
  } opcode_e;

  typedef enum bit [7:0] {
    NO_ERROR                  = 8'hE0,
    MISSING_PARAMETER_ERROR   = 8'hE1,
    TOO_MANY_PARAMETERS_ERROR = 8'hE2,
    ILLEGAL_VALUE_ERROR       = 8'hE3,
    RUNTIME_ERROR             = 8'hE4,
    MEMORY_ERROR              = 8'hE5,
    UNKNOWN_IDENTIFIER_ERROR  = 8'hE6,
    RESOURCE_EXHAUSTED_ERROR  = 8'hE7,
    ILLEGAL_STATE_ERROR       = 8'hE8,
    UNKNOWN_COMMAND_ERROR     = 8'hE9,
    CRITICAL_ERROR            = 8'hEA,
    UNKNOWN_ERROR             = 8'hEF
  } error_codes_e;

  typedef struct packed {
    logic valid;
    logic [7:0] code;
  } error_event_t;

endpackage
