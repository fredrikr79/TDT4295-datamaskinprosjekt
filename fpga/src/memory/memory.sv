module memory #(
    parameter int unsigned NumClients = memory_pkg::NumMemClients
) (
    // input from fpga
    input logic clk,
    input logic reset,
    input logic enable,

    // Per-client request ports.
    input logic [NumClients-1:0] client_toggle,

    input memory_pkg::transaction_e client_next_transaction[NumClients],
    input memory_pkg::address_t client_address[NumClients],
    input memory_pkg::message_length_t client_length[NumClients],
    input logic [7:0] client_write_data[NumClients],
    input logic [NumClients-1:0] client_allow,

    // Info to fpga
    output logic [NumClients-1:0] client_busy,
    output logic [NumClients-1:0] client_data_on_cable,
    output logic [NumClients-1:0] client_grant,
    output logic [7:0] read_data,

    // Memory pins
    output logic out_clk,
    output logic out_clk_n,
    output logic cs_n,
    output logic reset_n,
    inout wire [7:0] dq,
    inout wire rwds
);

  localparam int unsigned ClientIdBits = (NumClients > 1) ? $clog2(NumClients) : 1;
  typedef logic [ClientIdBits-1:0] client_id_t;

  logic initializer_finished, controller_busy, controller_data_on_cable;
  logic [7:0] controller_read_data, initializer_write_data, controller_write_data;
  memory_pkg::transaction_e controller_next_transaction, initializer_next_transaction;
  memory_pkg::address_t controller_address, initializer_address;
  memory_pkg::message_length_t initializer_length, controller_length;
  logic initializer_toggle, controller_toggle;

  // Captured request per client
  logic [NumClients-1:0] client_toggle_q;
  logic [NumClients-1:0] pending;
  memory_pkg::transaction_e req_transaction[NumClients];
  memory_pkg::address_t req_address[NumClients];
  memory_pkg::message_length_t req_length[NumClients];

  logic owner_valid;
  client_id_t owner;
  logic owner_started;
  logic arb_toggle;

  // Fixed priority: lowest index first.
  logic [NumClients-1:0] eligible;
  assign eligible = pending & client_allow;

  logic any_eligible;
  client_id_t next_owner;
  always_comb begin
    any_eligible = 1'b0;
    next_owner   = '0;
    for (int unsigned i = 0; i < NumClients; i++) begin
      if (eligible[i] && !any_eligible) begin
        next_owner   = client_id_t'(i);
        any_eligible = 1'b1;
      end
    end
  end

  always_ff @(posedge clk) begin
    if (reset) begin
      // Tracking the toggles
      client_toggle_q <= client_toggle;
      pending <= '0;
      owner_valid <= 1'b0;
      owner <= '0;
      owner_started <= 1'b0;
      arb_toggle <= 1'b0;
      for (int unsigned i = 0; i < NumClients; i++) begin
        req_transaction[i] <= memory_pkg::NONE;
        req_address[i] <= '0;
        req_length[i] <= '0;
      end
    end else if (enable) begin
      if (owner_valid) begin
        if (controller_busy) owner_started <= 1'b1;
        else if (owner_started) begin
          pending[owner] <= 1'b0;
          owner_valid <= 1'b0;
          owner_started <= 1'b0;
        end
      end else if (initializer_finished && !controller_busy && any_eligible) begin
        owner <= next_owner;
        owner_valid <= 1'b1;
        owner_started <= 1'b0;
        arb_toggle <= ~arb_toggle;
      end

      // Capture new requests
      for (int unsigned i = 0; i < NumClients; i++) begin
        if (client_toggle[i] != client_toggle_q[i] && !pending[i]) begin
          pending[i] <= 1'b1;
          req_transaction[i] <= client_next_transaction[i];
          req_address[i] <= client_address[i];
          req_length[i] <= client_length[i];
        end
      end
      client_toggle_q <= client_toggle;
    end
  end

  // Before the initializer is finished it owns the controller
  assign controller_toggle = initializer_finished ? arb_toggle : initializer_toggle;
  assign controller_next_transaction = initializer_finished ? req_transaction[owner]
                                                            : initializer_next_transaction;
  assign controller_address = initializer_finished ? req_address[owner] : initializer_address;
  assign controller_length = initializer_finished ? req_length[owner] : initializer_length;
  assign controller_write_data = initializer_finished ? client_write_data[owner]
                                                      : initializer_write_data;

  assign read_data = controller_read_data;

  always_comb begin
    for (int unsigned i = 0; i < NumClients; i++) begin
      client_grant[i] = owner_valid && (owner == client_id_t'(i));
      client_data_on_cable[i] = initializer_finished && client_grant[i] && controller_data_on_cable;
      client_busy[i] = !initializer_finished || pending[i];
    end
  end

  memory_controller controller (
      .clk,
      .reset,
      .enable,
      .transaction_toggle(controller_toggle),
      .next_transaction(controller_next_transaction),
      .address(controller_address),
      .length(controller_length),
      .write_data(controller_write_data),
      .busy(controller_busy),
      .data_on_cable(controller_data_on_cable),
      .read_data(controller_read_data),
      .out_clk,
      .out_clk_n,
      .reset_n,
      .dq,
      .cs_n,
      .rwds
  );


  memory_initializer initilizer (
      .clk,
      .reset,
      .enable,
      .controller_busy,
      .controller_data_on_cable,
      .finished(initializer_finished),
      .controller_toggle(initializer_toggle),
      .next_transaction(initializer_next_transaction),
      .next_address(initializer_address),
      .next_length(initializer_length),
      .write(initializer_write_data)
  );

endmodule
