#include "system.h"
#include "custom.h"
#include "gfx.h"
#include "ptplayer/ptplayer.h"

#include <clib/alib_protos.h>
#include <devices/timer.h>
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
#define kVBRLvl2IntOffset 0x68

extern void level2_int();

static void allow_task_switch(BOOL allow);
static ULONG get_vbr();
static void set_intreq(UWORD intreq);

struct GfxBase* GfxBase;
struct IntuitionBase* IntuitionBase;

static struct {
  BOOL wb_closed;
  BOOL task_switch_disabled;
  BOOL blitter_owned;
  UWORD save_copcon;
  UWORD save_dmacon;
  struct View* save_view;
  struct copinit* save_copinit;
  UWORD save_intena;
  UWORD save_intreq;
  ULONG save_vbr_lvl2;
} g;

Status system_init() {
  Status status = StatusOK;

  ASSERT(GfxBase = (struct GfxBase*)OpenLibrary("graphics.library", kLibVerKick1));
  ASSERT(IntuitionBase = (struct IntuitionBase*)OpenLibrary("intuition.library", kLibVerKick1));

  g.wb_closed = CloseWorkBench();

cleanup:
  if (status != StatusOK) {
    system_fini();
  }

  return status;
}

void system_fini() {
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

void system_print_error(STRPTR msg) {
  // DOS needs task switching to handle Write request.
  // Console needs blitter to draw text.
  if (DOSBase && (! g.task_switch_disabled) && (! g.blitter_owned)) {
    STRPTR out_strs[] = {"modsurfer: assert(", msg, ") failed\n"};
    BPTR out_handle = Output();

    for (UWORD i = 0; i < ARRAY_NELEMS(out_strs); ++ i) {
      Write(out_handle, out_strs[i], string_length(out_strs[i]));
    }
  }
}

Status system_time_micros(ULONG* time_micros) {
  Status status = StatusOK;
  struct MsgPort* port = NULL;
  struct timerequest* timer_io = NULL;
  BOOL timer_opened = FALSE;

  ASSERT(port = CreatePort(NULL, 0));
  ASSERT(timer_io = (struct timerequest*)CreateExtIO(port, sizeof(struct timerequest)));
  ASSERT(OpenDevice("timer.device", UNIT_VBLANK, (struct IORequest*)timer_io, 0) == 0);
  timer_opened = TRUE;

  timer_io->tr_node.io_Command = TR_GETSYSTIME;
  DoIO((struct IORequest*)timer_io);

  // Only the microsecond component of the time is returned.
  // This is sufficient for seeding the random number generator.
  *time_micros = timer_io->tr_time.tv_micro;

cleanup:
  if (timer_opened) {
    CloseDevice((struct IORequest*)timer_io);
  }

  if (timer_io) {
    DeleteExtIO((struct IORequest*)timer_io);
  }

  if (port) {
    DeletePort(port);
  }

  return status;
}

Status system_list_drives(dirlist_t* drives) {
  Status status = StatusOK;

  dirlist_init(drives);

  // OS may change the following data structures, disable task switching.
  allow_task_switch(FALSE);

  struct DosInfo* dos_info = BADDR(DOSBase->dl_Root->rn_Info);
  struct DeviceNode* dev_list = BADDR(dos_info->di_DevInfo);

  for (struct DeviceNode* node = dev_list; node; node = BADDR(node->dn_Next)) {
    if (node->dn_Type == DLT_DEVICE && node->dn_Task) {
      ASSERT(dirlist_append(drives, EntryDir, BADDR(node->dn_Name) + 1));
    }
  }

  ASSERT(dirlist_sort(drives));

cleanup:
  allow_task_switch(TRUE);

  if (status != StatusOK) {
    dirlist_free(drives);
  }

  return status;
}

Status system_list_path(STRPTR path,
                        dirlist_t* entries) {
  Status status = StatusOK;
  BPTR lock = 0;
  UBYTE mod_prefix[] = {'M', 'O', 'D', '.'};
  UBYTE mod_suffix[] = {'.', 'M', 'O', 'D'};

  CHECK(string_length(path) > 0, StatusInvalidPath);

  // First entry is a link to the parent directory.
  dirlist_init(entries);
  ASSERT(dirlist_append(entries, EntryDir, "/"));

  lock = Lock(path, ACCESS_READ);
  CHECK(lock, StatusInvalidPath);

  static struct FileInfoBlock fib;
  CHECK(Examine(lock, &fib), StatusInvalidPath);

  while (ExNext(lock, &fib)) {
    // Use uppercase filenames for display and sorting.
    string_to_upper(fib.fib_FileName);

    dirlist_entry_type_t type;

    if (fib.fib_DirEntryType > 0) {
      type = EntryDir;
    }
    else {
      if (string_has_suffix(fib.fib_FileName, mod_suffix, sizeof(mod_suffix)) ||
          string_has_prefix(fib.fib_FileName, mod_prefix, sizeof(mod_prefix))) {
        type = EntryMod;
      }
      else {
        type = EntryFile;
      }
    }

    ASSERT(dirlist_append(entries, type, fib.fib_FileName));
  }

  ASSERT(dirlist_sort(entries));

cleanup:
  if (lock) {
    UnLock(lock);
  }

  if (status != StatusOK) {
    dirlist_free(entries);
  }

  return status;
}

void system_acquire_control() {
  // Disable task switching until control is released.
  allow_task_switch(FALSE);

  // Wait for any in-flight blits to complete.
  // Our new copperlist expects exclusive access to the blitter.
  gfx_wait_blit();

  // Save and disable copper access to blitter registers.
  g.save_copcon = custom.copcon;
  custom.copcon = 0;

  // Save active view/copperlist and load our copperlist.
  g.save_view = GfxBase->ActiView;
  g.save_copinit = GfxBase->copinit;
  LoadView(gfx_view());

  // Wait for odd/even field copperlists to finish.
  gfx_wait_vblank();
  gfx_wait_vblank();

  // Save and enable DMA channels.
  g.save_dmacon = custom.dmaconr;
  custom.dmacon = DMACON_CLEARALL;
  custom.dmacon = DMACON_SET | DMACON_DMAEN | DMACON_BPLEN | DMACON_COPEN | DMACON_BLTEN | DMACON_SPREN;

  // Save and clear interrupt state.
  g.save_intena = custom.intenar;
  custom.intena = INTENA_CLEARALL;

  g.save_intreq = custom.intreqr;
  set_intreq(INTREQ_CLEARALL);

  // Clear keyboard state before installing interrupt handler.
  memory_clear((APTR)keyboard_state, sizeof(keyboard_state));

  // Save and replace level 2 interrupt handler.
  ULONG vbr = get_vbr();
  volatile ULONG* vbr_lvl2 = (volatile ULONG*)(vbr + kVBRLvl2IntOffset);

  g.save_vbr_lvl2 = *vbr_lvl2;
  *vbr_lvl2 = (ULONG)level2_int;

  // Enable PORTS interrupts for level 2 handler.
  custom.intena = INTENA_SET | INTENA_PORTS;

  // Install ptplayer interrupt handlers.
  mt_install_cia(&custom, (APTR)vbr, 1);
}

void system_release_control() {
  // Remove ptplayer interrupt handlers.
  mt_remove_cia(&custom);

  // Disable PORTS interrupts.
  custom.intena = INTENA_PORTS;

  // Restore level 2 interrupt handler.
  ULONG vbr = get_vbr();
  volatile ULONG* vbr_lvl2 = (volatile ULONG*)(vbr + kVBRLvl2IntOffset);

  *vbr_lvl2 = g.save_vbr_lvl2;

  // Restore interrupt state.
  set_intreq(INTREQ_CLEARALL);
  set_intreq(INTREQ_SET | g.save_intreq);

  custom.intena = INTENA_CLEARALL;
  custom.intena = INTENA_SET | g.save_intena;

  // Restore DMA channels.
  custom.dmacon = DMACON_CLEARALL;
  custom.dmacon = DMACON_SET | g.save_dmacon;

  // Restore original view and copperlist.
  custom.cop1lc = (ULONG)g.save_copinit;
  LoadView(g.save_view);

  // Wait until copper-initiated blits have finished.
  gfx_wait_vblank();
  gfx_wait_blit();

  // Restore copper access to blitter registers.
  custom.copcon = g.save_copcon;

  // Enable task switching after control duration.
  allow_task_switch(TRUE);
}

static void allow_task_switch(BOOL allow) {
  if (allow && g.task_switch_disabled) {
    Permit();
    g.task_switch_disabled = FALSE;
  }
  else if ((! allow) && (! g.task_switch_disabled)) {
    Forbid();
    g.task_switch_disabled = TRUE;
  }
}

static ULONG get_vbr() {
  // VBR is 0 on 68000, supervisor register on 68010+.
  ULONG vbr = 0;

  if (SysBase->AttnFlags & (1U << AFB_68010)) {
    UWORD get_vbr_reg[] = {0x4E7A, 0x0801, 0x4E73}; // movec vbr,d0; rte
    vbr = Supervisor((ULONG (*const)())get_vbr_reg);
  }

  return vbr;
}

static void set_intreq(UWORD intreq) {
  // Repeat twice to work around A4000 040/060 bug.
  for (UWORD i = 0; i < 2; ++ i) {
    custom.intreq = intreq;
  }
}

void system_acquire_blitter() {
  if (! g.blitter_owned) {
    g.blitter_owned = TRUE;
    OwnBlitter();
  }
}

void system_release_blitter() {
  if (g.blitter_owned) {
    g.blitter_owned = FALSE;
    DisownBlitter();
  }
}

void system_allow_copper_blits(BOOL allow) {
  custom.copcon = allow ? COPCON_CDANG : 0;
}
