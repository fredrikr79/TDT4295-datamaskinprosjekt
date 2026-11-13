from itertools import batched
from math import ceil


header = '10'
number = ''
for i in range(1, 29):
    number += '1'
    if ceil((len(header) + len(number)) / 8) > len(header) - 1:
        header = '1' + header
    byte_str = ' '.join(map(''.join, batched(header + '0'*((-len(header) - len(number)) % 8) + number, 8)))
    print(f'{len(header) - 1} bytes at {i} bit number up to {int(number, 2)} | {byte_str}')
