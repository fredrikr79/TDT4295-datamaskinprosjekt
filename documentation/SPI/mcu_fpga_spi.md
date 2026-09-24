# MCU <-> FPGA SPI protocol description.

For HEX's MCU <-> FPGA communication channel, we're utilizing a custom OCTO-SPI driver paired with dedicated hardware on the STM32. Since the dedicated hardware places fixed limitations on the timings and sequences of transmissions, the driver contains a few gotchas and nice-to-know details.

## Interface
The FPGA exposes these pins to the MCU in order to drive the OCTO-SPI protocol:

- `ck_sck` : The MCU's clock, used to time sampling of RX and issuing TX writes onto the SPI bus.
- `ck_ss` : A low driven signal used by the MCU to tell the FPGA an active transaction is underway. The MCU will be writing data (RX) or reading data  (TX) during this time.
- `Ready` : A high driven signal telling the MCU that the FPGA is waiting for the MCU to kick off the next step of the protocol. 
- `ACK` : Currently unused, will be used to acknowledge an understood command post RX.
- `octo_spi [7:0]` : 8 bit shared bus used for  

### Hidden components:

- `in_fifo` : A `1024` byte FWFT FIFO buffer used to store all data associated with the processing of a single command.
- `out_fifo` : A `1024` byte Standard FIFO buffer used to store all data associated with the response of a single command.

## The basic protocol

To illustrate the basic protocol, we will trace a simple echo command in its full round-trip from boot time til finish.

The general protocol is: `BOOT -> READY -> RX -> DECODE -> BUSY -> TX_IDLE -> TX_SEND -> READY`

The different steps are described in more detail below.

### Boot state
During boot, the FPGA will await the initialization of the `in_fifo` and `out_fifo`. Once the ports are establised it drives the `Ready` signal to `HIGH`, signaling the MCU that it is ready for a command to be issued.

### Ready state
The state used by the FPGA to signal that no command is currently in the pipeline. A requirement for this state is that both `in_fifo` and `out_fifo` are empty but initialized.

### RX state
Once a singular byte is placed onto the `in_fifo` the FPGA considers the first byte to be a command byte. In order to keep a consise separation between RX, processing and TX, all data associated with a command is communicated prior to command execution. During RX, each byte presented by the MCU on octo_spi is captured into in_fifo on an MCU-generated clock event. The MCU keeps ck_ss asserted while transmitting the complete command and releases it when the command has been fully transmitted. It is assumed that all data passed in this window is payload for the issued command, any data remaining on the in_fifo after processing and TX will be dropped to reset the FPGA for the next command. 

When RX ends, the first byte in in_fifo is interpreted as the command byte and passed to the decode stage.

### DECODE state

The FPGA dispatches different subroutines based on the value of the first byte transfered during RX.
For instance, in order to do an ECHO command, one would send the following hexadecimal bytes`

```
OPCODE -> EE | XX XX ... XX 
``` 

where | denotes the start of the payload.

The Decode state acts as an intermediate state between `RX` and `BUSY` and gives the FPGA a single cycle to store and evaluate the validity of the issued `OPCODE`. This could technically be done in `BUSY`, but is kept to keep the pipeline steps explicit.

### BUSY state

!NOTE this interface is a work in progress and is subject to change.

In this state the FPGA processes the command by delegating the work to an appropriate submodule. There is no universal condition for when "BUSY" is done, so it is up to every submodule to issue a transition to the TX stage (or ready if no response should be issued). 

### TX_IDLE state

Since the MCU relies on dedicated hardware, we are forced to wait a few cycles once the MCU issues a read. The delay is caused by the dedicated hardware requiring a sanity byte to be sent, and additional time for synchronizing the FPGA to take ownership of the data bus.

This state simply wait for the sanity byte and starts a 3-step synchronized handshake for taking control over the SPI bus. The TX handshake introduces a fixed delay of approximately 5 clk cycles before `tx_armed` is observed by the MCU. The handshake completion is denoted by setting `tx_armed` to `HIGH`.

### TX_SEND state

This is arguably the most involved state of the entire protocol. 

In order to securely and reliably send data from the FPGA, the TX protocol is, as the RX protocol, event driven. In other words, while ck_ss is asserted, each rising edge of ck_sck generates a TX event. The event is transferred into the FPGA's clk domain through a three-stage synchronizer. Once synchronized, the event causes the next value to be removed from `out_fifo` and loaded into `tx_out`. 

While TX is armed and ck_ss is asserted, tx_out drives the shared data bus. Each synchronized TX event advances tx_out to the next response byte.

The `tx_out` register is preloaded with the first `out_fifo` value prior to the first `ck_sck` signal in order to ensure that every event actually causes a value to be driven onto the data bus.

Once `ck_ss` is released, the FPGA treats the TX transaction as complete. Any remaining data in `in_fifo` is considered stale and is discarded before the next command transaction.


### FAILED state

!NOTE This interface is a WIP and will change

A general state that any part of the system can issue a transition to. It causes the FPGA to write `FF` onto `out_fifo` and enter the `TX_IDLE` state. Aka a shortcut bypassing `RX` and `Busy`. Typically used for illegal `OPCODE`s or data.



