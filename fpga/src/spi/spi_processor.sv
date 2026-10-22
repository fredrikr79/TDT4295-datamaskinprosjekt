module spi_processor #(

) (
    input spi_pck::state_e state,
    input logic [7:0] in_fifo, mem_out,
    input logic out_full,
    in_empty,
    reset_s,
    clk,
    mem_busy,
    mem_data_on_wire,
    sprite_id_found,
    input logic [$clog2(hud_pkg::SpriteLutDepth)-1:0] sprite_available_id,
    input spi_pck::error_event_t error_event, // Incoming global error decision
    output logic proc_w_en,
    proc_r_en,
    proc_done,
    sim_running,
    mem_toggle,
    sprite_req_valid,
    write_sprite_valid,
    error_valid,
    output logic [7:0] out_fifo,
    mem_in,
    error_code,
    output logic [15:0] mem_len,
    output logic [31:0] mem_addr,
    output spi_pck::opcode_e opcode,
    output memory_pkg::transaction_e mem_transaction,
    output hud_pkg::sprite_inst_entry_t sprite_req_body,
    output hud_pkg::sprite_op_e sprite_req_op
);

  import spi_pck::*;
  import hud_pkg::*;
  import memory_pkg::*;

  // in_read handlers registration MUX proc_1 | proc_2 | ...
  logic
      cmd_r_en = 1'b0,
      echo_r_en = 1'b0,
      read_cons_r_en,
      write_cons_r_en,
      read_box_r_en,
      write_box_r_en,
      write_sprite_r_en,
      sprite_write_r_en,
      sprite_update_r_en,
      sprite_delete_r_en,
      fail_r_en = 1'b0;

  assign proc_r_en = cmd_r_en  |
    echo_r_en                  |
    read_cons_r_en             |
    write_cons_r_en            |
    read_box_r_en              |
    write_box_r_en             |
    write_sprite_r_en          |
    sprite_write_r_en          |
    sprite_update_r_en         |
    sprite_delete_r_en         |
    fail_r_en;

  // out_write handlers registration MUX proc_1 | proc_2 | ...
  logic
      echo_w_en = 1'b0,
      info_w_en,
      read_cons_w_en,
      read_box_w_en,
      fail_w_en = 1'b0,
      write_sprite_w_en,
      sprite_write_w_en;

  assign proc_w_en = echo_w_en |
    info_w_en                  |
    read_cons_w_en             |
    read_box_w_en              |
    fail_w_en                  |
    write_sprite_w_en          |
    sprite_write_w_en;

  // Done signals
  logic
      echo_done,
      info_done,
      start_sim_done,
      stop_sim_done,
      write_cons_done,
      read_cons_done,
      write_box_done,
      read_box_done,
      write_sprite_done,
      sprite_write_done,
      sprite_update_done,
      sprite_delete_done;

  assign proc_done = echo_done |
    info_done                  |
    write_cons_done            |
    read_cons_done             |
    read_box_done              |
    write_box_done             |
    start_sim_done             |
    stop_sim_done              |
    write_sprite_done          |
    sprite_write_done          |
    sprite_update_done         |
    sprite_delete_done;

  // Run signals
  logic
      send_info = 1'b0,
      send_read_cons = 1'b0,
      send_write_cons = 1'b0,
      send_read_box = 1'b0,
      send_write_box = 1'b0,
      send_write_sprite = 1'b0,
      send_sprite_write = 1'b0,
      send_sprite_update = 1'b0,
      send_sprite_delete = 1'b0;

  // Output buffer muxing
  logic [7:0] info_fifo,
    read_cons_fifo,
    read_box_fifo,
    write_sprite_fifo,
    sprite_write_fifo;


  // Mem transaction toggle mux
  logic read_cons_mem_req,
    write_cons_mem_req,
    read_box_mem_req,
    write_box_mem_req,
    write_sprite_mem_req;

  always_ff @(posedge clk) begin
    if (read_cons_mem_req     |
        write_cons_mem_req    |
        read_box_mem_req      |
        write_box_mem_req     |
        write_sprite_mem_req
    ) begin
      mem_toggle <= ~mem_toggle;
    end
  end

  // Mem transaction addr mux
  logic [31:0] read_cons_mem_addr,
    write_cons_mem_addr,
    read_box_mem_addr,
    write_box_mem_addr,
    write_sprite_mem_addr;


  // Mem transaction len mux
  logic [15:0] read_cons_mem_len,
    write_cons_mem_len,
    read_box_mem_len,
    write_box_mem_len,
    write_sprite_mem_len;

  always_ff @(posedge clk) begin
    case (opcode)
      READ_CONS:    mem_len <= read_cons_mem_len;
      WRITE_CONS:   mem_len <= write_cons_mem_len;
      READ_BOX:     mem_len <= read_box_mem_len;
      WRITE_BOX:    mem_len <= write_box_mem_len;
      WRITE_SPRITE: mem_len <= write_sprite_mem_len;
      default:      mem_len <= 0;
    endcase
  end

  // Sprite LUT & Bitmap write request mux
  sprite_inst_entry_t sprite_w_body, sprite_u_body, sprite_d_body;
  logic sprite_w_valid, sprite_u_valid, sprite_d_valid;
  assign sprite_req_valid = sprite_w_valid | sprite_u_valid | sprite_d_valid;

  // ERROR detection
  always_ff @(posedge clk) begin
    case (opcode)
      ECHO: begin
        error_code <= echo_error_code;
        error_valid <= echo_error_valid;
      end
      INFO: begin
        error_code <= info_error_code;
        error_valid <= info_error_valid;
      end
      WRITE_CONS: begin
        error_code <= write_cons_error_code;
        error_valid <= write_cons_error_valid;
      end
      READ_CONS: begin
        error_code <= read_cons_error_code;
        error_valid <= read_cons_error_valid;
      end
      WRITE_BOX: begin
        error_code <= write_box_error_code;
        error_valid <= write_box_error_valid;
      end
      READ_BOX: begin
        error_code <= read_box_error_code;
        error_valid <= read_box_error_valid;
      end
      WRITE_SPRITE: begin
        error_code <=  write_sprite_error_code;
        error_valid <= write_sprite_error_valid;
      end
      SPRITE_WRITE: begin
        error_code <=  sprite_write_error_code;
        error_valid <= sprite_write_error_valid;
      end
      SPRITE_UPDATE: begin
        error_code <=  sprite_update_error_code;
        error_valid <= sprite_update_error_valid;
      end
      SPRITE_DELETE: begin
        error_code <=  sprite_delete_error_code;
        error_valid <= sprite_delete_error_valid;
      end
      START_SIM: begin
        error_code <= start_sim_error_code;
        error_valid <= start_sim_error_valid;
      end
      STOP_SIM: begin
        error_code <= stop_sim_error_code;
        error_valid <= stop_sim_error_valid;
      end
      default: begin
        if (state == ST_BUSY) begin
          error_code <= UNKNOWN_COMMAND_ERROR;
          error_valid <= 1'b1;
        end else begin
          error_code <= NO_ERROR;
          error_valid <= 1'b0;
        end
      end
      endcase
  end


  always_ff @(posedge clk) begin

    // general driver for popping data on IN_FIFO and driving onto OUT_FIFO
    // During TX / TX. Issued under proc to allow the processes to keep ownership over data flow
    // Processes are registered in DECODE, it is recommended to MUX read / write toggles to subprocesses
    // e.g in_r_en = cmd1_in_r_en | cmd2_in_r_en

    if (!reset_s) begin
      sim_running <= 1'b0;
      opcode <= spi_pck::opcode_e'(4'h0);
      out_fifo <= 8'h00;
      cmd_r_en  <= 1'b0;
      echo_r_en <= 1'b0;
      fail_r_en <= 1'b0;
      echo_w_en <= 1'b0;
      fail_w_en <= 1'b0;
      send_info <= 1'b0;
      send_write_cons <= 1'b0;
      send_read_cons <= 1'b0;
      send_write_sprite <= 1'b0;
      send_sprite_write <= 1'b0;
      send_sprite_update <= 1'b0;
      send_sprite_delete <= 1'b0;
      echo_done <= 1'b0;
      start_sim_done <= 1'b0;
      stop_sim_done <= 1'b0;
    end else begin

      // Most signals are handled by respective submodules
      cmd_r_en       <= 1'b0;
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
          // Comment out the line below if each command should itself pop the cmd byte
          cmd_r_en <= 1'b1;
          opcode   <= spi_pck::opcode_e'(in_fifo[7:4]);
        end

        ST_BUSY: begin
          unique case (spi_pck::opcode_e'(opcode)) // Default case handled by ERROR MUX

            // Register command handler here. they should own their own read/write signals.
            // Register these in the in_r_en / out_w_en MUX assignments.
            // Out_fifo is owned by this always_ff block and is the intended output register
            // for any response data.

            // Any shared non-timing-sensitive signals should be set in case blocks below.

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
                mem_addr <= write_cons_mem_addr;
                mem_transaction <= WRITE;
              end else send_write_cons <= 1'b0;
            end

            READ_CONS: begin
              if (!read_cons_done) begin
                send_read_cons <= 1'b1;
                mem_addr <= read_cons_mem_addr;
                mem_transaction <= READ;
                if (read_cons_w_en) begin
                  out_fifo <= read_cons_fifo;
                end
              end else send_read_cons <= 1'b0;
            end

            WRITE_BOX: begin
              if (!write_box_done) begin
                send_write_box <= 1'b1;
                mem_addr <= write_box_mem_addr;
                mem_transaction <= WRITE;
              end else send_write_box <= 1'b0;
            end

            READ_BOX: begin
              if (!read_box_done) begin
                send_read_box <= 1'b1;
                mem_addr <= read_box_mem_addr;
                mem_transaction <= READ;
                if (read_box_w_en) begin
                  out_fifo <= read_box_fifo;
                end
              end else send_read_box <= 1'b0;
            end

            WRITE_SPRITE: begin
              if (!write_sprite_done) begin
                send_write_sprite <= 1'b1;
                mem_addr <= write_sprite_mem_addr;
                mem_transaction <= WRITE;
                if (write_sprite_w_en) begin
                  out_fifo <= write_sprite_fifo;
                end
              end else send_write_sprite <= 1'b0;
            end

            SPRITE_WRITE: begin
              if (!sprite_write_done) begin
                send_sprite_write <= 1'b1;
                sprite_req_op <= LUT_WRITE;
                sprite_req_body <= sprite_w_body;
                if (sprite_write_w_en) begin
                  out_fifo <= sprite_write_fifo;
                end
              end else send_sprite_write <= 1'b0;
            end

            SPRITE_UPDATE: begin
              if (!sprite_update_done) begin
                send_sprite_update <= 1'b1;
                sprite_req_op <= LUT_UPDATE;
                sprite_req_body <= sprite_u_body;
              end else send_sprite_update <= 1'b0;
            end

            SPRITE_DELETE: begin
              if (!sprite_delete_done) begin
                send_sprite_delete <= 1'b1;
                sprite_req_op <= LUT_DELETE;
                sprite_req_body <= sprite_d_body;
              end else send_sprite_delete <= 1'b0;
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
                  out_fifo <= error_event.code;
                  fail_w_en <= 1'b1;
                  opcode <= UNSET;
                end
              end
            end
          endcase
        end
        default:; // Error handled by spi_fsm
      endcase
    end
  end

  info info_cmd_processor (
      .clk(clk),
      .info_fifo(info_fifo),
      .out_full(out_full),
      .info_done(info_done),
      .info_w_en(info_w_en),
      .send_info(send_info),
      .error_valid(info_error_valid),
      .error_code(info_error_code)
  );

  write_cons write_cons_cmd_processor (
      .clk(clk),
      .ck_rst(reset_s),
      .send_write_cons(send_write_cons),
      .mem_data_on_wire(mem_data_on_wire),
      .mem_busy(mem_busy),
      .mem_len(write_cons_mem_len),
      .mem_addr(write_cons_mem_addr),
      .mem_in(mem_in),
      .mem_req(write_cons_mem_req),
      .in_fifo(in_fifo),
      .write_cons_done(write_cons_done),
      .write_cons_r_en(write_cons_r_en),
      .error_valid(write_cons_error_valid),
      .error_code(write_cons_error_code)
  );

  read_cons read_cons_cmd_processor (
      .clk(clk),
      .ck_rst(reset_s),
      .send_read_cons(send_read_cons),
      .mem_data_on_wire(mem_data_on_wire),
      .in_fifo(in_fifo),
      .mem_len(read_cons_mem_len),
      .mem_busy(mem_busy),
      .mem_req(read_cons_mem_req),
      .mem_addr(read_cons_mem_addr),
      .mem_out(mem_out),
      .read_cons_fifo(read_cons_fifo),
      .read_cons_done(read_cons_done),
      .read_cons_w_en(read_cons_w_en),
      .read_cons_r_en(read_cons_r_en),
      .error_valid(read_cons_error_valid),
      .error_code(read_cons_error_code)
  );

  read_box read_box_handler (
    .clk(clk),
    .ck_rst(reset_s),
    .in_fifo(in_fifo),
    .read_box_fifo(read_box_fifo),
    .read_box_done(read_box_done),
    .read_box_w_en(read_box_w_en),
    .read_box_r_en(read_box_r_en),
    .mem_len(read_box_mem_len),
    .mem_busy(mem_busy),
    .mem_data_on_wire(mem_data_on_wire),
    .mem_req(read_box_mem_req),
    .mem_out(mem_out),
    .mem_addr(read_box_mem_addr),
    .error_valid(read_box_error_valid),
    .error_code(read_box_error_code)
  );

  write_box write_box_handler (
    .clk(clk),
    .ck_rst(reset_s),
    .in_fifo(in_fifo),
    .write_box_done(write_box_done),
    .write_box_r_en(write_box_r_en),
    .mem_len(write_box_mem_len),
    .mem_req(write_box_mem_req),
    .mem_busy(mem_busy),
    .mem_data_on_wire(mem_data_on_wire),
    .mem_in(mem_in),
    .mem_addr(write_box_mem_addr),
    .error_valid(write_box_error_valid),
    .error_code(write_box_error_code)
  );

  write_sprite write_sprite_cmd_processor (
      .clk(clk),
      .ck_rst(reset_s),
      .mem_data_on_wire(mem_data_on_wire),
      .send_write_sprite(send_write_sprite),
      .mem_busy(mem_busy),
      .in_fifo(in_fifo),
      .write_sprite_fifo(write_sprite_fifo),
      .mem_len(write_sprite_mem_len),
      .mem_in(mem_in),
      .write_sprite_done(write_sprite_done),
      .write_sprite_r_en(write_sprite_r_en),
      .write_sprite_w_en(write_sprite_w_en),
      .mem_req(write_sprite_mem_req),
      .mem_addr(write_sprite_mem_addr),
      .write_sprite_valid(write_sprite_valid),
      .error_valid(write_sprite_error_valid),
      .error_code(write_sprite_error_code)
  );

  sprite_write sprite_write_cmd_processor (
      .clk(clk),
      .ck_rst(reset_s),
      .send_sprite_write(send_sprite_write),
      .sprite_write_done(sprite_write_done),
      .in_fifo(in_fifo),
      .sprite_write_r_en(sprite_write_r_en),
      .sprite_write_w_en(sprite_write_w_en),
      .sprite_write_fifo(sprite_write_fifo),
      .sprite_req_valid(sprite_w_valid),
      .sprite_available_id(sprite_available_id),
      .sprite_id_found(sprite_id_found),
      .sprite_req_body(sprite_w_body),
      .error_valid(sprite_write_error_valid),
      .error_code(sprite_write_error_code)
  );

  sprite_update sprite_update_cmd_processor (
      .clk(clk),
      .ck_rst(reset_s),
      .send_sprite_update(send_sprite_update),
      .sprite_update_done(sprite_update_done),
      .sprite_update_r_en(sprite_update_r_en),
      .in_fifo(in_fifo),
      .sprite_req_valid(sprite_u_valid),
      .sprite_req_body(sprite_u_body),
      .error_valid(sprite_update_error_valid),
      .error_code(sprite_update_error_code)
  );

  sprite_delete sprite_delete_cmd_processor (
      .clk(clk),
      .ck_rst(reset_s),
      .send_sprite_delete(send_sprite_delete),
      .sprite_delete_done(sprite_delete_done),
      .sprite_delete_r_en(sprite_delete_r_en),
      .in_fifo(in_fifo),
      .sprite_req_valid(sprite_d_valid),
      .sprite_req_body(sprite_d_body),
      .error_valid(sprite_delete_error_valid),
      .error_code(sprite_delete_error_code)
  );


endmodule
