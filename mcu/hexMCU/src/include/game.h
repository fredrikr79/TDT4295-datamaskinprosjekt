#pragma once

void game_init(void);
void game_poll(void);
void game_change_room(int to_x, int to_y);

void save_finish(void);
void load_finish(void);

#include <stdbool.h>
bool game_loading(void);
void game_set_loading(bool on);


void game_reset_fpga(void);
void game_reset_mcu(void);
void game_reset_all(void);