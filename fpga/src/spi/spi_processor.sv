module spi_processor #(

) (
    input spi_pck::state_e state,
    input logic [7:0] in_fifo,
    input logic out_full,
    in_empty,
    reset_s,
    clk,
    mem_busy,
    mem_data_on_wire,
    output logic proc_w_en,
    proc_r_en,
    proc_done,
    sim_running,
    mem_toggle,
    output logic [7:0] out_fifo,
    mem_out,
    mem_in,
    mem_len,
    output logic [31:0] mem_addr,
    output memory_pkg::transaction_e mem_transaction,
    output spi_pck::opcode_e opcode
);

  import spi_pck::*;

  // in_read handlers registration MUX proc_1 | proc_2 | ...
  logic echo_r_en = 1'b0;
  logic write_cons_r_en = 1'b0;
  logic fail_r_en = 1'b0;
  assign proc_r_en = echo_r_en | write_cons_r_en | fail_r_en;

  // out_write handlers registration MUX proc_1 | proc_2 | ...
  logic echo_w_en = 1'b0;
  logic info_w_en;
  logic read_cons_w_en;
  logic fail_w_en = 1'b0;
  logic write_sprite_w_en = 1'b0;
  logic sprite_write_w_en = 1'b0;

  assign proc_w_en = echo_w_en |
    info_w_en                  |
    read_cons_w_en             |
    fail_w_en                  |
    write_sprite_w_en          |
    sprite_write_w_en;

  // Done signals
  logic echo_done = 1'b0;
  logic info_done;
  logic start_sim_done;
  logic stop_sim_done;
  logic write_cons_done;
  logic read_cons_done;
  logic write_sprite_done;
  logic sprite_write_done;
  logic sprite_update_done;
  logic sprite_delete_done;
  assign proc_done = echo_done |
    info_done                  |
    write_cons_done            |
    read_cons_done             |
    start_sim_done             |
    stop_sim_done              |
    write_sprite_done          |
    sprite_write_done          |
    sprite_update_done         |
    sprite_delete_done;

  // Run signals
  logic send_info = 1'b0;
  logic send_read_cons = 1'b0;
  logic send_write_cons = 1'b0;
  logic send_write_sprite = 1'b0;
  logic send_sprite_write = 1'b0;
  logic send_sprite_update = 1'b0;
  logic send_sprite_delete = 1'b0;

  // Output buffer muxing
  logic [7:0] info_fifo;
  logic [7:0] read_cons_fifo;
  logic [7:0] read_box_fifo;
  logic [7:0] write_sprite_fifo;
  logic [7:0] sprite_write_fifo;


  always_ff @(posedge clk) begin

    // general driver for popping data on IN_FIFO and driving onto OUT_FIFO
    // During TX / TX. Issued under proc to allow the processes to keep ownership over data flow
    // Processes are registered in DECODE, it is recommended to MUX read / write toggles to subprocesses
    // e.g in_r_en = cmd1_in_r_en | cmd2_in_r_en

    if (reset_s) begin
      echo_r_en <= 1'b0;
      echo_w_en <= 1'b0;
      echo_done <= 1'b0;
      fail_w_en <= 1'b0;
      fail_r_en <= 1'b0;
      write_cons_r_en <= 1'b0;
      read_cons_w_en <= 1'b0;
      opcode <= spi_pck::opcode_e'(4'h0);
      out_fifo <= 8'h00;
      send_info <= 1'b0;
      send_write_cons <= 1'b0;
      send_read_cons <= 1'b0;
      send_write_sprite <= 1'b0;
      send_sprite_write <= 1'b0;
      send_sprite_update <= 1'b0;
      send_sprite_delete <= 1'b0;
    end else begin

      // Most signals are handled by respective submodules
      echo_r_en      <= 1'b0;
      echo_w_en      <= 1'b0;
      fail_w_en      <= 1'b0;
      fail_r_en      <= 1'b0;
      echo_done      <= 1'b0;
      start_sim_done <= 1'b0;
      stop_sim_done  <= 1'b0;

      case (state)

        ST_FAILED: begin
          opcode <= FAILED;
        end

        ST_DECODE: begin
          // USING FWFT FIFO's allowing us to peek at the read bus without popping.
          // Each dispatched command decides for itself if it should pop the command
          // By setting in_r_en <= 1'b1;
          opcode <= spi_pck::opcode_e'(in_fifo[7:4]);
        end

        ST_BUSY: begin
          case (spi_pck::opcode_e'(opcode))

            // Register command handler here. they should own their own read/write signals.
            // Register these in the in_r_en / out_w_en MUX assignments.
            // Out_fifo is owned by this always_ff block and is the intended output register
            // for any response data.

            ECHO: begin
              if (!out_full && !in_empty) begin
                out_fifo  <= in_fifo;
                echo_w_en <= 1'b1;
                echo_r_en <= 1'b1;
              end else begin
                echo_done <= 1'b1;
              end
            end

            INFO: begin
              if (!info_done) begin
                send_info <= 1'b1;
                if (info_w_en) begin
                  out_fifo <= info_fifo;
                end
              end else send_info <= 1'b0;
            end

            WRITE_CONS: begin
              if (!write_cons_done) begin
                send_write_cons <= 1'b1;
              end else send_write_cons <= 1'b0;
            end

            READ_CONS: begin
              if (!read_cons_done) begin
                send_read_cons <= 1'b1;
                if (read_cons_w_en) begin
                  out_fifo <= read_cons_fifo;
                end
              end else send_read_cons <= 1'b0;
            end

            WRITE_SPRITE: begin
              if (!write_sprite_done) begin
                  send_write_sprite <= 1'b1;
                  if (write_sprite_w_en) begin
                    out_fifo <= write_sprite_fifo;
                  end
              end else send_write_sprite <= 1'b0;
            end

            SPRITE_WRITE: begin
              if (!sprite_write_done) begin
                  send_sprite_write <= 1'b1;
                  if (sprite_write_w_en) begin
                    out_fifo <= sprite_write_fifo;
                  end
              end else send_sprite_write <= 1'b0;
            end

            SPRITE_UPDATE: begin
              if (!sprite_update_done) begin
                send_sprite_update <= 1'b1;
              end else send_sprite_update <= 1'b0;
            end

            SPRITE_DELETE: begin
              if (!sprite_delete_done) begin
                sprite_delete_done <= 1'b1;
              end else sprite_delete_done <= 1'b0;
            end

            START_SIM: begin
              sim_running <= 1'b1;
              start_sim_done <= 1'b1;
            end

            STOP_SIM: begin
              sim_running   <= 1'b0;
              stop_sim_done <= 1'b1;
            end

            FAILED: begin
              if (!out_full) begin
                if (!in_empty) begin
                  fail_r_en <= 1'b1;
                end else begin
                  out_fifo <= 8'hE0;
                  fail_w_en <= 1'b1;
                  opcode <= UNSET;
                end
              end
            end

            default: begin
              opcode <= FAILED;
            end
          endcase
        end

        default: begin
          // Do nothing
        end

      endcase
    end
  end

  info info_cmd_processor (
      .clk(clk),
      .info_fifo(info_fifo),
      .out_full(out_full),
      .info_done(info_done),
      .info_w_en(info_w_en),
      .send_info(send_info)
  );

  write_cons write_cons_cmd_processor (
      .clk(clk),
      .ck_rst(ck_rst),
      .data_rdy(data_rdy),
      .mem_busy(mem_busy),
      .mem_len(mem_len),
      .mem_addr(mem_addr),
      .mem_func(mem_transaction),
      .mem_in(mem_in),
      .mem_toggle(mem_toggle),
      .in_fifo(in_fifo),
      .write_cons_fifo(write_cons_fifo),
      .write_cons_done(write_cons_done),
      .write_cons_r_en(write_cons_r_en)
  );

  read_cons read_cons_cmd_processor (
      .clk(clk),
      .ck_rst(ck_rst),
      .data_rdy(data_rdy),
      .in_fifo(in_fifo),
      .mem_len(mem_len),
      .mem_busy(mem_busy),
      .mem_toggle(mem_toggle),
      .mem_addr(mem_addr),
      .mem_func(mem_func),
      .mem_out(mem_out),
      .read_cons_fifo(read_cons_fifo),
      .read_cons_done(read_cons_done),
      .read_cons_w_en(read_cons_w_en),
      .read_cons_r_en(read_cons_r_en)
  );

endmodule
