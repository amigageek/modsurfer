#include "system.h"
#include "custom.h"
#include "gfx.h"
#include "ptplayer/ptplayer.h"

#include <dos/dosextens.h>
#include <dos/filehandler.h>
#include <exec/execbase.h>
#include <graphics/gfxbase.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#define kLibVerKick1 33
#define kLibVerKick3 39
#define VBR_VecLev2 0x68

extern VOID level2_int();

static ULONG get_vbr();
static BOOL is_pal();
static VOID set_intreq(UWORD intreq);

struct GfxBase* GfxBase;
struct IntuitionBase* IntuitionBase;

static struct {
  struct FileInfoBlock fib __attribute__((aligned(sizeof(LONG))));
  struct View* old_view;
  struct copinit* old_copinit;
  BOOL wb_closed;
  UWORD old_intena;
  UWORD old_intreq;
  UWORD old_copcon;
  ULONG old_veclev2;
} g;

Status system_init() {
  Status status = StatusOK;

  ASSERT(GfxBase = (struct GfxBase*)OpenLibrary("graphics.library", kLibVerKick1));
  ASSERT(IntuitionBase = (struct IntuitionBase*)OpenLibrary("intuition.library", kLibVerKick1));

  g.wb_closed = CloseWorkBench();

cleanup:
  if (status == StatusError) {
    system_fini();
  }

  return status;
}

VOID system_fini() {
  if (g.wb_closed) {
    OpenWorkBench();
    g.wb_closed = FALSE;
  }

  if (IntuitionBase) {
    CloseLibrary((struct Library*)IntuitionBase);
    IntuitionBase = NULL;
  }

  if (GfxBase) {
    CloseLibrary((struct Library*)GfxBase);
    GfxBase = NULL;
  }
}

Status system_list_drives(dirlist_t* drives) {
  Status status = StatusOK;

  dirlist_init(drives);

  Forbid();

  struct DosInfo* dos_info = BADDR(DOSBase->dl_Root->rn_Info);
  struct DeviceNode* dev_list = BADDR(dos_info->di_DevInfo);

  for (struct DeviceNode* node = dev_list; node; node = BADDR(node->dn_Next)) {
    if (node->dn_Type == DLT_DEVICE && node->dn_Task) {
      ASSERT(dirlist_append(drives, EntryDir, BADDR(node->dn_Name) + 1));
    }
  }

  ASSERT(dirlist_sort(drives));

cleanup:
  if (status == StatusError) {
    dirlist_free(drives);
  }

  Permit();

  return status;
}

static BOOL is_mod_file(STRPTR name) {
  UWORD len = string_length(name);
  UBYTE mod_ext[] = { '.', 'M', 'O', 'D' };

  if (len < sizeof(mod_ext)) {
    return FALSE;
  }

  STRPTR ext_start = name + (len - sizeof(mod_ext));

  for (UWORD i = 0; i < sizeof(mod_ext); ++ i) {
    if (ext_start[i] != mod_ext[i]) {
      return FALSE;
    }
  }

  return TRUE;
}

Status system_list_path(STRPTR path,
                        dirlist_t* entries) {
  Status status = StatusOK;
  BPTR lock = 0;

  dirlist_init(entries);
  ASSERT(dirlist_append(entries, EntryDir, "/"));

  lock = Lock(path, ACCESS_READ);
  CHECK(lock);

  CHECK(Examine(lock, &g.fib));

  while (ExNext(lock, &g.fib)) {
    string_to_upper(g.fib.fib_FileName);

    dirlist_entry_type_t type;

    if (g.fib.fib_DirEntryType > 0) {
      type = EntryDir;
    }
    else {
      type = is_mod_file(g.fib.fib_FileName) ? EntryMod : EntryFile;
    }

    ASSERT(dirlist_append(entries, type, g.fib.fib_FileName));
  }

  ASSERT(dirlist_sort(entries));

cleanup:
  if (lock) {
    UnLock(lock);
  }

  if (status == StatusError) {
    dirlist_free(entries);
  }

  return status;
}

VOID system_append_path(STRPTR base,
                        STRPTR subdir) {
  UWORD base_len = string_length(base);

  if (subdir[0] == '/') {
    UWORD trim_to = base_len - 1;

    while ((base[trim_to - 1] != '/') && (base[trim_to - 1] != ':') && (trim_to > 0)) {
      -- trim_to;
    }

    base[trim_to] = '\0';
  }
  else {
    UWORD base_len = string_length(base);
    UWORD subdir_len = string_length(subdir);

    STRPTR separator = base[0] ? "/" : ":";
    string_copy(base + base_len, subdir);
    string_copy(base + base_len + subdir_len, separator);
  }
}

VOID system_acquire_control() {
  // Disable task switching before modifying the view.
  Forbid();

  // Wait for any in-flight blits to complete.
  gfx_wait_blit();

  // Save/allow copper to access blitter.
  g.old_copcon = custom.copcon;
  custom.copcon = COPCON_CDANG;

  // Save active view and load our copperlist.
  g.old_view = GfxBase->ActiView;
  g.old_copinit = GfxBase->copinit;
  LoadView(gfx_view());

  // Wait for odd/even field copperlists to finish.
  gfx_wait_vblank();
  gfx_wait_vblank();

  // Save and clear interrupt enable state.
  g.old_intena = custom.intenar;
  custom.intena = INTENA_CLEARALL;

  // Save and clear interrupt request state.
  g.old_intreq = custom.intreqr;
  set_intreq(INTREQ_CLEARALL);

  // Save and replace level 2 interrupt handler.
  ULONG vbr = get_vbr();
  volatile ULONG* vbr_veclev2 = (volatile ULONG*)(vbr + VBR_VecLev2);
  g.old_veclev2 = *vbr_veclev2;
  *vbr_veclev2 = (ULONG)level2_int;

  // Enable PORTS interrupts.
  custom.intena = INTENA_SET | INTENA_PORTS;

  // Install ptplayer interrupt handlers.
  mt_install_cia(&custom, (APTR)vbr, is_pal() ? 1 : 0);
}

VOID system_release_control() {
  // Remove ptplayer interrupt handlers.
  mt_remove_cia(&custom);

  // Disable PORTS interrupts.
  custom.intena = INTENA_PORTS;

  // Restore level 2 interrupt handler.
  *(volatile ULONG*)(get_vbr() + VBR_VecLev2) = g.old_veclev2;

  // Restore interrupt request state.
  set_intreq(INTREQ_CLEARALL);
  set_intreq(INTREQ_SET | g.old_intreq);

  // Restore interrupt enable state.
  custom.intena = INTENA_CLEARALL;
  custom.intena = INTENA_SET | g.old_intena;

  // Restore original view and copperlist.
  custom.cop1lc = (ULONG)g.old_copinit;
  LoadView(g.old_view);

  // Wait until copper-initiated blits have finished.
  gfx_wait_vblank();
  gfx_wait_blit();

  // Restore copper access.
  custom.copcon = g.old_copcon;

  // Enable task switching after restoring the view.
  Permit();

  // Keyboard handler now removed, clear any remaining state.
  for (UWORD i = 0; i < ARRAY_NELEMS(key_state); ++ i) {
    key_state[i] = 0;
  }
}

static ULONG get_vbr() {
  // VBR is 0x0 on 68000, supervisor register on 68010+.
  ULONG vbr = 0;

  if (SysBase->AttnFlags & (1U << AFB_68010)) {
    CONST UWORD get_vbr_reg[] = { 0x4E7A, 0x0801, 0x4E73 }; // movec.l vbr,d0; rte
    vbr = Supervisor((ULONG (*CONST)())get_vbr_reg);
  }

  return vbr;
}

static BOOL is_pal() {
  // >= Kickstart 3.0: Clock detection is accurate.
  //  < Kickstart 3.0: Clock is guessed from PAL/NTSC boot setting.
  UWORD pal_mask = (GfxBase->LibNode.lib_Version >= kLibVerKick3) ? REALLY_PAL : PAL;
  return (GfxBase->DisplayFlags & pal_mask) ? TRUE : FALSE;
}

static VOID set_intreq(UWORD intreq) {
  // Repeat twice to work around A4000 040/060 bug.
  for (UWORD i = 0; i < 2; ++ i) {
    custom.intreq = intreq;
  }
}
