#include <conio.h>
#include <dos.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "resident.h"


#define STR(x) #x
#define XSTR(x) STR(x)

int amis_install_check(char amis_id, struct amis_info *info);
#pragma aux amis_install_check =                \
  "xchg al, ah"                                 \
  "int 0x2D"                                    \
  "cbw"                                         \
  "mov word ptr [si], di"                       \
  "mov word ptr 2[si], dx"                      \
  "mov word ptr 4[si], cx"                      \
  parm [ax] [si]                                \
  value [ax]                                    \
  modify [bx cx dx di]

struct amis_info {
  char __far *signature;
  union version version;
};

int ioctl_read(int handle, char __near *buf, int nbytes);
#pragma aux ioctl_read =                        \
  "mov ax, 0x4402"                              \
  "int 0x21"                                    \
  "sbb dx, dx"                                  \
  parm [bx] [dx] [cx]                           \
  value [dx]                                    \
  modify [ax bx cx dx si di]

static int emm386_get_version(int handle, char __near *version_buf) {
  /* Interrupt list: 214402SF02 GET MEMORY MANAGER VERSION */
  version_buf[0] = 2;
  return ioctl_read(handle, version_buf, 2);
}

int emm386_virtualize_io(int first, int last, int count, void __far *table, int size, int *out_handle);
/* Interrupt list: 2F4A15BX0000 INSTALL I/O VIRTUALIZATION HANDLER */
#pragma aux emm386_virtualize_io =              \
  ".386"                                        \
  "push ax"                                     \
  "push ds"                                     \
  "mov ax, es"                                  \
  "mov ds, ax"                                  \
  "mov ax, 0x4A15"                              \
  "shl edx, 16"                                 \
  "mov dx, bx"                                  \
  "xor bx, bx"                                  \
  "stc"                                         \
  "int 0x2F"                                    \
  "pop ds"                                      \
  "pop bx"                                      \
  "mov [bx], ax"                                \
  "sbb ax, ax"                                  \
  parm [bx] [dx] [cx] [es si] [di] [ax]         \
  value [ax]                                    \
  modify [ax bx cx dx si di]


int emm386_unvirtualize_io(int handle);
#pragma aux emm386_unvirtualize_io =            \
  "mov ax, 0x4A15"                              \
  "mov bx, 1"                                   \
  "stc"                                         \
  "int 0x2F"                                    \
  "sbb ax, ax"                                  \
  parm [si]                                     \
  value [ax]                                    \
  modify [ax bx cx dx si di]


int qemm_get_qpi_entry_point(int handle, void __far **qpi_entry);
#pragma aux qemm_get_qpi_entry_point =          \
  "mov ax, 0x4402"                              \
  "mov cx, 4"                                   \
  "int 0x21"                                    \
  "sbb ax, ax"                                  \
  parm [bx] [dx]                                \
  modify [bx cx dx si di]


int qpi_get_version(void __far **qpi_entry);
#pragma aux qpi_get_version =                   \
  "mov ah, 3"                                   \
  "call dword ptr [si]"                         \
  parm [si]                                     \
  value [bx]                                    \
  modify [ax]


void __far *qpi_get_io_callback(void __far **qpi_entry);
#pragma aux qpi_get_io_callback =               \
  "mov ax, 0x1A06"                              \
  "call dword ptr [si]"                         \
  parm [si]                                     \
  value [es di]                                 \
  modify [ax]


void qpi_set_io_callback(void __far **qpi_entry, void __far *callback);
#pragma aux qpi_set_io_callback =               \
  "mov ax, 0x1A07"                              \
  "call dword ptr [si]"                         \
  parm [si] [es di]                             \
  modify [ax   dx]


char qpi_get_port_trap(void __far **qpi_entry, int port);
#pragma aux qpi_get_port_trap =                 \
  "mov ax, 0x1A08"                              \
  "call dword ptr [si]"                         \
  parm [si] [dx]                                \
  value [bl]                                    \
  modify [ax]


void qpi_set_port_trap(void __far **qpi_entry, int port);
#pragma aux qpi_set_port_trap =                 \
  "mov ax, 0x1A09"                              \
  "call dword ptr [si]"                         \
  parm [si] [dx]                                \
  modify [ax]


void qpi_clear_port_trap(void __far **qpi_entry, int port);
#pragma aux qpi_clear_port_trap =               \
  "mov ax, 0x1A0A"                              \
  "call dword ptr [si]"                         \
  parm [si] [dx]                                \
  modify [ax]


void qpi_clear_port_trap(void __far **qpi_entry, int port);
#pragma aux qpi_clear_port_trap =               \
  "mov ax, 0x1A0A"                              \
  "call dword ptr [si]"                         \
  parm [si] [dx]                                \
  modify [ax]


static bool amis_unhook(struct iisp_header __far *handler, unsigned our_seg) {
  for (;;) {
    struct iisp_header __far *next_handler;
    if (handler->jump_to_start != 0x10EB
        || handler->signature != 0x424B
        || handler->jump_to_reset[0] != 0xEB) {
      return false;
    }
    next_handler = handler->next_handler;
    if (FP_SEG(next_handler) == our_seg) {
      handler->next_handler = next_handler->next_handler;
      return true;
    }
    handler = next_handler;
  }
}


static bool setup_emm386() {
  unsigned char version[2];
  int handle, err, v;

  err = _dos_open("EMMXXXX0", O_RDONLY, &handle);
  if (err) {
    err = _dos_open("EMMQXXX0", O_RDONLY, &handle);
  }
  if (err) {
    return false;
  }
  err = emm386_get_version(handle, &version);
  _dos_close(handle);
  if (err || !(version[0] == 4 && version[1] >= 46)) {
    return false;
  }

  err = emm386_virtualize_io(0x2A20, 0x2A2F, 16, &emm386_table, (int)&resident_end, &v);
  if (err) {
    return false;
  }
  config.emm_type = EMM_EMM386;
  config.emm386_virt_io_handle = v;
  return true;
}


static bool shutdown_emm386(struct config __far *cfg) {
  int err;
  err = emm386_unvirtualize_io(cfg->emm386_virt_io_handle);
  if (err) {
    return false;
  }
  cfg->emm_type = EMM_NONE;
  return true;
}


static void __far *get_qpi_entry_point() {
  int handle, err;
  void __far *qpi;
  err = _dos_open("QEMM386$", O_RDONLY, &handle);
  if (err) {
    return 0;
  }
  err = qemm_get_qpi_entry_point(handle, &qpi);
  _dos_close(handle);
  if (err) {
    return 0;
  }
  return qpi;
}


static bool setup_qemm() {
  void __far *qpi;
  int version;
  int i;

  qpi = get_qpi_entry_point();
  if (!qpi) {
    return false;
  }
  version = qpi_get_version(&qpi);
  if (version < 0x0703) {
    return false;
  }
  
  for (i = 0; i < 16; i++)
  {
	if (qpi_get_port_trap(&qpi, 0x2A20 + i)) {
      cputs("Some other program is already intercepting IMFC I/O\r\n");
      exit(1);
	}
  }
  
  for (i = 0; i < 16; i++)
	qpi_set_port_trap(&qpi, 0x2A20 + i);

  qemm_handler.next_handler = qpi_get_io_callback(&qpi);
  qpi_set_io_callback(&qpi, &qemm_handler);

  config.emm_type = EMM_QEMM;
  return true;
}


static bool shutdown_qemm(struct config __far *cfg) {
  void __far *qpi;
  struct iisp_header __far *callback;
  int i;

  qpi = get_qpi_entry_point();
  if (!qpi) {
    return false;
  }

  for (i = 0; i < 16; i++)
    qpi_clear_port_trap(&qpi, 0x2A20 + i);

  callback = qpi_get_io_callback(&qpi);
  if (FP_SEG(callback) == FP_SEG(cfg)) {
    qpi_set_io_callback(&qpi, callback->next_handler);
  } else {
    if (!amis_unhook(callback, FP_SEG(cfg))) {
      return false;
    }
  }
  cfg->emm_type = EMM_NONE;
  return true;
}


static void check_jemm(char bios_id) {
  unsigned char buf[6] = { 0 };
  int handle, err;

  err = _dos_open("EMMXXXX0", O_RDONLY, &handle);
  if (err) {
    err = _dos_open("EMMQXXX0", O_RDONLY, &handle);
  }
  if (err) {
    return;
  }
  err = ioctl_read(handle, &buf, 6);
  _dos_close(handle);
  if (err || !(buf[0] == 0x28 && buf[1] == 0)) {
    return;
  }

  cputs("Detected JEMM memory manager. Use this command instead:\r\n"
        "    JLOAD JIMFC.DLL");
  putch('1' + bios_id);
  cputs("\r\n");
  exit(1);
}


static bool uninstall(struct config __far *cfg) {
  struct iisp_header __far *current_amis_handler;

  if (cfg->emm_type == EMM_EMM386 && !shutdown_emm386(cfg)) {
    return false;
  }
  else if (cfg->emm_type == EMM_QEMM && !shutdown_qemm(cfg)) {
    return false;
  }

  current_amis_handler = (struct iisp_header __far *) _dos_getvect(0x2D);
  if (FP_SEG(current_amis_handler) == FP_SEG(cfg)) {
    _dos_setvect(0x2D, current_amis_handler->next_handler);
  } else {
    if (!amis_unhook(current_amis_handler, FP_SEG(cfg))) {
      return false;
    }
  }

  _dos_freemem(cfg->psp);
  return true;
}


static void usage(void) {
  cputs("Usage: SOFTIMFC\r\n"
        "       SOFTIMFC UNLOAD\r\n");
  exit(1);
}


static void status(struct config __far *cfg) {
  cputs("  Status: Loaded\r\n");
  putch('1' + cfg->bios_id);
  cputs("\r\n");
}


int main(void) {
  bool installed = false;
  bool found_unused_amis_id = false;
  int unused_amis_id = -1;
  struct config __far *cfg = &config;
  int i;

  cputs("SOFTIMFC " XSTR(VERSION_MAJOR) "." XSTR(VERSION_MINOR)
        "  \r\n\r\n");

  /* Check if the TSR is already in memory */
  /* Also look for an unused AMIS multiplex id */
  for (i = 0; i < 256; ++i) {
    struct amis_info info;
    int result = amis_install_check(i, &info);
    if (result == 0 && !found_unused_amis_id) {
      found_unused_amis_id = true;
      amis_id = i;
    }
    else if (result == -1 && _fmemcmp(info.signature, amis_header, 16) == 0) {
      if (info.version.word != (VERSION_MAJOR * 256 + VERSION_MINOR)) {
        cputs("Error: A different version of SOFTIMFC is already loaded.\r\n");
        exit(1);
      }
		// Initialize virtual IMFC
		InitIMFC();
	  
      installed = true;
      cfg = MK_FP(FP_SEG(info.signature),
                  *(short __far *)(info.signature + _fstrlen(info.signature) + 1));
      break;
    }
  }

  /* Parse the command line */
  {
    char cmdline[127];
    int cmdlen;
    char *arg;

    cmdlen = *(char __far *)MK_FP(_psp, 0x80);
    _fmemcpy(cmdline, MK_FP(_psp, 0x81), 127);
    cmdline[cmdlen] = 0;

    for (arg = strtok(cmdline, " "); arg; arg = strtok(NULL, " ")) {
      if (stricmp(arg, "unload") == 0) {
        if (!installed) {
          cputs("SOFTIMFC is not loaded.\r\n");
          exit(1);
        } else if (uninstall(cfg)) {
          cputs("SOFTIMFC is now unloaded from memory.\r\n");
          exit(0);
        } else {
          cputs("Could not unload SOFTIMFC.\r\n");
          exit(1);
        }
      } else {
        usage();
      }
    }
  }

  if (installed) {
    status(cfg);
  }
  else {
    int __far *env_seg;

    if (!found_unused_amis_id) {
      cputs("Error: No unused AMIS multiplex id found\n");
      return 1;
    }
    check_jemm(cfg->bios_id);
    if (!setup_qemm() && !setup_emm386()) {
      cputs("Error: no supported memory manager found\r\n"
            "Requires EMM386 4.46+, QEMM 7.03+ or JEMM\r\n");
      return 1;
    }
    status(cfg);

    cfg->psp = _psp;

    /* hook AMIS interrupt */
    amis_handler.next_handler = _dos_getvect(0x2D);
    _dos_setvect(0x2D, (void (__interrupt *)()) &amis_handler);

    /* free environment block */
    env_seg = MK_FP(_psp, 0x2C);
    _dos_freemem(*env_seg);
    *env_seg = 0;

    _dos_keep(0, ((char __huge *)&resident_end - (char __huge *)(_psp :> 0) + 15) / 16);
  }
  return 0;
}
