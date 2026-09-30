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
    UNSET = 0000,
    ECHO = 0001,
    INFO = 0002,
    FAILED = 0003
  } opcode_e;

endpackage
