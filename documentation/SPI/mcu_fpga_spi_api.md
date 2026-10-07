## Boot protocol

The boot protocol is purely based on 

MCU waits READY
health_check -> ECHO 100 bytes -> resend until correct
MCU issues boot / init cmd response:
    Resolution -> [16 bit X resolution][16 bit Y resolution]
    size buffers -> [16 bit buffer size]
    sessionID -> [32 bit random ID]
Await READY -> done

## Missing I/O

SYNC signal,
FAILED / ACKNACK flag 3 bits,

## Commands
|      NAME       |      PAYLOAD     |           RETURNS           |  OPCODE  |  OPCODE HEX  | IMPLEMENTED |
|-----------------|------------------|-----------------------------|----------|--------------|-------------|
|1. ECHO          | [B1][B2] .. [BN] |  [B1][B2]...[BN]            |  0001    |  01          |yes          |
|2. INFO          | VOID             |  [B1][B2]...[BN]            |  0001    |  01          |yes          |
|3. Send_line     | [X][Y][N]        |  VOID                       |  0010    |  02          |no           |
|4. Read_line     | [X][Y][N]        |  [tileID][Meta1][Meta2]...  |  0011    |  03          |no           |
|5. Send_HUD_box  | [X1][Y1][X2][Y2] |  VOID                       |  0100    |  04          |no           |
|6. Read_HUD_box  | [X1][Y1][X2][Y2] |  [tileID][Meta1][Meta2]...  |  0101    |  05          |no           |
|7. Send_SIM_box  | [X1][Y1][X2][Y2] |  VOID                       |  0110    |  06          |no           |
|8. Read_SIM_box  | [X1][Y1][X2][Y2] |  [tileID][Meta1][Meta2]...  |  0111    |  07          |no           |
|9. Stop_SIM      | [XXXXXXX0]       |  VOID                       |  1000    |  08          |no           |
|10. Start_SIM    | [XXXXXXX1]       |  VOID                       |  1001    |  09          |no           |

