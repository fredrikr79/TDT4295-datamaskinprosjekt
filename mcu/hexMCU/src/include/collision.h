#include "fpga.h"

bool tile_has_collision(uint8_t id);

typedef struct
{
    fpga_box_t box;
    uint8_t rel_x;
    uint8_t rel_y;
    uint16_t length;
} velocity_box_t;

velocity_box_t create_velocity_box(
    float pos_x, float pos_y,
    uint8_t size_x, uint8_t size_y
);

bool perform_collision_detection(
    float *pos_x, float *pos_y,
    float *velocity_x, float *velocity_y,
    uint8_t size_x, uint8_t size_y,
    velocity_box_t velocity_box, uint8_t *box
);
