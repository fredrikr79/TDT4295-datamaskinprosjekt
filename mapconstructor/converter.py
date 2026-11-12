from itertools import batched, repeat, starmap
from json import load
from os import chdir, mkdir
from os.path import dirname, exists, join
from sys import exit
from typing import TypedDict

from PIL import Image


color = tuple[int, int, int]
portal = list[dict[str, list[int]]]
class Room(TypedDict):
    path: str
    portals: portal
config = dict[str, dict[str, Room]]


def load_config() -> config:
    with open('config.json', 'r') as f:
        data: config = load(f)

    for version in data.values():
        for room in version.values():
            room['path'] = join(*room['path'].split('/'))
    
    return data

def load_particles() -> dict[color, int]:
    with open('particles.json', 'r') as f:
        particle_data: dict[str, list[int | color]] = load(f)

    # Verify unique IDs and colours in particle configuration
    errors = 0
    colors: dict[color, str] = {}
    for name, (_, _, particle_color) in particle_data.items():
        particle_color = tuple(particle_color) # type: ignore
        if particle_color in colors:
            print(f'{name} has a duplicate of {colors[particle_color]}\'s colour ({particle_color})!')
            errors += 1
            
    # Abort script if errors found in particle configuration
    if errors:
        print(f'\nFound {errors} in particle configuration! Aborting!')
        exit()
        
    # Construct colour to ID-lookup table
    reverse_lookup: dict[color, int] = dict(starmap(
        lambda a, b, c: (tuple(c), (a << 4) + (b & 15)),
        particle_data.values())
    )
    
    return reverse_lookup

def approximate_color(
    pixel: color,
    color_to_id: dict[color, int],
    approximations: dict[color, int] = {}
) -> int:
    if pixel in approximations:
        return approximations[pixel]
    
    color_similarity: dict[color, int] = {}
    for particle_color in color_to_id.keys():
        color_similarity[particle_color] = (
            (pixel[0] - particle_color[0])**2
            + (pixel[1] - particle_color[1])**2
            + (pixel[2] - particle_color[2])**2
        )
    best_match: color = sorted(
        color_similarity.items(),
        key=lambda a: a[1]
    )[0][0]

    approximations[best_match] = color_to_id[best_match]
    return color_to_id[best_match]

def load_id_file(
    path: str,
    reverse_lookup: dict[color, int],
    approximations: dict[color, int],
    pos: str = 'UNKNOWN'
) -> tuple[int, ...] | None:
    if exists(path):
        img = Image.open(path, mode='r')
    else:
        print(f'Could not find {path}! Skipping!')
        return None

    img_id_data: list[int] = []
    for y in range(img.size[1]):
        for x in range(img.size[0]):
            pixel = tuple(img.getpixel((x, y)))  # type: ignore !UNSAFE!
            if pixel in reverse_lookup:
                img_id_data.append(reverse_lookup[pixel])  # type: ignore
            elif approximations is not None:
                img_id_data.append(approximate_color(
                    pixel, reverse_lookup, approximations  # type: ignore
                ))
            else:
                print(
                    f'Pixel {(x, y)} with color {pixel} in {path} for {pos} \
                    does not match any particles, and approximations \
                    are disabled! Skipping!'
                )
                return None

    img.close()
    return tuple(img_id_data)

def int_or_none(x) -> bool:
    return isinstance(x, int) or x is None

def int_to_bin(x: int | None | list[int], length: int) -> str:
    if isinstance(x, int):
        return ('1' if x < 0 else '0') + bin(abs(x))[2:].rjust(length, '0')[1:]
    elif x is None:
        return '1'*length
    elif isinstance(x, list) and all(map(int_or_none, x)):
        return ''.join(map(int_to_bin, x, repeat(length)))
    else:
        raise TypeError(f'Expected x of type Int or List[Int], got {type(x)}')

def bin_to_bytes(binary: str) -> bytes:
    return bytes(int(''.join(x), 2) for x in batched(binary, 8))

def generate_bytes(id: int, consecutive: int) -> bytes:
    variable_number = bin(consecutive)[2:]
    byte_length = 1
    if consecutive > 4294967295:
        print(f'{consecutive} is too lagre!')
        exit()
    elif consecutive > 134217727:  # 27
        byte_length = 5
    elif consecutive > 1048575:  # 20
        byte_length = 4
    elif consecutive > 8191:  # 13
        byte_length = 3
    elif consecutive > 63:  # 6
        byte_length = 2
    variable_number_header = '1'*byte_length + '0'
    variable_number = (
        variable_number_header
        + '0'*((-len(variable_number_header) - len(variable_number)) % 8)
        + variable_number
    )
    return bin_to_bytes(variable_number + int_to_bin(id, 8))

def convert_file_to_bytes(portals: portal, id_map: tuple[int, ...]) -> bytearray:
    byte_data = bytearray()
    
    # Add portals to level data
    portal_len = min(len(portals), 8)
    portal_bin = int_to_bin(portal_len, 8)
    for prt in portals:
        portal_bin += int_to_bin(prt['from'], 16)
        portal_bin += int_to_bin(prt['to'], 16)
        portal_bin += int_to_bin(prt['room'], 8)
        portal_bin += int_to_bin(prt['pos'], 16)
    byte_data.extend(bin_to_bytes(portal_bin))

    # Add IDs to level data
    prev = id_map[0]
    consecutive = 1
    for id in id_map[1:]:
        if id == prev:
            consecutive += 1

        if id != prev or consecutive == 4294967295:
            byte_data.extend(generate_bytes(prev, consecutive))
            prev = id
            consecutive = 1
    
    if consecutive != 0:
        byte_data.extend(generate_bytes(prev, consecutive))

    return byte_data

def convert_file_to_raw_bytes(id_map: tuple[int, ...]) -> bytearray:
    byte_data = bytearray()
    byte_data.extend(bytes(id_map))
    return byte_data

def save_file(path: str, byte_data: bytearray) -> None:
    with open(path, 'wb') as f:
        f.write(byte_data)

def make_if_missing(path: str) -> None:
    if not exists(path):
        mkdir(path)

def main() -> None:
    chdir(dirname(__file__))
    
    # Prepare data
    reverse_lookup = load_particles()
    approximations: dict[color, int] = {}
    loaded_config = load_config()
    
    make_if_missing('data')
    make_if_missing('fpga')

    # Make a version of rooms (e.g. full, mini)
    for version_name, version in loaded_config.items():
        print(f'=== Generating version "{version_name}" ===')
        
        make_if_missing(join('data', version_name))
        version_rom = join('data', version_name, 'rom')
        make_if_missing(version_rom)
        make_if_missing(join('data', version_name, 'save'))
        fpga_version = join('fpga', version_name)
        make_if_missing(fpga_version)

        # Convert room
        for pos, room in version.items():
            print(f'  Generating room "{pos}"')
            
            # Load image as list of IDs
            id_map = load_id_file(
                join('src', room['path']),
                reverse_lookup,
                approximations
            )
            if id_map is None:
                continue
            
            # Generate level data
            byte_data = convert_file_to_bytes(room['portals'], id_map)
            save_file(
                join(version_rom, pos + '.dat'),
                byte_data
            )
            
            # Generate FPGA display data
            id_bytes = convert_file_to_raw_bytes(id_map)
            save_file(
                join(fpga_version, pos + '.dat'),
                id_bytes
            )
    print(approximations)

if __name__ == '__main__':
    main()
