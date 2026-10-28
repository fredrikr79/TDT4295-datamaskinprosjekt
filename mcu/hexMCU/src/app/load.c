

#include <stdint.h>
#include <stdbool.h>
#define MAX_MEMID 1024 // each memeid can contain aboutn 1024 bytes of data
#define MAX_SPRITEID 256


// this will load sprites form the sd card then send them to the fpga, it will also keep track of the memids and spriteids



// sprite struct


typedef struct
{
    uint16_t spriteID;
    uint16_t x;
    uint16_t y;
    bool active;
} sprite_t;



// how we get data into memory can be a mix of creating figures dynamicly and loading sprites form the sd card.


// we will also have a structure with all the diffrent sprites we have on the sd card, and the expected sd card location.
// we will then for each of these try to load them from the sd card and then send them to the fpga
// after it is sent we will take the memid and make a sprtie then when we get the sprite id back we can make a new sprite_t of it
// we will have helper function to update the sprites something like sprite_update(spriteID, x, y, active)
// we also need a structure to connect s sprite_t to the underlaying sprite, so we know what the cursure sprite is forexample.
// so we load in the curuse sprite we shuld someehow be able to do sprite_update((cursure sprite id)...)

// we can also have a basic function to create a cube of a solid color forexample.
// since we decided to make full words a sprite we will also need to generate a sprite for each word we will use.
// like paused, save game, load, and the diffrent meterial names, (sand, game of life, water, and so on)
// this is why we uppdet it to have posibility for 1024 memids, since text sprites will tkae up a lot of the space.

// so if we make a sand text sprite we also need to be able to refer to it, so we can show it when selecting that element.
// text will be dynamicly generated such as we allredy are doing.
// so we can have a function that is make_sprte_text(char *t), that returns a array, that we can then use to send to the fpga to initate the sprite.
// also need to have error recovery on loading sprite to fpga making sprite id and so on, and also error recovery on when a sprite update comes back with an error.

// for animation we shuld be able to upoad all the animation frames, then we will swap them by turing on and of active,and move them using x.
// so we could have ani_next_frame(), change animation, and ofcoruse a way to move the grouped images, so ani_move_sprite() or something like that
// so we can create an animation structt that has an array of sprite ids to change between and a number of how many frames each "animation frame shows up", so if its 5 it will change the sprite evry 5 frames form the fpga, or 30 for every second
// we alos nee a ani_sprite_t witch tracks where this is located and so on.

