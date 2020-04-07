#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

struct termios original_terminal_state;

void disable_raw_mode() {
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_terminal_state);
}

void enable_raw_mode() {
  tcgetattr(STDIN_FILENO, &original_terminal_state);
  atexit(disable_raw_mode);

  struct termios raw = original_terminal_state;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_iflag &= ~(ICRNL | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}



int main() {
  enable_raw_mode();
  char c = '\0';
  for(;;){
    read(STDIN_FILENO, &c, 1);
    if(c == 'q') break;

    if(iscntrl(c)){
      printf("%d\r\n", c);
    } else {
      printf("%d ('%c')\r\n", c, c);
    }
  }
  disable_raw_mode();
  return EXIT_SUCCESS;
}

