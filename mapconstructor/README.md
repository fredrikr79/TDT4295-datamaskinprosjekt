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
