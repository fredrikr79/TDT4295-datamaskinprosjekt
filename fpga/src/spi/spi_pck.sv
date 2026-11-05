package spi_pck;

  // FSM STATES
  typedef enum bit [7:0] {
    ST_BOOT    = 8'b00000100,
    ST_READY   = 8'b00001010,
    ST_RX      = 8'b00010111,
    ST_DECODE  = 8'b00011011,
    ST_BUSY    = 8'b00110101,
    ST_TX_IDLE = 8'b00111001,
    ST_TX_SEND = 8'b01010001,
    ST_MEMORY  = 8'b01011001,
    ST_FAILED  = 8'b11111111
  } state_e;

  // KNOWN COMMANDS
  typedef enum bit [3:0] {
    UNSET         = 0000,
    ECHO          = 0001,
    INFO          = 0002,
    WRITE_CONS    = 0003,
    READ_CONS     = 0004,
    WRITE_BOX     = 0005,
    READ_BOX      = 0006,
    WRITE_SPRITE  = 0007,
    SPRITE_WRITE  = 0008,
    SPRITE_UPDATE = 0009,
    SPRITE_DELETE = 0010,
    START_SIM     = 0011,
    STOP_SIM      = 0012,
    FAILED        = 0015
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
