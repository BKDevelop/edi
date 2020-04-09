#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>

/* defines */

#define CTRL_KEY(k) ((k) & 0x1f)

/* global data */

struct termios original_terminal_state;

/* terminal configuration */
void reposition_cursor(){
  write(STDOUT_FILENO, "\x1b[H", 4); //move cursor to top left
}

void clear_screen(){
  write(STDOUT_FILENO, "\x1b[2J", 4); //clear
  reposition_cursor();
}

void die(const char* s){
  clear_screen();

  perror(s);
  exit(EXIT_FAILURE);
}

void disable_raw_mode() {
  if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_terminal_state) == -1) {
    die("tcsetattr");
  }
}

void enable_raw_mode() {
  if(tcgetattr(STDIN_FILENO, &original_terminal_state) == -1) die("tcgetattr");
  atexit(disable_raw_mode);

  struct termios raw = original_terminal_state;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_iflag &= ~(ICRNL | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;
  if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

/* input */

char read_keypress(){
  int read_return;
  char c;
  while((read_return = read(STDIN_FILENO, &c, 1)) != 1){
    if(read_return == -1 && errno != EAGAIN) die("read");
  }
  return c;
}
void process_keypress(){
  char c = read_keypress();

  switch(c) {
    case CTRL_KEY('q'):
      clear_screen();
      exit(EXIT_SUCCESS);
  }
}

/* output */

void draw_rows(){
  for(int y = 0; y < 24; y++){
    write(STDOUT_FILENO, "~\r\n", 3); // add ~ to left hand side
  }
}

void refresh_screen(){
  clear_screen();
  draw_rows();
  reposition_cursor();
}


/* init */

int main() {
  enable_raw_mode();
  while(true){
    refresh_screen();
    process_keypress();
  }
  disable_raw_mode();
  return EXIT_SUCCESS;
}

