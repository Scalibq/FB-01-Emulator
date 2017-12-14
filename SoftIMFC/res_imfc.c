#pragma code_seg("RESIDENT")
#pragma data_seg("RESIDENT", "CODE")

#include "resident.h"


struct config RESIDENT config;


/* I/O port access */

extern unsigned inp(unsigned port);
extern unsigned outp(unsigned port, unsigned value);
#pragma intrinsic(inp, outp)
#pragma aux inp modify nomemory;
#pragma aux outp modify nomemory;

/* Debug output */

static void writechar(char x) {
  int COM1 = 0x03F8;
  outp(COM1, x);
}

static void writeln(void) {
  writechar('\r');
  writechar('\n');
}

static void writehex(char x) {
  x = (x & 0xf) + '0';
  if (x > '9') {
    x += 'A' - '9' - 1;
  }
  writechar(x);
}

static void write2hex(char x) {
  writehex(x >> 4);
  writehex(x);
}

static void write4hex(int x) {
  write2hex(x >> 8);
  write2hex(x);
}


/* I/O virtualization */

extern unsigned emulate_imfc_io(int port, int is_write, unsigned ax);
