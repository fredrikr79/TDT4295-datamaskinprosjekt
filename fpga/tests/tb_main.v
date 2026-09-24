`timescale 1ns / 1ps

module tb_main;

    // ============================================================
    // FPGA / MCU control signals
    // ============================================================

    reg clk;
    reg ck_ss;
    reg ck_rst;

    // Free-running MCU oscillator.
    // ck_sck is the gated version seen by the DUT.
    reg sck_src;
    reg sck_enable;

    wire ck_sck;
    assign ck_sck = sck_src & sck_enable;


    // ============================================================
    // Shared octal SPI bus
    // ============================================================

    tri [7:0] octo_spi;
    tri       acknack;

    reg [7:0] mcu_oct_data;
    reg       mcu_oct_oe;

    assign octo_spi =
        mcu_oct_oe ? mcu_oct_data : 8'bz;


    // ============================================================
    // DUT outputs
    // ============================================================

    wire [3:0] led;
    wire       led0_r;
    wire       led0_g;
    wire       led0_b;
    wire       ready;

    wire       out_almost_empty;
    wire       out_empty;
    wire       in_almost_full;
    wire       in_full;


    // ============================================================
    // DUT
    // ============================================================

    main #(
        .DEBUG(0)
    ) dut (
        .clk              (clk),
        .ck_ss            (ck_ss),
        .ck_sck           (ck_sck),
        .ck_rst           (ck_rst),

        .octo_spi         (octo_spi),
        .acknack          (acknack),

        .led              (led),
        .led0_r           (led0_r),
        .led0_g           (led0_g),
        .led0_b           (led0_b),
        .ready            (ready),

        .out_almost_empty (out_almost_empty),
        .out_empty        (out_empty),
        .in_almost_full   (in_almost_full),
        .in_full          (in_full)
    );


    // ============================================================
    // FPGA clock
    //
    // 100 MHz
    // ============================================================

    initial begin
        clk = 1'b0;

        forever #5 clk = ~clk;
    end


    // ============================================================
    // MCU SCK SOURCE
    //
    // 125 MHz source clock:
    //
    // period = 8 ns
    //
    // The DUT does NOT see this clock directly.
    // ck_sck is the gated version.
    // ============================================================

    initial begin
        sck_src = 1'b0;

        forever #20 sck_src = ~sck_src;
    end


    // ============================================================
    // SPI CLOCK CONTROL
    //
    // Change the gate while the source clock is LOW so that
    // ck_sck never gets a runt pulse.
    // ============================================================

    task spi_start;
        begin
            @(negedge sck_src);

            sck_enable = 1'b1;

            $display("[%0t] SPI CLOCK ENABLED", $time);
        end
    endtask


    task spi_stop;
        begin
            sck_enable = 1'b0;
            $display("[%0t] SPI CLOCK DISABLED", $time);
        end
    endtask


    // ============================================================
    // MCU WRITE
    //
    // The octal SPI interface transfers one complete byte on
    // each rising edge of ck_sck.
    //
    // IMPORTANT:
    // The data must be stable BEFORE the rising edge.
    // ============================================================

    task mcu_write_byte;
        input [7:0] data;

        begin
            // Put data on the bus first.
            mcu_oct_data = data;
            mcu_oct_oe   = 1'b1;

            // Data is sampled by DUT on this edge.
            @(posedge ck_sck);

            @(negedge ck_sck);

            mcu_oct_oe = 1'b0;

            $display("[%0t] MCU WRITE %02h",
                     $time, data);
        end
    endtask


    // ============================================================
    // MCU READ
    //
    // MCU releases the bus.
    //
    // FPGA is expected to drive the bus during TX.
    // ============================================================

    task mcu_read_byte;
        output [7:0] data;

        begin
            mcu_oct_oe = 1'b0;

            // FPGA data should already be present before the
            // rising edge when tx_drive_s becomes active.
            @(posedge ck_sck);

            #1;

            data = octo_spi;

            $display("[%0t] MCU READ %02h",
                     $time, data);

            @(negedge ck_sck);
        end
    endtask


    // ============================================================
    // Wait for a particular READY transition
    // ============================================================

    task wait_ready;
        begin
            wait (ready === 1'b1);

            $display("[%0t] FPGA READY",
                     $time);
        end
    endtask


    // ============================================================
    // MAIN TEST
    // ============================================================

    reg [7:0] rx_byte0;
    reg [7:0] rx_byte1;
    reg [7:0] rx_byte2;
    reg [7:0] rx_byte3;
    reg [7:0] rx_byte4;

    initial begin

        // --------------------------------------------------------
        // Initial conditions
        // --------------------------------------------------------

        ck_ss        = 1'b1;
        ck_rst       = 1'b0;

        sck_enable   = 1'b0;

        mcu_oct_data = 8'hzz;
        mcu_oct_oe   = 1'b0;

        rx_byte0     = 8'h00;
        rx_byte1     = 8'h00;
        rx_byte2     = 8'h00;
        rx_byte3     = 8'h00;
        rx_byte4     = 8'h00;


        // --------------------------------------------------------
        // RESET
        //
        // SCK is stopped during reset.
        // clk continues running.
        // --------------------------------------------------------

        $display("[%0t] RESET",
                 $time);

        repeat (5)
            @(posedge clk);

        ck_rst = 1'b1;

        $display("[%0t] RESET RELEASED",
                 $time);


        // --------------------------------------------------------
        // Wait for FPGA boot
        //
        // No SPI clock is required for this.
        // --------------------------------------------------------

        wait (ready === 1'b1);

        $display("[%0t] FPGA READY FOR RX",
                 $time);


        // --------------------------------------------------------
        // START RX TRANSACTION
        //
        // MCU asserts CS first.
        // Then starts SCK.
        // --------------------------------------------------------

        ck_ss = 1'b0;

        spi_start();


        // --------------------------------------------------------
        // Send command/data
        //
        // ECHO command = upper nibble 0.
        //
        // 00, 05, 06
        // --------------------------------------------------------

        mcu_write_byte(8'hee);
        mcu_write_byte(8'h01);
        mcu_write_byte(8'h02);
        mcu_write_byte(8'h03);
        mcu_write_byte(8'h04);
        mcu_write_byte(8'h05);


        // --------------------------------------------------------
        // Stop clock before releasing CS.
        //
        // This gives the final SPI edge time to propagate into
        // the free-running clk domain.
        // --------------------------------------------------------
        sck_enable = 1'b0;

        spi_stop();
        ck_ss = 1'b1;


        $display("[%0t] RX TRANSACTION COMPLETE",
                 $time);


        // --------------------------------------------------------
        // Give the 100 MHz domain time to process the RX FIFO.
        // --------------------------------------------------------

        wait(ready === 1'b1);


        // --------------------------------------------------------
        // FPGA should eventually enter TX_IDLE / READY.
        // --------------------------------------------------------

        if (ready !== 1'b1) begin

            $display("[%0t] ERROR: FPGA did not reach TX/READY state",
                     $time);

        end
        else begin

            $display("[%0t] FPGA ready for TX",
                     $time);

        end


        // ============================================================
        // TX TRANSACTION
        // ============================================================

        ck_ss = 1'b0;
        spi_start();

        mcu_write_byte(8'hAA);

        // Let the FPGA's TX handshake develop.
        repeat (4)
            @(posedge sck_src);

        $display("[%0t] FPGA SHOULD NOW BE DRIVING TX BUS",
                $time);


        mcu_read_byte(rx_byte0);
        $display("[%0t] FIRST TX BYTE = %02h", $time, rx_byte0);
        mcu_read_byte(rx_byte1);
        $display("[%0t] SECOND TX BYTE = %02h", $time, rx_byte1);
        mcu_read_byte(rx_byte2);
        $display("[%0t] THIRD TX BYTE = %02h", $time, rx_byte2);
        mcu_read_byte(rx_byte3);
        $display("[%0t] FOURTH TX BYTE = %02h", $time, rx_byte3);
        mcu_read_byte(rx_byte4);
        $display("[%0t] FIFTH TX BYTE = %02h", $time, rx_byte4);

        // ------------------------------------------------------------
        // Now terminate the transaction.
        // ------------------------------------------------------------

        spi_stop();
        ck_ss = 1'b1;

        wait(ready === 1'b1);

        if (ready !== 1'b1)
            $display("[%0t] ERROR: FPGA did not return to READY",
                    $time);
        else
            $display("[%0t] FPGA returned to READY",
                    $time);

        // Try new command
        repeat(5)
            @(posedge sck_src);

        ck_ss = 1'b0;

        spi_start();


        // --------------------------------------------------------
        // Send command/data
        //
        // ECHO command = upper nibble 0.
        //
        // 00, 05, 06
        // --------------------------------------------------------

        mcu_write_byte(8'hee);
        mcu_write_byte(8'h01);
        mcu_write_byte(8'h02);
        mcu_write_byte(8'h03);
        mcu_write_byte(8'h04);
        mcu_write_byte(8'h05);


        // --------------------------------------------------------
        // Stop clock before releasing CS.
        //
        // This gives the final SPI edge time to propagate into
        // the free-running clk domain.
        // --------------------------------------------------------

        spi_stop();
        ck_ss = 1'b1;


        $display("[%0t] RX TRANSACTION COMPLETE",
                 $time);


        // --------------------------------------------------------
        // Give the 100 MHz domain time to process the RX FIFO.
        // --------------------------------------------------------

        wait(ready === 1'b1);


        // --------------------------------------------------------
        // FPGA should eventually enter TX_IDLE / READY.
        // --------------------------------------------------------

        if (ready !== 1'b1) begin

            $display("[%0t] ERROR: FPGA did not reach TX/READY state",
                     $time);

        end
        else begin

            $display("[%0t] FPGA ready for TX",
                     $time);

        end


        // ============================================================
        // TX TRANSACTION
        // ============================================================

        ck_ss = 1'b0;
        spi_start();

        mcu_write_byte(8'hAA);

        // Let the FPGA's TX handshake develop.
        repeat (4)
            @(posedge sck_src);

        $display("[%0t] FPGA SHOULD NOW BE DRIVING TX BUS",
                $time);


        mcu_read_byte(rx_byte0);
        $display("[%0t] FIRST TX BYTE = %02h", $time, rx_byte0);
        mcu_read_byte(rx_byte1);
        $display("[%0t] SECOND TX BYTE = %02h", $time, rx_byte1);
        mcu_read_byte(rx_byte2);
        $display("[%0t] THIRD TX BYTE = %02h", $time, rx_byte2);
        mcu_read_byte(rx_byte3);
        $display("[%0t] FOURTH TX BYTE = %02h", $time, rx_byte3);
        mcu_read_byte(rx_byte4);
        $display("[%0t] FIFTH TX BYTE = %02h", $time, rx_byte4);

        // ------------------------------------------------------------
        // Now terminate the transaction.
        // ------------------------------------------------------------

        spi_stop();
        ck_ss = 1'b1;

        wait(ready === 1'b1);

        if (ready !== 1'b1)
            $display("[%0t] ERROR: FPGA did not return to READY",
                    $time);
        else
            $display("[%0t] FPGA returned to READY",
                    $time);


        // --------------------------------------------------------
        // Finish
        // --------------------------------------------------------

        $display("[%0t] TEST COMPLETE",
                 $time);

        $finish;
    end


endmodule