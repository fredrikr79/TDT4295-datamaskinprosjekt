module memory_intializer (
    input clk,
    reset,
    enable,
    controller_busy,
    controller_data_on_wire,
    output logic finished,
    controller_toggle,
    output memory_pkg::transaction_e next_transaction,
    output memory_pkg::address_t next_address,
    output memory_pkg::message_length_t next_length
);

  localparam int NInitTransactions = 10;
  localparam int TransactionCount  = NInitTransactions > 1 ? $clog2(NInitTransactions) : 0;
  typedef struct {
    memory_pkg::transaction_e transaction;
    memory_pkg::address_t address;
    memory_pkg::message_length_t length;
  } memory_meta_data_t;

  localparam memory_meta_data_t InitTransactions[NInitTransactions] = '{
      default: '{transaction: memory_pkg::NONE, address: 31'b0, length: 0}
      // TODO: make init transactions
  };
  logic [TransactionCount-1:0] i_current_transactions;

  always_ff @(posedge clk) begin
    if (reset) begin
      finished <= 0;
      next_transaction <= memory_pkg::NONE;
      next_length <= 0;
      i_current_transactions <= 0;
    end else if (enable) begin
      if (i_current_transactions >= NInitTransactions) begin
        finished <= 1;
      end else if (!controller_busy) begin
        next_transaction <= InitTransactions[i_current_transactions].transaction;
        next_address <= InitTransactions[i_current_transactions].address;
        next_length <= InitTransactions[i_current_transactions].length;
        i_current_transactions <= i_current_transactions + 1;
      end
    end
  end
endmodule
