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
    UNKNOWN_COMMAND_ERROR     = E0,
    MISSING_PARAMETER_ERROR   = E1,
    TOO_MANY_PARAMETERS_ERROR = E2,
    ILLEGAL_VALUE_ERROR       = E3,
    RUNTIME_ERROR             = E4,
    MEMORY_ERROR              = E5,
    UNKNOWN_IDENTIFIER_ERROR  = E6,
    UNKNOWN_VALUE_ERROR       = E7,
    TIMEOUT_ERROR             = E8,
    RESOURCE_EXHAUSTED_ERROR  = E9,
    INVALID_STATE_ERROR       = EA,
    UNKNOWN_ERROR             = EF
  } error_codes_e;

endpackage
