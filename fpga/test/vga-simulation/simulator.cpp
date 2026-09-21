#include <GL/glut.h>
#include <iostream>
#include <thread>
#include <verilated.h> // defines common routines

#include "Vtb_main.h" // from Verilating "Vtb_main.v"

using namespace std;

Vtb_main *top_module; // instantiation of the model

uint64_t main_time = 0;  // current simulation time
double sc_time_stamp() { // called by $time in Verilog
  return main_time;
}

// to wait for the graphics thread to complete initialization
bool gl_setup_complete = false;

#include "../config.hpp"

const int TotalWidth = LeftPorch + ActiveWidth + RightPorch + HorizontalSync;

const int TotalHeight = TopPorch + ActiveHeight + BottomPorch + VerticalSync;

// pixels are buffered here
float graphics_buffer[ActiveWidth][ActiveHeight][3] = {};

// calculating each pixel's size in accordance to OpenGL system
// each axis in OpenGL is in the range [-1:1]
float pixel_w = 2.0 / ActiveWidth;
float pixel_h = 2.0 / ActiveHeight;

// gets called periodically to update screen
void render(void) {
  glClear(GL_COLOR_BUFFER_BIT);

  // convert pixels into OpenGL rectangles
  for (int i = 0; i < ActiveWidth; i++) {
    for (int j = 0; j < ActiveHeight; j++) {
      glColor3f(graphics_buffer[i][j][0], graphics_buffer[i][j][1],
                graphics_buffer[i][j][2]);
      glRectf(i * pixel_w - 1, -j * pixel_h + 1, (i + 1) * pixel_w - 1,
              -(j + 1) * pixel_h + 1);
    }
  }

  glFlush();
}

// timer to periodically update the screen
void glutTimer(int t) {
  glutPostRedisplay(); // re-renders the screen
  glutTimerFunc(t, glutTimer, t);
}

// handle up/down/left/right arrow keys
int keys[4] = {};
void Special_input(int key, int x, int y) {
  switch (key) {
  case GLUT_KEY_UP:
    keys[0] = 1;
    break;
  case GLUT_KEY_DOWN:
    keys[1] = 1;
    break;
  case GLUT_KEY_LEFT:
    keys[2] = 1;
    break;
  case GLUT_KEY_RIGHT:
    keys[3] = 1;
    break;
  }
}

// initiate and handle graphics
void graphics_loop(int argc, char **argv) {
  glutInit(&argc, argv);
  glutInitDisplayMode(GLUT_SINGLE);
  glutInitWindowSize(ActiveWidth, ActiveHeight);
  glutInitWindowPosition(100, 100);
  glutCreateWindow("VGA Simulator");
  glutDisplayFunc(render);
  glutSpecialFunc(Special_input);

  gl_setup_complete = true;

  // re-render every 16ms, around 60Hz
  glutTimerFunc(16, glutTimer, 16);
  glutMainLoop();
}

// tracking VGA signals
int coord_x = 0;
int coord_y = 0;
bool pre_h_sync = 0;
bool pre_v_sync = 0;

// set Verilog module inputs based on arrow key inputs
void apply_input() {
  top_module->up = keys[0];
  top_module->down = keys[1];
  top_module->left = keys[2];
  top_module->right = keys[3];

  for (int i = 0; i < 4; i++)
    keys[i] = 0;
}

// we only want the input to last for one or few clocks
void discard_input() {
  top_module->up = 0;
  top_module->down = 0;
  top_module->left = 0;
  top_module->right = 0;
}

// read VGA outputs and update graphics buffer
void sample_pixel() {
  discard_input();

<<<<<<< HEAD
    if(!top_module->h_sync && pre_h_sync){ // on negative edge of h_sync
        // re-sync horizontal counter
        coord_x = HORIZONTAL_FRONT_PORCH + ACTIVE_WIDTH + HORIZONTAL_SYNC;
        coord_y = (coord_y + 1) % TOTAL_HEIGHT;
    }

    if(!top_module->v_sync && pre_v_sync){ // on negative edge of v_sync
        // re-sync vertical counter
        coord_y = VERTICAL_FRONT_PORCH + ACTIVE_HEIGHT + VERTICAL_SYNC;
        apply_input(); // inputs are pulsed once each new frame
    }
=======
  coord_x = (coord_x + 1) % TotalWidth;

  if (!top_module->h_sync && pre_h_sync) { // on negative edge of h_sync
    // re-sync horizontal counter
    coord_x = RightPorch + ActiveWidth + HorizontalSync;
    coord_y = (coord_y + 1) % TotalHeight;
  }
>>>>>>> 73f4381 (Make new implementation of the vga circuit)

  if (!top_module->v_sync && pre_v_sync) { // on negative edge of v_sync
    // re-sync vertical counter
    coord_y = TopPorch + ActiveHeight + VerticalSync;
    apply_input(); // inputs are pulsed once each new frame
  }

  if (coord_x < ActiveWidth && coord_y < ActiveHeight) {
    uint16_t packed_color = top_module->color;
    uint8_t red = (packed_color >> 8) & 0xF;
    uint8_t green = (packed_color >> 4) & 0xF;
    uint8_t blue = packed_color & 0xF;
    graphics_buffer[coord_x][coord_y][0] = float(red) / 15.0f;
    graphics_buffer[coord_x][coord_y][1] = float(green) / 15.0f;
    graphics_buffer[coord_x][coord_y][2] = float(blue) / 15.0f;
  }

  pre_h_sync = top_module->h_sync;
  pre_v_sync = top_module->v_sync;
}

// simulate for a single clock
void tick() {
  // update simulation time
  main_time++;

  // rising edge
  top_module->clk = 1;
  top_module->eval();

  // falling edge
  top_module->clk = 0;
  top_module->eval();
}

// globally reset the model
void reset() {
  top_module->reset = 1;
  top_module->clk = 0;
  top_module->eval();
  tick();
  top_module->reset = 0;
}
void enable() {
  top_module->enable = 1;
  top_module->eval();
}

int main(int argc, char **argv) {
  // create a new thread for graphics handling
  thread thread(graphics_loop, argc, argv);
  // wait for graphics initialization to complete
  while (!gl_setup_complete)
    ;

  Verilated::commandArgs(argc, argv); // remember args

  // create the model
  top_module = new Vtb_main;

  // reset the model
  reset();

  // enable the model
  enable();

  // cycle accurate simulation loop
  while (!Verilated::gotFinish()) {
    if (top_module->pixel_pulse) {
      sample_pixel();
    }
    tick();
  }

  top_module->final();
  delete top_module;
}
