//
//  main.c
//  Vi_Project
//  Mini Proyecto
//  TC2025 Grupo 01 Programación Avanzada
//
//  Creado por Arturo Baez, Diego Montaño y Monica Nava el 08/09/20.
//  Ultima versión: 20/09/2020
//  
//  Characteristics that the project have
//  Open file 
//  Disable CTRL+Z and CTRL+C to quit program
//  Switch between text editing mode and command mode
//  :q to exit. Ask user if they want to save changes before quitting
//  :wq quit automatically saving changes
//  :n go to line number
//  - Number of occurrences in text :f text
//  Navigate to text :s text
//
//  Cygwin64 is required to run the code in Windows.

// includes
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <stdarg.h>
#include <time.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

// defines
#define CTRL_KEY(k) ((k) & 0x1f)
#define TAB_STOP 8

//Enum to make easier the manipulation of data
enum editorKey{
    BACKSPACE = 127,
    UP = 1000,
    DOWN,
    RIGHT,
    LEFT,
    q_escape,
    line_num,
    q_auto,
    DEL_KEY,
    COUNT_TYPE,
    FIND_TYPE
};

// data
typedef struct erow{
    int size;
    int rsize;
    char *chars;
    char *render;
}erow;

// Struct of the Editor
struct editorConfig{
    int cx, cy;
    int rowoff;
    int coloff;
    int screenrows;
    int screencols;
    int numrows;
    erow *row;
    char *filename;
    char *cmndMsg[80];
    char *textMsg[80];
    time_t cmndMsg_time;
    time_t textMsg_time;
    int dirty;
    struct termios origin;
};

struct editorConfig Editor;

//prototypes
void editorSetTextMessage(const char *fmt,...);
void editorRefreshScreen(void);
char *editorPrompt(char *prompt);
void editorUpdateRow(erow *row);
void editorInsertNewline(void);



// Terminal functions to handle errors //
void die(const char *s){
    write(STDOUT_FILENO,"\x1b[2j", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    perror(s);
    exit(1);
}

void disableRawMode(){
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &Editor.origin) == -1){
        die("tcsetattr");
    }
}

void enableRawMode(){
    if(tcgetattr(STDIN_FILENO, &Editor.origin) == -1) die("tcgetattr");
    atexit(disableRawMode);
    struct termios raw = Editor.origin;
    raw.c_oflag &= ~(OPOST);
    raw.c_iflag &= ~(IXON);
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 10;
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

// Functions to help the input of commands
int editorReadKey(){
    int nread;
    char c;
    //printf("ANTES\r\n");
    while((nread = read(STDIN_FILENO, &c, 1)) != 1){ // o while???
        //printf("INNNNNN");
        if(nread == -1 && errno != EAGAIN) die("read");
    }
    if(c == '\x1b'){
        char seq[3];
        if(read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if(read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
        
        if(seq[0] == '['){
            switch(seq[1]){
                case 'A': return UP;
                case 'B': return DOWN;
                case 'C': return RIGHT;
                case 'D': return LEFT;
            }
        }
        return '\x1b';
    }
    if (c == ':'){
        char seq[3];
        if(read(STDIN_FILENO, &seq[0], 1) != 1) return ':';
        //si es .n
        switch(seq[0]){
            case 'n': return line_num;
            case 'q': return q_escape;
            case 'f': return COUNT_TYPE;
            case 's': return FIND_TYPE;
            case 'w':
                if(read(STDIN_FILENO, &seq[1], 1) != 1) return ':';
                    if (seq[1] == 'q'){
                        return q_auto;
                    }else
                        return ':';
                }
    }
    return c;
}

// Function to get the size of the Window Terminal
int getWindowsSize(int *rows, int *cols){
    struct winsize ws;
    
    if(ioctl(STDOUT_FILENO, TIOCGWINSZ,&ws) == -1 || ws.ws_col == 0){
        return -1;
    }else{
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

// Row operations, helped to manipulate and priniting the file
int editorRowRxToCx(erow *row, int rx) {
  int cur_rx = 0;
  int cx;
  for (cx = 0; cx < row->size; cx++) {
    if (row->chars[cx] == '\t')
      cur_rx += (TAB_STOP - 1) - (cur_rx % TAB_STOP);
    cur_rx++;
    if (cur_rx > rx) return cx;
  }
  return cx;
}

void editorAppendRow(int at,char *s,size_t len){
    if (at < 0 || at > Editor.numrows){
        return;
    }
    Editor.row = realloc(Editor.row, sizeof(erow) * (Editor.numrows + 1));
    memmove(&Editor.row[at + 1], &Editor.row[at], sizeof(erow) * (Editor.numrows - at));
    
    Editor.row[at].size = len;
    Editor.row[at].chars = malloc(len + 1);
    memcpy(Editor.row[at].chars, s, len);
    Editor.row[at].chars[len] = '\0';
    Editor.row[at].rsize = 0;
    Editor.row[at].render = NULL;
    editorUpdateRow(&Editor.row[at]);
    Editor.numrows++;
}

// Function to write in the file
void editorUpdateRow(erow *row) {
  int tabs = 0;
  int j;
  for (j = 0; j < row->size; j++)
    if (row->chars[j] == '\t') tabs++;
  free(row->render);
  row->render = malloc(row->size + tabs*7 + 1);
  int idx = 0;
  for (j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t') {
      row->render[idx++] = ' ';
      while (idx % 8 != 0) row->render[idx++] = ' ';
    } else {
      row->render[idx++] = row->chars[j];
    }
  }
  row->render[idx] = '\0';
  row->rsize = idx;
}

void editorFreeRow(erow *row) {
  free(row->render);
  free(row->chars);
}

//Function to delete in the file
void editorDelRow(int at) {
  if (at < 0 || at >= Editor.numrows){
    return;
  }
  editorFreeRow(&Editor.row[at]);
  memmove(&Editor.row[at], &Editor.row[at + 1], sizeof(erow) * (Editor.numrows - at - 1));
  Editor.numrows--;
  Editor.dirty++;
}

void editorRowInsertChar(erow *row, int at, int c) {
  if (at < 0 || at > row->size) at = row->size;
  row->chars = realloc(row->chars, row->size + 2);
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
  row->size++;
  row->chars[at] = c;
  editorUpdateRow(row);
}

void editorRowAppendString(erow *row, char *s, size_t len) {
  row->chars = realloc(row->chars, row->size + len + 1);
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  editorUpdateRow(row);
  Editor.dirty++;
}

// Delete helper
void editorRowDelChar(erow *row, int at) {
  if (at < 0 || at >= row->size) return;
  memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
  row->size--;
  editorUpdateRow(row);
  Editor.dirty++;
}

// Editor operations, to have control of where the user can move the pointer to write in the file
void editorInsertChar(int c) {
  if (Editor.cy == Editor.numrows) {
    editorAppendRow(Editor.numrows, "", 0);
  }
  editorRowInsertChar(&Editor.row[Editor.cy], Editor.cx, c);
  Editor.cx++;
}

void editorInsertNewline() {
  if (Editor.cx == 0) {
    editorAppendRow(Editor.cy, "", 0);
  } else {
    erow *row = &Editor.row[Editor.cy];
    editorAppendRow(Editor.cy + 1, &row->chars[Editor.cx], row->size - Editor.cx);
    row = &Editor.row[Editor.cy];
    row->size = Editor.cx;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
  }
  Editor.cy++;
  Editor.cx = 0;
}

void editorDelChar() {
  if (Editor.cy == Editor.numrows){
    return;
  }
  if (Editor.cx == 0 && Editor.cy == 0){
    return;
  }
  erow *row = &Editor.row[Editor.cy];
  if (Editor.cx > 0) {
    editorRowDelChar(row, Editor.cx - 1);
    Editor.cx--;
  } else {
    Editor.cx = Editor.row[Editor.cy - 1].size;
    editorRowAppendString(&Editor.row[Editor.cy - 1], row->chars, row->size);
    editorDelRow(Editor.cy);
    Editor.cy--;
  }
}

// File I/O handler
char *editorRowsToString(int *buflen) {
  int totlen = 0;
  int j;
  for (j = 0; j < Editor.numrows; j++)
    totlen += Editor.row[j].size + 1;
  *buflen = totlen;
  char *buf = malloc(totlen);
  char *p = buf;
  for (j = 0; j < Editor.numrows; j++) {
    memcpy(p, Editor.row[j].chars, Editor.row[j].size);
    p += Editor.row[j].size;
    *p = '\n';
    p++;
  }
  return buf;
}

// Function that opens the file given and print it in console
void editorOpen(char *fn){
    free(Editor.filename);
    Editor.filename = strdup(fn);
    FILE *ptrFile = fopen(fn,"r");
    if(!ptrFile) die("fopen");
    
    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    //linelen = getline(&line,&linecap,ptrFile);
    while((linelen = getline(&line,&linecap,ptrFile)) != -1){
        while(linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) linelen--;
        editorAppendRow(Editor.numrows,line,linelen);
    }
    free(line);
    fclose(ptrFile);
}

// Function that help to save the changes maded to the file
void editorSave() {
    if (Editor.filename == NULL) return;
    int len;
    char *buf = editorRowsToString(&len);
    int fd = open(Editor.filename, O_RDWR | O_CREAT, 0644);
    if (fd != -1) {
    if (ftruncate(fd, len) != -1) {
      if (write(fd, buf, len) == len) {
        close(fd);
        free(buf);
        editorSetTextMessage("%d bytes written to disk", len);
        return;
      }
    }
    close(fd);
  }
  free(buf);
  editorSetTextMessage("Can't save! I/O error: %s", strerror(errno));
}

// Function for the command of Find
void editorFind(int type){
    char *query = editorPrompt("Buscar: %s (ESC para cancelar)");
    if(query == NULL) return;
    
    int i,count = 0;
    
    for(i = 0; i < Editor.numrows; i++){
        erow *row = &Editor.row[i];
        char *match = strstr(row->render, query);
        if(match){
            if(type == FIND_TYPE){
                Editor.cy = i;
                Editor.cx = editorRowRxToCx(row, match - row->render) ;
                Editor.rowoff = Editor.numrows;
                break;
            }else if(type == COUNT_TYPE){
                count++;
            }
        }
    }
    if(type == COUNT_TYPE){
        editorSetTextMessage("Numero de ocurrencias: %d ",count);
    }
    free(query);
}

// Append buffer
struct abuf{
    char *b;
    int len;
};

#define ABUF_INIT {NULL,0}

void appendBuffer(struct abuf *ab, char *s, int len){
    char *new = realloc(ab->b,ab->len+len);
    if(new == NULL) return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void freeBuffer(struct abuf *ab){
    free(ab->b);
}

// Output of the file
void editorScroll(){
    if(Editor.cy < Editor.rowoff){
        Editor.rowoff = Editor.cy;
    }
    if(Editor.cy >= Editor.rowoff + Editor.screenrows){
        Editor.rowoff = Editor.cy - Editor.screenrows + 1;
    }
    if(Editor.cx <Editor.coloff){
        Editor.coloff = Editor.cx;
    }
    if(Editor.cx >= Editor.coloff + Editor.screencols){
        Editor.coloff = Editor.cx - Editor.screencols + 1;
    }
}

void editorDrawRows(struct abuf *ab){
    int y;
    for(y = 0;y < Editor.screenrows-2;y++){
        int filerow = y + Editor.rowoff;
        if(filerow >= Editor.numrows){
            if(Editor.numrows == 0 && y == Editor.screenrows / 3){
                char title[50];
                int titleLen = snprintf(title, sizeof(title),"Mini-Proyecto");
                if(titleLen > Editor.screencols) titleLen = Editor.screencols;
                int padding = (Editor.screencols - titleLen) / 2;
                if (padding) {
                  appendBuffer(ab, "~", 1);
                  padding--;
                }
                while (padding--) appendBuffer(ab, " ", 1);
                appendBuffer(ab, title, titleLen);
                appendBuffer(ab, "\x1b[K\r\n", 5);
            }else{
                appendBuffer(ab, "~\x1b[K\r\n", 6);
            }
        } else{
            int len = Editor.row[filerow].size - Editor.coloff;
            if(len < 0) len = 0;
            if(len > Editor.screencols) len = Editor.screencols;
            appendBuffer(ab, &Editor.row[filerow].chars[Editor.coloff], len);
            appendBuffer(ab, "\x1b[K\r\n", 5);
        }
    }
    //appendBuffer(ab, "~", 1);
    //appendBuffer(ab, "\x1b[K", 3);
}

void editorDrawCommandBar(struct abuf *ab){
    appendBuffer(ab,"\x1b[7m",4);
    char status[80], rstatus[80];
    int cmdlen = strlen(Editor.cmndMsg);
    int len = snprintf(status, sizeof(status),"Linea cmnds: ");
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", Editor.cy + 1, Editor.numrows);
    if(len > Editor.screencols) len = Editor.screencols;
    if(cmdlen > Editor.screencols - len) cmdlen = Editor.screencols - len;
    appendBuffer(ab, status, len);
    
    while(len < Editor.screencols){
        if(Editor.screencols - len == rlen){
            appendBuffer(ab, rstatus, rlen);
            break;
        }else{
            appendBuffer(ab," ",1);
            len++;
        }
    }
    //appendBuffer(ab,"\x1b[m",3);
    appendBuffer(ab,"\r\n",2);
}

void editorDrawTextBar(struct abuf *ab){
    appendBuffer(ab,"\x1b[K", 3);
    int msglen = strlen(Editor.textMsg);
    if (msglen > Editor.screencols) msglen = Editor.screencols;
    if (msglen && time(NULL) - Editor.textMsg_time < 5){
        appendBuffer(ab, Editor.textMsg, msglen);
    } else {
        msglen = 0;
    }
    while(msglen < Editor.screencols){
        appendBuffer(ab, " ", 1);
        msglen++;
    }
    appendBuffer(ab,"\x1b[m",3);
}

// This function helps the printing of the user input in real time
void editorRefreshScreen(){
    editorScroll();
    
    struct abuf ab = ABUF_INIT;
    appendBuffer(&ab, "\x1b[H", 3);
    
    editorDrawRows(&ab);
    editorDrawCommandBar(&ab);
    editorDrawTextBar(&ab);
    
    char buf[32];
    snprintf(buf,sizeof(buf),"\x1b[%d;%dH", (Editor.cy - Editor.rowoff) + 1, (Editor.cx - Editor.coloff) + 1);
    appendBuffer(&ab, buf, strlen(buf));
    //appendBuffer(&ab, "\x1b[H", 3);
    write(STDIN_FILENO, ab.b, ab.len);
    freeBuffer(&ab);
}

//Go to line command
void gotoLineNo(int key){
    int nread;
    char seq[3];
    while((nread = read(STDIN_FILENO, &seq, 1)) != 1){
        if(nread == -1 && errno != EAGAIN) die("read");
    }
    if(seq[0] > 47 || seq[0] < 58 ){
        Editor.cy = seq[0]-48;
        Editor.cx= 0;
    }
    
}

// Function for the :q command
void salirQ(int key){
    char *query = editorPrompt("Quieres guardar antes de salir? Y/N %s");
    if(query == NULL) return;
    
    char seq = *query;
    if(seq == 'Y' || seq == 'y' ){
        editorSave();
    }
}

// Function that shows the messages given by the commands
void editorSetTextMessage(const char *fmt,...){
    va_list ap;
    va_start(ap,fmt);
    vsnprintf(Editor.textMsg, sizeof(Editor.textMsg), fmt, ap);
    va_end(ap);
    Editor.textMsg_time = time(NULL);
}

void editorSetCmndMessage(const char *fmt,...){
    va_list ap;
    va_start(ap,fmt);
    vsnprintf(Editor.cmndMsg, sizeof(Editor.cmndMsg), fmt, ap);
    va_end(ap);
    Editor.cmndMsg_time = time(NULL);
}

// input
char *editorPrompt(char *prompt){
    size_t bufsize = 128;
    char *buf = malloc(bufsize);
    
    size_t buflen = 0;
    buf[0] = '\0';
    
    while(1){
        editorSetTextMessage(prompt,buf);
        editorRefreshScreen();
        
        int c = editorReadKey();
        if(c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE){
            if(buflen != 0) buf[--buflen] = '\0';
        }else if(c == '\x1b'){
            editorSetTextMessage("");
            free(buf);
            return NULL;
        }else if(c == '\n'){
                if(buflen != 0){
                    editorSetTextMessage(" ");
                    return buf;
        }
        }else if(!iscntrl(c) && c < 128){
            if(buflen == bufsize - 1){
                bufsize *= 2;
                buf = realloc(buf,bufsize);
            }
            buf[buflen++] = c;
            buf[buflen] = '\0';
        }
    }
}

void editorMoveCursor(int key){
    erow *row = (Editor.cy >= Editor.numrows) ? NULL : &Editor.row[Editor.cy];
    switch(key){
        case LEFT:
            if(Editor.cx != 0){
                Editor.cx--;
            }
            break;
        case RIGHT:
            if(row && Editor.cx < row->size){
                Editor.cx++;
            }
            break;
        case UP:
            if(Editor.cy != 0){
                Editor.cy--;
            }
            break;
        case DOWN:
            if(Editor.cy < Editor.numrows){
                Editor.cy++;
            }
            break;
    }
    row = (Editor.cy >= Editor.numrows) ? NULL : &Editor.row[Editor.cy];
    int rowlen = row ? row->size : 0;
    if(Editor.cx > rowlen){
        Editor.cx = rowlen;
    }
}

// Function thats proccess all the commands given by the user and special keys
void editorProcessKeypress(){
    int c = editorReadKey();
    switch(c){
        case '\r':
            editorInsertNewline();
            break;
        case CTRL_KEY('q'):
            write(STDOUT_FILENO,"\x1b[2j",4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        case CTRL_KEY('s'):
            editorSave();
            break;
        case FIND_TYPE:
            editorFind(FIND_TYPE);
            break;
        case COUNT_TYPE:
            editorFind(COUNT_TYPE);
            break;
        case BACKSPACE:
        case CTRL_KEY('h'):
        case DEL_KEY:
            if (c == DEL_KEY) {
                editorMoveCursor(RIGHT);
            }
            editorDelChar();
            break;
        case UP:
        case DOWN:
        case LEFT:
        case RIGHT:
            editorMoveCursor(c);
            break;
        case line_num:
            gotoLineNo(c);
            break;
        case CTRL_KEY('l'):
        case q_escape:
            salirQ(c);
            write(STDOUT_FILENO,"\x1b[2j",4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        case q_auto:
            editorSave();
            write(STDOUT_FILENO,"\x1b[2j",4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        case '\x1b':
            break;
        default:
        editorInsertChar(c);
        break;
    }
}

// init of the Editor struct
void initEditor(){
    Editor.cx = 0;
    Editor.cy = 0;
    Editor.numrows = 0;
    Editor.rowoff = 0;
    Editor.coloff = 0;
    Editor.filename = NULL;
    Editor.cmndMsg[0] = '\0';
    Editor.textMsg[0] = '\0';
    Editor.cmndMsg_time = 0;
    Editor.textMsg_time = 0;
    Editor.dirty = 0;
    
    if(getWindowsSize(&Editor.screenrows, &Editor.screencols) == -1) die("getWindowsSize");
}

// Main that call all functions
int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();
    if(argc >= 2){
        editorOpen(argv[1]);
    }
    editorSetTextMessage("HELP: Ctrl-Q = quit | Ctrl-S = save | Ctrl-F = find");

    //Infinity loop that waits for user input/commands
    while (1){
        editorRefreshScreen();
        editorProcessKeypress();
    }
    
    return 0;
}
