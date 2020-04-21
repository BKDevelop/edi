#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/* defines */

#define EDI_VERSION "0.0.1"
#define CTRL_KEY(k) ((k)&0x1f)

enum editor_keys {
  ARROW_UP = 1000,
  ARROW_DOWN = 1001,
  ARROW_RIGHT = 1002,
  ARROW_LEFT = 1003
};
/* global data */

struct editor_config {
  int cursor_x, cursor_y;
  int screen_rows, screen_cols;

  struct termios original_terminal_state;
};

struct editor_config EDITOR;

/*  prototypes */
struct append_buffer;

int read_keypress();
void append_buffer_append();

/* terminal configuration */

void reposition_cursor_at(struct append_buffer *ab, int x, int y) {
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "\x1b[%d;%dH", y, x);
  append_buffer_append(ab, buffer, strlen(buffer)); // move cursor to top left
}

void reposition_cursor(struct append_buffer *ab) {
  reposition_cursor_at(ab, 1, 1); // move cursor to top left
}

void clear_screen(struct append_buffer *ab) {
  append_buffer_append(ab, "\x1b[2J", 4); // clear
  reposition_cursor(ab);
}

void hide_cursor(struct append_buffer *ab) {
  append_buffer_append(ab, "\x1b[?25l", 6);
}

void show_cursor(struct append_buffer *ab) {
  append_buffer_append(ab, "\x1b[?25h", 6);
}

void clear_screen_for_quit() {
  write(STDOUT_FILENO, "\x1b[2J", 4); // clear
  write(STDOUT_FILENO, "\x1b[H", 3);  // move cursor to top left
}

void die(const char *s) {
  write(STDOUT_FILENO, "\x1b[2J", 4); // clear
  write(STDOUT_FILENO, "\x1b[H", 3);  // move cursor to top left

  perror(s);
  exit(EXIT_FAILURE);
}

void disable_raw_mode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &EDITOR.original_terminal_state) ==
      -1) {
    die("tcsetattr");
  }
}

void enable_raw_mode() {
  if (tcgetattr(STDIN_FILENO, &EDITOR.original_terminal_state) == -1)
    die("tcgetattr");
  atexit(disable_raw_mode);

  struct termios raw = EDITOR.original_terminal_state;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_iflag &= ~(ICRNL | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr");
}

int get_cursor_position(int *rows, int *cols) {
  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
    return -1;

  char buffer[32];
  unsigned int i = 0;
  while (i < sizeof(buffer) - 1) {
    if (read(STDIN_FILENO, &buffer[i], 1) != 1)
      break;
    if (buffer[i] == 'R')
      break;
    i++;
  }
  buffer[i] = '\0';

  if (buffer[0] != '\x1b' || buffer[1] != '[')
    return -1;
  if (sscanf(&buffer[2], "%d;%d", rows, cols) != 2)
    return -1;

  return 0;
}

int get_window_size(int *rows, int *cols) {
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
      return -1;
    return get_cursor_position(rows, cols);
  } else {
    *rows = ws.ws_row;
    *cols = ws.ws_col;
    return 0;
  }
}

/*  append buffer */

struct append_buffer {
  char *b;
  int len;
};

#define ABUF_INIT                                                              \
  { NULL, 0 }

void append_buffer_append(struct append_buffer *ab, const char *append_s,
                          int append_len) {
  char *new = realloc(ab->b, ab->len + append_len);

  if (new == NULL)
    return;
  memcpy(&new[ab->len], append_s, append_len);
  ab->b = new;
  ab->len += append_len;
}

void append_buffer_free(struct append_buffer *ab) { free(ab->b); }

/* input */

void move_cursor(int key_pressed) {
  switch (key_pressed) {
  case ARROW_LEFT:
    if (EDITOR.cursor_x > 0)
      EDITOR.cursor_x--;
    break;

  case ARROW_DOWN:
    if (EDITOR.cursor_y < EDITOR.screen_rows - 1)
      EDITOR.cursor_y++;
    break;

  case ARROW_UP:
    if (EDITOR.cursor_y > 0)
      EDITOR.cursor_y--;
    break;

  case ARROW_RIGHT:
    if (EDITOR.cursor_x < EDITOR.screen_cols - 1)
      EDITOR.cursor_x++;
    break;

  default:
    break;
  }
}

int read_keypress() {
  int read_return;
  char c;
  while ((read_return = read(STDIN_FILENO, &c, 1)) != 1) {
    if (read_return == -1 && errno != EAGAIN)
      die("read");
  }

  // hanlde escape sequences
  char esc = '\x1b';

  if (c == esc) {
    char sequence[3];
    if ((read_return = read(STDIN_FILENO, &sequence[0], 1)) != 1)
      return esc;
    if ((read_return = read(STDIN_FILENO, &sequence[1], 1)) != 1)
      return esc;

    if (sequence[0] != '[')
      return esc;

    switch (sequence[1]) {
    // remap ArrowKeys for movement
    case 'A':
      return ARROW_UP;
    case 'B':
      return ARROW_DOWN;
    case 'C':
      return ARROW_RIGHT;
    case 'D':
      return ARROW_LEFT;
    default:
      return esc;
    }

  } else {
    return c;
  }
}

void process_keypress() {
  int key_pressed = read_keypress();

  switch (key_pressed) {
  case CTRL_KEY('q'):
    clear_screen_for_quit();
    exit(EXIT_SUCCESS);
  // movement keys
  case ARROW_UP:
  case ARROW_DOWN:
  case ARROW_RIGHT:
  case ARROW_LEFT:
    move_cursor(key_pressed);
    break;
  }
}

/* output */

void write_buffer(struct append_buffer *ab) {
  write(STDOUT_FILENO, ab->b, ab->len);
  append_buffer_free(ab);
}

void draw_welcome_message(struct append_buffer *ab) {
  char welcome_string[80];
  int welcome_length =
      snprintf(welcome_string, sizeof(welcome_string),
               "Edi - a small text editor -- Version: %s", EDI_VERSION);

  if (welcome_length > EDITOR.screen_cols)
    welcome_length = EDITOR.screen_cols;
  int padding = (EDITOR.screen_cols - welcome_length) / 2;
  if (padding) {
    append_buffer_append(ab, "~", 1);
    padding--;
  }
  while (padding) {
    append_buffer_append(ab, " ", 1);
    padding--;
  }

  append_buffer_append(ab, welcome_string, welcome_length);
}

void draw_rows(struct append_buffer *ab) {
  for (int y = 0; y < EDITOR.screen_rows; y++) {
    if (y == EDITOR.screen_rows / 3) {
      draw_welcome_message(ab);
    } else {
      append_buffer_append(ab, "~", 1); // add ~ to left hand side
    }

    append_buffer_append(ab, "\x1b[K", 3); // clear line
    if (y < EDITOR.screen_rows - 1) {
      append_buffer_append(ab, "\r\n", 2);
    }
  }
}

void refresh_screen() {
  struct append_buffer ab = ABUF_INIT;
  hide_cursor(&ab);
  draw_rows(&ab);
  reposition_cursor_at(&ab, EDITOR.cursor_x, EDITOR.cursor_y);
  show_cursor(&ab);
  write_buffer(&ab);
}

/* init */

void init_editor() {
  EDITOR.cursor_x = 0;
  EDITOR.cursor_y = 0;

  if (get_window_size(&EDITOR.screen_rows, &EDITOR.screen_cols) == -1)
    die("get_window_size");
}

int main() {
  enable_raw_mode();
  init_editor();

  while (true) {
    refresh_screen();
    process_keypress();
  }
  disable_raw_mode();
  return EXIT_SUCCESS;
}
