#include <unistd.h>
#include <termios.h>
#include <stdlib.h>

struct termios original_terminal_state;

void disable_raw_mode() {
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_terminal_state);
}

void enable_raw_mode() {
  tcgetattr(STDIN_FILENO, &original_terminal_state);
  atexit(disable_raw_mode);

  struct termios raw = original_terminal_state;
  raw.c_lflag &= ~(ECHO);
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}



int main() {
  enable_raw_mode();
  char c;
  while(read(STDIN_FILENO, &c, 1) == 1 && c != 'q');
  disable_raw_mode();
  return 0;
}

