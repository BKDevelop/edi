#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

/* defines */

#define EDI_VERSION "0.0.1"
#define EDI_TAB_STOP 8
#define CTRL_KEY(k) ((k)&0x1f)

enum editor_keys {
  ARROW_UP = 1000, // integer enum in c does auto increment
  ARROW_DOWN,
  ARROW_RIGHT,
  ARROW_LEFT,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN
};
/* global data */

typedef struct editor_row {
  int size;
  int render_size;
  char *chars;
  char *render;
} editor_row;

struct editor_config {
  int cursor_x, cursor_y;
  int render_cursor_x;
  int screen_rows, screen_cols;
  int row_offset;
  int col_offset;

  int number_of_rows;
  editor_row *row;

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

/* row operations */
int cursor_x_to_render_x(editor_row* row, int cursor_x){
  int render_cursor_x = 0;
  for(int i = 0; i < cursor_x; i++){
    if(row -> chars[i] == '\t') {
      render_cursor_x += (EDI_TAB_STOP - 1) - (render_cursor_x % EDI_TAB_STOP);
    }
    render_cursor_x++;
  }
  return render_cursor_x;
}

void update_render_row(editor_row *row) {
  int tabs = 0;
  for (int j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t')
      tabs++;
  }

  free(row->render);
  /* row->render = malloc(row->size + 1 + (tabs * (EDI_TAB_STOP - 1))); */
  row->render = malloc((row -> size) + (tabs * (EDI_TAB_STOP -1)) + 1);
  int idx = 0;

  for (int j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t') {
      row->render[idx++] = ' ';
      while (idx % EDI_TAB_STOP != 0)
        row->render[idx++] = ' ';
    } else {
      row->render[idx++] = row->chars[j];
    }
  }

  row->render[idx] = '\0';
  row->render_size = idx;
}

void append_editor_row(char *line, ssize_t line_length) {
  EDITOR.row =
      realloc(EDITOR.row, sizeof(editor_row) * (EDITOR.number_of_rows + 1));

  int at = EDITOR.number_of_rows;
  EDITOR.row[at].size = line_length;
  EDITOR.row[at].chars = malloc(line_length + 1);
  memcpy(EDITOR.row[at].chars, line, line_length);
  EDITOR.row[at].chars[line_length] = '\0';
  EDITOR.number_of_rows++;

  EDITOR.row[at].render_size = 0;
  EDITOR.row[at].render = NULL;
  update_render_row(&EDITOR.row[at]);
}
/* file i/o */

void open_file(char *filename) {
  FILE *file = fopen(filename, "r");
  if (!file)
    die("open_file");

  char *line = NULL;
  size_t line_cap = 0;
  ssize_t line_length;

  while ((line_length = getline(&line, &line_cap, file)) != -1) {
    while (line_length > 0 &&
           (line[line_length - 1] == '\r' || line[line_length - 1] == '\n'))
      line_length--;
    append_editor_row(line, line_length);
  }

  free(line);
  fclose(file);
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
  editor_row *row = (EDITOR.cursor_y >= EDITOR.number_of_rows)
                        ? NULL
                        : &EDITOR.row[EDITOR.cursor_y];
  switch (key_pressed) {
  case ARROW_LEFT:
    if (EDITOR.cursor_x > 0) {
      EDITOR.cursor_x--;
    } else if (EDITOR.cursor_y > 0) {
      EDITOR.cursor_y--;
      EDITOR.cursor_x = EDITOR.row[EDITOR.cursor_y].size;
    }
    break;

  case ARROW_DOWN:
    if (EDITOR.cursor_y < EDITOR.number_of_rows)
      EDITOR.cursor_y++;
    break;

  case ARROW_UP:
    if (EDITOR.cursor_y > 0)
      EDITOR.cursor_y--;
    break;

  case ARROW_RIGHT:
    if (row && (EDITOR.cursor_x < (row->size))) {
      EDITOR.cursor_x++;
    } else if (EDITOR.cursor_y < EDITOR.number_of_rows) {
      EDITOR.cursor_y++;
      EDITOR.cursor_x = 0;
    }
    break;

  default:
    break;
  }

  // snap cursor end of line if moved to shorter line
  row = (EDITOR.cursor_y >= EDITOR.number_of_rows)
            ? NULL
            : &EDITOR.row[EDITOR.cursor_y];
  int row_length = row ? row->size : 0;
  if (EDITOR.cursor_x > row_length)
    EDITOR.cursor_x = row_length;
}

int read_keypress() {
  int read_return;
  char c;
  while ((read_return = read(STDIN_FILENO, &c, 1)) != 1) {
    if (read_return == -1 && errno != EAGAIN)
      die("read");
  }

  // handle escape sequences
  char esc = '\x1b';

  if (c == esc) {
    char sequence[3];
    if ((read_return = read(STDIN_FILENO, &sequence[0], 1)) != 1)
      return esc;
    if ((read_return = read(STDIN_FILENO, &sequence[1], 1)) != 1)
      return esc;

    if (sequence[0] == '[') {
      if (sequence[1] >= '0' && sequence[1] <= '9') {
        if ((read_return = read(STDIN_FILENO, &sequence[2], 1)) != 1)
          return esc;
        if (sequence[2] == '~') {
          switch (sequence[1]) {
          case '1':
            return HOME_KEY;
          case '3':
            return DEL_KEY;
          case '4':
            return END_KEY;
          case '5':
            return PAGE_UP;
          case '6':
            return PAGE_DOWN;
          }
        }
      } else {
        switch (sequence[1]) {
        case 'A':
          return ARROW_UP;
        case 'B':
          return ARROW_DOWN;
        case 'C':
          return ARROW_RIGHT;
        case 'D':
          return ARROW_LEFT;
        case 'H':
          return HOME_KEY;
        case 'F':
          return END_KEY;
        default:
          return esc;
        }
      }
    } else if (sequence[0] == '0') {
      switch (sequence[1]) {
      case 'H':
        return HOME_KEY;
      case 'F':
        return END_KEY;
      }
    }

    return esc;
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
  case PAGE_UP:
  case PAGE_DOWN: {
    int times = EDITOR.screen_rows;
    while (times--)
      move_cursor(key_pressed == PAGE_UP ? ARROW_UP : ARROW_DOWN);
  } break;
  case HOME_KEY:
    EDITOR.cursor_x = 0;
    break;
  case END_KEY:
    EDITOR.cursor_x = EDITOR.screen_cols - 1;
    break;
  }
}

/* output */

void scroll() {
  EDITOR.render_cursor_x = 0;
  if (EDITOR.cursor_y < EDITOR.number_of_rows) {
    EDITOR.render_cursor_x = cursor_x_to_render_x(&EDITOR.row[EDITOR.cursor_y], EDITOR.cursor_x);
  }
  if (EDITOR.cursor_y < EDITOR.row_offset) {
    EDITOR.row_offset = EDITOR.cursor_y;
  }
  if (EDITOR.cursor_y >= EDITOR.row_offset + EDITOR.screen_rows) {
    EDITOR.row_offset = EDITOR.cursor_y - EDITOR.screen_rows + 1;
  }
  if (EDITOR.render_cursor_x < EDITOR.col_offset) {
    EDITOR.col_offset = EDITOR.render_cursor_x;
  }
  if (EDITOR.render_cursor_x >= EDITOR.col_offset + EDITOR.screen_cols) {
    EDITOR.col_offset = EDITOR.render_cursor_x - EDITOR.screen_cols + 1;
  }
}

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
    int file_row = y + EDITOR.row_offset;
    if (file_row >= EDITOR.number_of_rows) {
      if (EDITOR.number_of_rows == 0 && y == EDITOR.screen_rows / 3) {
        draw_welcome_message(ab);
      } else {
        append_buffer_append(ab, "~", 1); // add ~ to left hand side
      }
    } else {
      int len = EDITOR.row[file_row].render_size - EDITOR.col_offset;
      if (len < 0)
        len = 0;
      if (len > EDITOR.screen_cols)
        len = EDITOR.screen_cols;
      append_buffer_append(ab, &EDITOR.row[file_row].render[EDITOR.col_offset],
                           len);
    }

    append_buffer_append(ab, "\x1b[K", 3); // clear line on the right of cursor
    if (y < EDITOR.screen_rows - 1) {
      append_buffer_append(ab, "\r\n", 2);
    }
  }
}

void refresh_screen() {
  scroll();

  struct append_buffer ab = ABUF_INIT;
  reposition_cursor(&ab);
  hide_cursor(&ab);
  draw_rows(&ab);
  reposition_cursor_at(&ab, (EDITOR.render_cursor_x - EDITOR.col_offset) + 1,
                       (EDITOR.cursor_y - EDITOR.row_offset) + 1);
  show_cursor(&ab);
  write_buffer(&ab);
}

/* init */

void init_editor() {
  EDITOR.cursor_x = 0;
  EDITOR.cursor_y = 0;
  EDITOR.render_cursor_x = 0;
  EDITOR.row_offset = 0;
  EDITOR.col_offset = 0;
  EDITOR.number_of_rows = 0;
  EDITOR.row = NULL;

  if (get_window_size(&EDITOR.screen_rows, &EDITOR.screen_cols) == -1)
    die("get_window_size");
}

int main(int argc, char *argv[]) {
  enable_raw_mode();
  init_editor();
  if (argc >= 2) {
    open_file(argv[1]);
  }

  while (true) {
    refresh_screen();
    process_keypress();
  }
  disable_raw_mode();
  return EXIT_SUCCESS;
}
