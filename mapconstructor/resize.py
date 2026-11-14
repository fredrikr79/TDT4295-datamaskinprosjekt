from os import chdir, mkdir, listdir
from os.path import dirname, exists, join
from sys import exit

from PIL import Image


def main() -> None:
    chdir(dirname(__file__))
    
    # Check if there are any source versions and print them out
    pass
    
    # Retrieve the name of the version to scale from
    while True:
        src_version: str = input('Enter version to rescale\n> ')
        if exists(join('src', src_version)):
            break
        print(f'"{src_version}" is not an available version!\n')
    
    # Retrieve the name of the new version
    while True:
        new_version: str = input('\nEnter name for the new version\n> ')
        if not exists(join('src', new_version)):
            break
        print(f'A version with the name "{new_version}" already exists!')
    
    # Retrieve the size of the new version
    while True:
        new_size_raw: str = input('\nEnter the size (e.g. "160, 90")\n> ')
        try:
            new_size: tuple[int, ...] = tuple(map(int, map(str.strip, new_size_raw.split(','))))
            if len(new_size) == 2:
                break
        except ValueError:
            pass
        print(f'"{new_size_raw}" doesn\'t match the required format!')
    
    # Create new version
    src_version_path = join('src', src_version)
    new_version_path = join('src', new_version)
    mkdir(new_version_path)
    for path in listdir(src_version_path):
        if not path.endswith('.png'):
            continue
        
        src_img = Image.open(join(src_version_path, path))
        new_img = src_img.resize(new_size, Image.Resampling.NEAREST)
        new_img.save(join(new_version_path, path))

if __name__ == '__main__':
    main()
