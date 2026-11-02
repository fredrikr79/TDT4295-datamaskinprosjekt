#pragma once

void game_init(void);
void game_poll(void);
void game_change_room(int to_x, int to_y);

void set_next_velocity_box(int to_x, int to_y);

void save_finish(void);
void load_finish(void);