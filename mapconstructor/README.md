# Map Constructor Development Setup

This project uses Python and Pillow to generate level data and raw bytes for the FPGA team.

The package Pillow needs to be installed in order to run the map constructor. This can be done through pip or your systems package manager depending on your system.

## Project Structure

```
mapconstructor
├── config.json             # Configuration of rooms
├── particles.json          # Configuration of interpreting colours in images as IDs
├── converter.py            # Script to convert images from src/*version*/
├── length.py               # Script to see when dynamic size numbers in the level data increase in byte-length
├── src                     # Source of level data
│   ├── full                # Version of world map with source files
│   │   ├── start.png       # Image to be used for creating the level data for a room
│   │   ├── start.kra       # A Krita-file to create room images
│   │   └── ...             # More room images and other painting-program files
│   └── ...                 # More versions of source files
├── data                    # Level data
│   ├── full                # Version of world map with level data
│   │   ├── rom             # Base level data unaffected by saving
│   │   │   ├── 0,0.dat     # Level data for a room
│   │   │   └── ...         # More rooms
│   │   └── save            # Empty folder for save data on the SD-card
│   └── ...                 # More versions of level data
└── fpga                    # Raw bytes for the FPGA team
    ├── full                # Version of world map with raw bytes for the FPGA team
    │   ├── 0,0.dat         # Raw data in bytes of a room's IDs
    │   └── ...             # More raw data
    └── ...                 # More versions of FPGA data
```

## Requirements

- Python
- Pillow (PIL)

## Level data format

The data format for the rooms are binary files starting with portal data and is followed up by tile data. 

Rooms start in the top left corner, and are read left to right, top to bottom.

All numbers in the format uses some form of big endian. 

### Portal section

This first section details the information of up to 8 portals. 

The first byte says how many portals there are. 

Then comes the portals. They are made up of a from, a to, a room, and a pos. 

The different sections are made up as follows:

    1. from: uint16, uint16
    2. to: uint16, uint16
    3. room: int8, int8
    4. pos: uint16, uint16

"from" and "to" indicated where in the room two of the portal's edges which make up it's area are located. 

"room" indicates which room the portal goes to. These ints are in one's complement.

"pos" indicates where in the target room the player will be placed.

The following is the configuration of two portals:
```
{"from": [0, 263], "to": [8, 279], "room": [-2, 0], "pos": [620, 262]},
{"from": [632, 143], "to": [640, 159], "room": [0, 0], "pos": [4, 143]}
```
The following is an expanded view of the configuration above turned into bits:
```
    {
        00000010
    }
    {
        ([00000000 00000000][00000001 00000111])
        ([00000000 00001000][00000001 00010111])
        ([10000010][00000000])
        ([00000010 01101100][00000001 00000110])
    }
    {
        ([00000010 01111000][00000000 10001111])
        ([00000010 10000000][00000000 10011111])
        ([00000000][00000000])
        ([00000000 00000100][00000000 10001111])
    }
```
The following is the above bits collapsed:
```
00000010 00000000 00000000 00000001 00000111 00000000 00001000 00000001 00010111 10000010 00000000 00000010 01101100 00000001 00000110 00000010 01111000 00000000 10001111 00000010 10000000 00000000 10011111 00000000 00000000 00000000 00000100 00000000 10001111
```

### Tile section

This section is made up of chunks specifying how many consecutive tiles of the same ID is present. 

A chunk starts with a header of n 1-bits followed by a 0-bit to indicate that there are n - 1 more bytes than the first that are a part of indicating how many consecutive tiles the chunk is made up of. Setting the first n 1-bits as zeros and combining the n bytes gives the number of consecutive bytes. For the sake of not having to simulate numbers with more bytes than a system can represent natively, the larget number should be 4294967295. This means that n can be 5, but in that case, the first byte will only be zeros once the first n bits are discarded, so the first byte can be omitted, giving a uint32 number at max. 

The chunk ends with a byte with the ID of the tiles in the chunk. The ID is a 4 bit number at the start of the byte. The byte for ID 5 would be 01010000. 

Examples:

    A chunk of 15 tiles of id 5 is represented by 10011001 01010000

    A chunk of 349 tiles of id 1 is represented by 11000001 01011101 00010000

    A chunk of 1787549140 tiles of id 13 is represented by 11110000 01101010 10001011 11010101 11010100 11010000

Encoding example:

    The chunk to encode has 9575 tiles of ID 3.

    9575 in binary is 10010101100111. That is 14 bits, which only leaves 2 bits left. But the header would need 3 bits (110) to represent those 2 bytes, so another byte is added to make room for the header. This means the header needs to be made up of 4 bits (1110) since there is now another byte. The space between 1110 and 10010101100111 will be filled with zeros to make full bytes. The result is the 3 bytes 11100000 00100101 01100111.

    3 in binary is 11, and padding it to be a 4-bit ID results in 0011. Then the last 0000 gets added at the back resulting in the byte 00110000. 

    The final chunk is 11100000 00100101 01100111 00110000.

Decoding example:

    These are the first 10 bytes of a level data file after the portal section: 11110001 01010101 11000110 01001110 01100000 10001101 01010000 1110110 00110000 10100100

    The first section of 1-bits is made up of 4 bits. Therefore, the bytes 11110001, 01010101, 11000110 and 01001110 are a part of the number of tiles, and 01100000 will be the part indicating the ID. After that comes a different chunk. 

    After setting the first 4 bits to zeros, we get the bytes 00000001 01010101 11000110 01001110 for the number of tiles. This number can be used directly without having to cut off the first byte since it is made up of less than 5 bytes. This gives 22398542 tiles. On a little endian system, the bytes must be used in the opposite order (01001110 11000110 01010101 00000001). 

    To get the ID, we take the first 4 bits from 01100000 to get 0110. 0110 is binary for 6, giving us the ID 6.

## Raw data for the FPGA

The data format of raw room data for the FPGA are binary files where every tile is it's own byte, where the ID makes up the first 4 bits of the byte. ID 5 would give the byte 01010000. Rooms start in the top left corner, and are read left to right, top to bottom.
