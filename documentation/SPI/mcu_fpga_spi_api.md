# MCU <-> FPGA SPI API

## Boot protocol

The boot protocol is purely based on 

```
MCU waits READY
While(ECHO NOT GOOD)
    ECHO 100 bytes -> Response 100 bytes
INFO ->
    Resolution : [16 bit X resolution][16 bit Y resolution]
    size buffers : [16 bit buffer size]
    sessionID : [32 bit random ID]
Await READY
Issue first CMD
```

## Commands
|      NAME        |      PAYLOAD                            |           RETURNS                                    |  OPCODE  |  OPCODE HEX  | IMPLEMENTED |
|------------------|-----------------------------------------|------------------------------------------------------|----------|--------------|-------------|
|0.  UNSET         | Issued by FPGA                          |                                                      |  0000    |      0       |             |
|1.  ECHO          | [B1][B2] .. [BN]                        |  [B1][B2]...[BN]                                     |  0001    |      1       |yes          |
|2.  INFO          | VOID                                    |  [screenW 2B][screenH 2B][buffSize 2B][sessionID 4B] |  0010    |      2       |yes          |
|3.  WRITE_CONS    | [X][Y][N]                               |  VOID                                                |  0011    |      3       |yes          |
|4.  READ_CONS     | [X][Y][N]                               |  [B1][B2]...[BN]                                     |  0100    |      4       |yes          |
|5.  WRITE_BOX     | [X1][Y1][w][h]                          |  VOID                                                |  0101    |      5       |yes          |
|6.  READ_BOX      | [X1][Y1][w][h]                          |  [B1][B2]...[BN]                                     |  0110    |      6       |yes          |
|7.  WRITE_SPRITE  | [B1][B2] .. [BN]                        |  [memID]                                             |  0111    |      7       |yes          |
|8.  SPRITE_WRITE  | [memID][active][X 2B][Y 2B][W 2B][H 2B] |  [spriteID]                                          |  1000    |      8       |yes          |
|9.  SPRITE_UPDATE | [spriteID][memID][active][X 2B][Y 2B]   |  VOID                                                |  1001    |      9       |yes          |
|10. SPRITE_DELETE | [spriteID]                              |  VOID                                                |  1010    |      A       |yes          |
|11. START_SIM     | VOID                                    |  VOID                                                |  1011    |      B       |yes          |
|12. STOP_SIM      | VOID                                    |  VOID                                                |  1100    |      C       |yes          |
|13. FAILED        | Issued by FPGA                          |  [errorCode]                                         |  1111    |      F       |yes          |

!NB Bytes labeled with the suffix XB signals a multi-byte parameter. E.g X 2B in sprite_write are two bytes constituting a 16-bit X-coordinate.

A Description of each command, and its purpose / effect is described below.

### ECHO

Takes a sequence of bytes and returns them the the MCU in order they we're sent.

The FPGA may flicker its signals during startup. Especially, the READY signal may flicker. A 100-byte ECHO request is issued in a polling manner during startup to let the MCU decide if the FPGA has booted and stablized.


### INFO

This is a READ_ONLY command that returns essential FPGA parameters to the MCU. 

The current list of parameters are:

Screen dimensions,
SPI input buffer size
Session ID

The Screen dimensions dictates the parameters for the MCU's hud layer implementation.
The SPI input buffer size decides the maximum command size the MCU can issue without data loss.
The session ID is used to identify intermediate powerloss that may cause the FPGA's internal memory to be invalid.

### WRITE / READ CONS

Issues a memory request for contigious data regions in memory.

The provided XY coordiante is a grid-based location that is translated into a memory address on the FPGA.
Thus the top-leftmost pixel of the screen may be identified using the XY coordinate <0,0>.

The N parameter or length parameter, tells us how many bytes we want to write / read.

Please note that the FPGA's memory modules is a shared pseudo static DDR memory. This places a few limitations on the SPI modules capabilites when issuing a read/write request:

1. The memory is shared with the VGA / simulation module. The SPI modules is only allowed to issue memory request when the simulation module yields it to the SPI module. This occurs when the module is done with rendering both the simulation and HUD layer for this frame. (or any in-between sections of the frame). This means that a read/write may not be issued until the next frame. The simulation module may not interupt an ongoing SPI transaction.

2. The memory is pseudo static. This means that the memory must be released from an ongoing transaction every 86 transferred bytes or so (at 50MHZ). The SPI module can handle larger requests by implicitly by chunking the payload into multiple transactions.

### WRITE / READ BOX

Issues a memory request for data defined by a rectangle in grid space.

The box is defined using an top-left anchoring XY coordinate, a width and a height. the anchor is included in the width / height.
The same limitations of the FPGA's memory module applies here. See WRITE/READ Cons for details.

```
E.g XY = (0,0) W = 2 H = 2 => 4 pixels (0,0), (0,1), (1,0), (1,1)
```

### WRITE SPRITE

Issues a memory write request for uploading the pixel data for a sprite to a designated memory location. The Sprite memory is a long contigious memory placed after the simulation grid memory. 

Under the hood this works quite similarly to `WRITE_CONS` with the extra functionality that it keeps track of allocated ID's and memory usage and issues these ID's upon a write sprite request. Its up to the MCU to read an remember these ID's as they are necessary to link a sprite instance created through `SPRITE_WRITE` to the correct image data.

The base pointers of each sprite in memory is kept in an on-chip LUT. This enables reuse of the same stored sprites across many sprite instances.

### SPRITE WRITE

Instantiates a sprite instance at position `(X,Y)` that renders the image `memID` of dimensions `W * H` if `active` is high.
A sprite instance is a rendering entity that may render some sprite in some dimensions, at some location. 

SPRITE WRITE returns a sprite ID, an unique identifier used to modify or delete sprite instances at a later point in time. The ID is issued based on the lowest, available ID. ID allocations are tracked via a sepperate bitmap defined in `hud.sv`. 

### SPRITE UPDATE

Updates sprite instance `spriteID`. The height and width parameters aren't modifyable. This eases the time an update takes to execute.

Modifyable parameters are:
- memID
- active
- X coordinate
- Y coordinate

The update updates the FPGA's internal LUT for sprite instances. It does not allow for updates on sprites uploaded to memory. These are regarded as static, READ_ONLY assets.

### SPRITE DELETE

Deletes the sprite instance `spriteID` from the FPGA's internal LUT for sprite instances.

Deleting in this regard means setting every single parameter to 0. The memID 0 is unallocated in the sprite memory LUT, so no action will be acted upon such an entry.

Deleting a sprite instance releases its spriteID for aquisition during a later `SPRITE WRITE` request.

### START / STOP SIM

Drives the active high `sim_running` signal to determine if the simulation should be running or freezed. Initially `sim_running` is set to low.

## Failing commands

Failing commands will drive the ACK line low. When the MCU recieves a READY signal from the FPGA, it may consult the ACK line regarding weather the command failed or not. The FPGA will automatically flush the I/O FIFO's and recover to a READY state with no command in the pipeline. If a memory transaction command fails the FPGA makes no guarantee of the state of the memory. However any corrupted memory writes may simply be corrected by a subsequent write to the same memory region. Likewise, START/STOP SIM may simply be reissued, and SPRITE_X commands can be corrected by deleting the attempted entry to ensure its cleared, then attempting a new write. 


