#include "menu.h"
#include "blit.h"
#include "gfx.h"
#include "module.h"
#include "system.h"

#include <devices/inputevent.h>
#include <proto/graphics.h>

#define kFooterHeight 13

#define kFrameWidth 1
#define kFramePad 4
#define kFrameY0 kDispHdrHeight
#define kFrameY1 (kFrameY0 + kFrameWidth + kFontHeight + (2 * kFramePad))
#define kFrameY2 ((kDispHeight - 1) - kFooterHeight)
#define kFrameX0 0
#define kFrameX1 (kFrameWidth + (2 * kFramePad) + ((kSampleNameMaxLen * kFontSpacing) - 1))
#define kFrameX2 (kFrameX3 - kFrameWidth - kSliderWidth - (kSliderPad * 2))
#define kFrameX3 (kDispWidth - 1)

#define kSliderPad 1
#define kSliderLeft (kFrameX2 + kFrameWidth + kSliderPad)
#define kSliderTop (kFrameY1 + kFrameWidth + kSliderPad)
#define kSliderWidth 10
#define kSliderMaxHeight ((kFrameY2 - kFrameY1 + 1) - (2 * kFrameWidth) - (2 * kSliderPad))

#define kPathInfoTop (kFrameY0 + kFrameWidth + kFramePad)
#define kPathInfoMaxChars (((kFrameX3 - kFrameX1 + 1) - (2 * kFrameWidth) - (2 * kFramePad)) / kFontSpacing)

#define kFileListRowGap 4
#define kTableRowHeight (kFontHeight + kFileListRowGap)
#define kTableTop (kFrameY1 + kFrameWidth + (kFramePad / 2))
#define kFileListBottom (kFrameY2 - kFrameWidth - (kFramePad / 2))
#define kFileListLeft (kFrameX1 + kFrameWidth + kFramePad)
#define kFileListRight (kFrameX2 - kFrameWidth - kFramePad)
#define kFileListWidth (kFileListRight - kFileListLeft + 1)
#define kTableHeight (kFileListBottom - kTableTop)
#define kTableNumRows (((kFrameY2 - kFrameY1 + 1) - (2 * kFrameWidth) - kFramePad) / kTableRowHeight)
#define kFileListMaxChars (((kFrameX2 - kFrameX1 + 1) - (2 * kFrameWidth) - (2 * kFramePad)) / kFontSpacing)

#define kFileInfoLeft (kFrameWidth + kFramePad)
#define kFileInfoRight (kFrameX1 - kFrameWidth - kFramePad)
#define kFileInfoWidth (kFileInfoRight - kFileInfoLeft + 1)

#define kFramePen 1
#define kDarkPen 2
#define kLightPen 3
#define kTextModPen 4

typedef struct {
  BOOL escape_pressed;
  BOOL mouse_pressed;
  BOOL disk_removed;
  UWORD mouse_x;
  UWORD mouse_y;
  UWORD mouse_2x; // Mouse tracked at double resolution for finer movement.
  UWORD mouse_2y;
} InputState;

static struct InputEvent* input_handler(struct InputEvent* event_list __asm("a0"),
                                        InputState* state __asm("a1"));
static Status refresh_file_list();
static Status mouse_button_down(UWORD mouse_x, UWORD mouse_y);
static void mouse_button_up(UWORD mouse_x, UWORD mouse_y);
static void mouse_moved(UWORD mouse_x, UWORD mouse_y);
static Status check_mouse_button_down_file_list(UWORD mouse_x, UWORD mouse_y);
static void check_mouse_button_down_slider(UWORD mouse_x, UWORD mouse_y);
static BOOL check_mouse_button_down_start_button(UWORD mouse_x, UWORD mouse_y);
static Status file_selected();
static void slider_move(WORD unclamped_offset);
static void slider_step(UWORD direction);
static WORD file_list_entry_at(UWORD pos_x, UWORD pos_y);
static void redraw_body();
static void redraw_path();
static void redraw_file_list(BOOL force_redraw);
static void redraw_slider(BOOL force_redraw);
static void redraw_mod_info();
static void redraw_file_list_highlight(UWORD entry_idx, BOOL highlighted);
static void draw_file_list_row(dirlist_entry_t* entries, STRPTR names, UWORD row);
static void draw_frames();
static void draw_footer_text();

static struct {
  volatile InputState input_state;
  BYTE dir_path[0x100];
  dirlist_t file_list;
  UWORD fl_entry_offset;
  WORD fl_entry_hover;
  WORD fl_entry_selected;
  UWORD slider_offset;
  UWORD slider_height;
  WORD slider_drag_start_mouse_y;
  WORD slider_drag_start_offset;
} g;

Status menu_init() {
  Status status = StatusOK;

  g.fl_entry_hover = -1;
  g.fl_entry_selected = -1;
  g.slider_drag_start_mouse_y = -1;
  g.slider_drag_start_offset = -1;
  g.input_state.mouse_2x = kDispWidth;
  g.input_state.mouse_2y = kDispHeight;
  g.input_state.mouse_x = g.input_state.mouse_2x / 2;
  g.input_state.mouse_y = g.input_state.mouse_2y / 2;

  ASSERT(system_add_input_handler(input_handler, (APTR)&g.input_state));

  system_acquire_blitter();
  gfx_draw_logo();
  gfx_init_score();
  gfx_update_pointer(g.input_state.mouse_x, g.input_state.mouse_y);
  ASSERT(refresh_file_list());

cleanup:
  system_release_blitter();
  return status;
}

void menu_fini() {
  module_close();
  system_remove_input_handler();

  dirlist_free(&g.file_list);
}

Status menu_redraw() {
  Status status = StatusOK;

  system_acquire_blitter();
  gfx_clear_body();
  redraw_body();
  draw_frames();
  draw_footer_text();
  system_release_blitter();

cleanup:
  return status;
}

static struct InputEvent* input_handler(struct InputEvent* event_list __asm("a0"),
                                        InputState* state __asm("a1")) {
  for (struct InputEvent* event = event_list; event; event = event->ie_NextEvent) {
    switch (event->ie_Class) {
    case IECLASS_RAWKEY:
      if (event->ie_Code == kKeycodeEsc) {
        state->escape_pressed = TRUE;
      }

      break;

    case IECLASS_RAWMOUSE:
      if (event->ie_Code == IECODE_LBUTTON) {
        state->mouse_pressed = TRUE;
      }
      else if (event->ie_Code == (IECODE_LBUTTON | IECODE_UP_PREFIX)) {
        state->mouse_pressed = FALSE;
      }

      // Mouse movement is tracked at high resolution.
      // Track this but downsample to low resolution when positioning sprite.
      state->mouse_2x = MAX(0, MIN((kDispWidth * 2) - 1, state->mouse_2x + event->ie_position.ie_xy.ie_x));
      state->mouse_2y = MAX(0, MIN((kDispHeight * 2) - 1, state->mouse_2y + event->ie_position.ie_xy.ie_y));

      UWORD mouse_x = state->mouse_2x >> 1;
      UWORD mouse_y = state->mouse_2y >> 1;

      if ((mouse_x != state->mouse_x) || (mouse_y != state->mouse_y)) {
        state->mouse_x = mouse_x;
        state->mouse_y = mouse_y;
        gfx_update_pointer(mouse_x, mouse_y);
      }

      break;

    case IECLASS_DISKREMOVED:
      state->disk_removed = TRUE;
      break;
    }

    // Suppress all event reporting to Intuition.
    event->ie_Class = IECLASS_NULL;
  }

  return event_list;
}

Status menu_event_loop() {
  Status status = StatusOK;
  BOOL mouse_pressed = FALSE;
  UWORD last_mouse_x = -1;
  UWORD last_mouse_y = -1;

  while (status == StatusOK) {
    WaitTOF();
    system_acquire_blitter();

    // Handle mouse movement events.
    if (g.input_state.mouse_x != last_mouse_x ||
        g.input_state.mouse_y != last_mouse_y) {
      last_mouse_x = g.input_state.mouse_x;
      last_mouse_y = g.input_state.mouse_y;

      mouse_moved(last_mouse_x, last_mouse_y);
    }

    // Handle mouse button down events.
    if (g.input_state.mouse_pressed && (! mouse_pressed)) {
      mouse_pressed = TRUE;
      CATCH(mouse_button_down(last_mouse_x, last_mouse_y), StatusPlay);
    }

    // Handle mouse button up events.
    if ((! g.input_state.mouse_pressed) && mouse_pressed) {
      mouse_pressed = FALSE;
      mouse_button_up(last_mouse_x, last_mouse_y);
    }

    // Handle disk removed events.
    if (g.input_state.disk_removed) {
      g.input_state.disk_removed = FALSE;

      string_copy(g.dir_path, "");
      ASSERT(refresh_file_list());
      redraw_body();
    }

    if (g.input_state.escape_pressed) {
      status = StatusQuit;
    }

    system_release_blitter();
  }

  if (status == StatusPlay) {
    status = StatusOK;
  }

cleanup:
  system_release_blitter();

  return status;
}

static Status refresh_file_list() {
  Status status = StatusOK;

  dirlist_free(&g.file_list);

  system_release_blitter();
  CATCH(system_list_path(g.dir_path, &g.file_list), StatusInvalidPath);

  if (status == StatusInvalidPath) {
    status = StatusOK;

    // If path is empty or inaccessible show the drive list instead.
    string_copy(g.dir_path, "");
    ASSERT(system_list_drives(&g.file_list));
  }

  UWORD num_entries = dirlist_size(&g.file_list);

  g.fl_entry_offset = 0;
  g.fl_entry_selected = -1;
  g.slider_offset = 0;
  g.slider_height = (kSliderMaxHeight * kTableNumRows) / MAX(kTableNumRows, num_entries);

  module_close();

cleanup:
  system_acquire_blitter();

  return status;
}

static Status mouse_button_down(UWORD mouse_x,
                                UWORD mouse_y) {
  Status status = StatusOK;

  ASSERT(check_mouse_button_down_file_list(mouse_x, mouse_y));
  check_mouse_button_down_slider(mouse_x, mouse_y);

  if (check_mouse_button_down_start_button(mouse_x, mouse_y)) {
    status = StatusPlay;
  }

cleanup:
  return status;
}

static void mouse_button_up(UWORD mouse_x,
                            UWORD mouse_y) {
  if (g.slider_drag_start_mouse_y != -1) {
    g.slider_drag_start_mouse_y = -1;
    g.slider_drag_start_offset = -1;
    redraw_slider(FALSE);
  }

  // Resume mouse hover highlighting now that button has been released.
  mouse_moved(mouse_x, mouse_y);
}

static void mouse_moved(UWORD mouse_x,
                        UWORD mouse_y) {
  if (g.slider_drag_start_offset >= 0) {
    WORD mouse_rel_y = mouse_y - g.slider_drag_start_mouse_y;
    slider_move(g.slider_drag_start_offset + mouse_rel_y);
  }
  else {
    WORD fl_entry_mouse = file_list_entry_at(mouse_x, mouse_y);

    // Hovering over a file list row?
    if (fl_entry_mouse != -1) {
      if (g.fl_entry_hover != fl_entry_mouse) {
        if ((g.fl_entry_hover != -1) && (g.fl_entry_hover != g.fl_entry_selected)) {
          redraw_file_list_highlight(g.fl_entry_hover, FALSE);
          g.fl_entry_hover = -1;
        }

        if (fl_entry_mouse < dirlist_size(&g.file_list)) {
          g.fl_entry_hover = fl_entry_mouse;
          redraw_file_list_highlight(g.fl_entry_hover, TRUE);
        }
      }
    }
    // Previously hovering over an unselected file list row but no longer?
    else if (g.fl_entry_hover != -1 && g.fl_entry_hover != g.fl_entry_selected) {
      redraw_file_list_highlight(g.fl_entry_hover, FALSE);
      g.fl_entry_hover = -1;
    }
  }
}

static Status check_mouse_button_down_file_list(UWORD mouse_x,
                                                UWORD mouse_y) {
  Status status = StatusOK;

  WORD fl_entry_mouse = file_list_entry_at(mouse_x, mouse_y);

  if ((fl_entry_mouse != -1) && (fl_entry_mouse != g.fl_entry_selected)) {
    if (g.fl_entry_selected != -1) {
      redraw_file_list_highlight(g.fl_entry_selected, FALSE);
      g.fl_entry_selected = -1;
    }

    dirlist_entry_t* entries = dirlist_entries(&g.file_list);
    dirlist_entry_t* entry = entries + fl_entry_mouse;

    if (entry->type == EntryDir) {
      STRPTR names = dirlist_names(&g.file_list);
      STRPTR name = names + entry->name_offset;

      string_append_path(g.dir_path, name);
      ASSERT(refresh_file_list());
      redraw_body();

      g.fl_entry_hover = -1;
      mouse_moved(mouse_x, mouse_y);
    }
    else {
      g.fl_entry_selected = fl_entry_mouse;
      redraw_file_list_highlight(g.fl_entry_selected, TRUE);

      ASSERT(file_selected());
    }
  }

cleanup:
  return status;
}

static void check_mouse_button_down_slider(UWORD mouse_x,
                                           UWORD mouse_y) {
  if (mouse_x >= kSliderLeft && mouse_y >= kSliderTop) {
    if (mouse_y < (kSliderTop + g.slider_offset)) {
      // Clicked above slider, step slider upwards.
      slider_step(-1);
    }
    else if (mouse_y >= (kSliderTop + g.slider_offset + g.slider_height)) {
      // Clicked below slider, step slider downwards.
      slider_step(1);
    }
    else {
      // Clicked on slider, begin drag.
      g.slider_drag_start_mouse_y = mouse_y;
      g.slider_drag_start_offset = g.slider_offset;
      redraw_slider(FALSE);
    }
  }
}

static BOOL check_mouse_button_down_start_button(UWORD mouse_x,
                                                 UWORD mouse_y) {
  if (module_is_open() && (mouse_y >= kFrameY0) && (mouse_y <= kFrameY1) && (mouse_x <= kFrameX1)) {
    menu_redraw_button("BUILDING TRACK...");
    return TRUE;
  }

  return FALSE;
}

static Status file_selected() {
  Status status = StatusOK;

  dirlist_entry_t* entries = dirlist_entries(&g.file_list);
  STRPTR names = dirlist_names(&g.file_list);
  dirlist_entry_t* entry = entries + g.fl_entry_selected;
  STRPTR file_name = names + entry->name_offset;

  module_close();
  module_open(g.dir_path, file_name);

  system_release_blitter();
  CATCH(module_load_header(), StatusInvalidMod);
  system_acquire_blitter();

  if (status == StatusInvalidMod) {
    module_close();
    menu_redraw_button(NULL);
    status = StatusOK;
  }
  else {
    menu_redraw_button("START GAME");
  }

  redraw_mod_info();

cleanup:
  return status;
}

static void slider_move(WORD unclamped_offset) {
  g.slider_offset = MAX(0, MIN(kSliderMaxHeight - g.slider_height, unclamped_offset));

  redraw_slider(FALSE);

  UWORD num_entries = dirlist_size(&g.file_list);
  UWORD new_fl_entry_offset = (num_entries * g.slider_offset) / kSliderMaxHeight;

  if (new_fl_entry_offset != g.fl_entry_offset) {
    g.fl_entry_offset = new_fl_entry_offset;
    redraw_file_list(FALSE);
  }
}

static void slider_step(UWORD direction) {
  slider_move(g.slider_offset + (direction * g.slider_height));
}

static WORD file_list_entry_at(UWORD pos_x,
                               UWORD pos_y) {
  WORD entry = -1;

  if ((pos_x >= kFrameX1) && (pos_x <= kFrameX2) &&
      (pos_y >= kTableTop) && (pos_y <= kFileListBottom)) {
    entry = ((pos_y - kTableTop) / kTableRowHeight) + g.fl_entry_offset;

    if (entry >= dirlist_size(&g.file_list)) {
      entry = -1;
    }
  }

  return entry;
}

static void redraw_body() {
  redraw_path();
  redraw_file_list(TRUE);
  redraw_slider(TRUE);
  menu_redraw_button(module_is_open() ? "START GAME" : NULL);
  redraw_mod_info();
}

static void redraw_path() {
  UBYTE* disp_planes = gfx_display_planes();
  UWORD clear_left = kFrameX1 + 1;
  UWORD clear_top = kFrameY0 + 1;
  UWORD clear_width = kFrameX3 - clear_left;
  UWORD clear_height = kFrameY1 - clear_top;

  for (UWORD i = 0; i < kDispDepth; ++ i) {
    UBYTE* plane = disp_planes + (i * kDispSlice);

    blit_rect(plane, kDispStride, clear_left, clear_top,
              NULL, 0, 0, 0, clear_width, clear_height, FALSE);
  }

  STRPTR path_start = g.dir_path + MAX(0, string_length(g.dir_path) - kPathInfoMaxChars);
  gfx_draw_text(path_start, -1, kFileListLeft, kPathInfoTop, kDarkPen, TRUE);
}

static void redraw_file_list(BOOL force_redraw) {
  static UWORD last_offset = -1;

  UWORD clear_top = 0;
  UWORD clear_height = 0;
  UWORD copy_from_top = 0;
  UWORD copy_to_top = 0;
  UWORD copy_height = 0;
  UWORD draw_row_start = 0;
  UWORD draw_row_end = 0;

  WORD entry_delta = g.fl_entry_offset - last_offset;
  UWORD num_entries = dirlist_size(&g.file_list);

  if (force_redraw || (ABS(entry_delta) >= kTableNumRows)) {
    clear_top = kTableTop;
    clear_height = kTableHeight;
    draw_row_end = MIN(kTableNumRows, num_entries);
  }
  else {
    clear_height = ABS(entry_delta) * kTableRowHeight;
    copy_height = kTableHeight - clear_height;

    if (entry_delta < 0) {
      clear_top = kTableTop;
      copy_from_top = kTableTop;
      copy_to_top = clear_top + clear_height;
      draw_row_start = 0;
      draw_row_end = - entry_delta;
    }
    else {
      clear_top = kFileListBottom - (entry_delta * kTableRowHeight);
      copy_from_top = kTableTop + clear_height;
      copy_to_top = kTableTop;
      draw_row_start = kTableNumRows - entry_delta;
      draw_row_end = kTableNumRows;
    }
  }

  last_offset = g.fl_entry_offset;

  UBYTE* planes = gfx_display_planes();
  UWORD redraw_left = kFrameX1 + kFrameWidth;
  UWORD redraw_width = kFrameX2 - redraw_left;

  for (UWORD i = 0; i < kDispDepth; ++ i) {
    UBYTE* plane = planes + (i * kDispSlice);

    if (copy_height > 0) {
      blit_copy(plane, kDispStride, redraw_left, copy_from_top,
                plane, kDispStride, redraw_left, copy_to_top,
                redraw_width, copy_height, TRUE, (copy_from_top < copy_to_top));
    }

    blit_rect(plane, kDispStride, redraw_left, clear_top,
              NULL, 0, 0, 0, redraw_width, clear_height, FALSE);
  }

  dirlist_entry_t* entries = dirlist_entries(&g.file_list);
  STRPTR names = dirlist_names(&g.file_list);

  for (UWORD i = draw_row_start; i < draw_row_end; ++ i) {
    draw_file_list_row(entries, names, i);
  }

  if (g.fl_entry_selected != -1) {
    redraw_file_list_highlight(g.fl_entry_selected, TRUE);
  }
}

static void redraw_slider(BOOL force_redraw) {
  static UWORD last_offset = 0;
  static UWORD last_height = 0;
  static UWORD last_color = 0;

  UWORD draw_top = 0;
  UWORD draw_height = 0;
  UWORD draw_planes = 0;
  UWORD clear_top = 0;
  UWORD clear_height = 0;
  UWORD clear_planes = 0;

  UWORD color = (g.slider_drag_start_offset >= 0) ? kLightPen : kFramePen;

  if (color != last_color) {
    // Color changed: clear and redraw the slider region.
    clear_planes = last_color & (~color);
    clear_top = draw_top = kSliderTop + g.slider_offset;
    clear_height = draw_height = g.slider_height;
    draw_planes = color;
  }
  else if (g.slider_height != last_height) {
    // Height changed: clear the old slider region and draw the new.
    clear_top = kSliderTop + last_offset;
    clear_height = last_height;
    clear_planes = last_color;
    draw_top = kSliderTop + g.slider_offset;
    draw_height = g.slider_height;
    draw_planes = color;
  }
  else if (g.slider_offset != last_offset) {
    if (g.slider_offset < last_offset) {
      // Moved up: clear old bottom region, draw new top region.
      clear_top = kSliderTop + MAX(last_offset, g.slider_offset + g.slider_height);
      clear_height = draw_height = MIN(g.slider_height, last_offset - g.slider_offset);
      draw_top = kSliderTop + g.slider_offset;
    }
    else {
      // Moved down: clear old top region, draw new bottom region.
      clear_top = kSliderTop + last_offset;
      clear_height = draw_height = MIN(g.slider_height, g.slider_offset - last_offset);
      draw_top = kSliderTop + MAX(g.slider_offset, last_offset + g.slider_height);
    }

    draw_planes = clear_planes = color;
  } else if (force_redraw) {
    // Already cleared, just draw.
    draw_top = kSliderTop + g.slider_offset;
    draw_height = g.slider_height;
    draw_planes = color;
  }

  last_color = color;
  last_offset = g.slider_offset;
  last_height = g.slider_height;

  UBYTE* plane = gfx_display_planes();

  for (UWORD plane_mask = 1; plane_mask < (1 << kDispDepth); plane_mask <<= 1) {
    if (clear_planes & plane_mask) {
      blit_rect(plane, kDispStride, kSliderLeft, clear_top,
                NULL, 0, 0, 0, kSliderWidth, clear_height, FALSE);
    }

    if (draw_planes & plane_mask) {
      blit_rect(plane, kDispStride, kSliderLeft, draw_top,
                NULL, 0, 0, 0, kSliderWidth, draw_height, TRUE);
    }

    plane += kDispSlice;
  }
}

static void redraw_mod_info() {
  ModuleHeader* header = module_is_open() ? module_header() : NULL;
  gfx_draw_title(header ? header->title : (BYTE*)"SELECT A MOD");

  UBYTE* planes = gfx_display_planes();

  for (UWORD plane_idx = 0; plane_idx < kDispDepth; ++ plane_idx) {
    if (kLightPen & (1 << plane_idx)) {
      UBYTE* plane = planes + (plane_idx * kDispSlice);
      blit_rect(plane, kDispStride, kFileInfoLeft, kTableTop,
                NULL, 0, 0, 0, kFileInfoWidth, kTableHeight, FALSE);
    }
  }

  if (header) {
    UWORD num_rows = MIN(kNumSamplesMax, kTableNumRows);

    for (UWORD row = 0; row < num_rows; ++ row) {
      STRPTR name = header->sample_info[row].name;
      UWORD top = kTableTop + (kFramePad / 2) + (row * kTableRowHeight);
      gfx_draw_text(name, kSampleNameMaxLen, kFileInfoLeft, top, kLightPen, TRUE);
    }
  }
}

void menu_redraw_button(STRPTR text) {
  UBYTE* planes = gfx_display_planes();

#define kPlayButtonWidth (kFrameX1 - kFrameX0)
#define kPlayButtonHeight (kFrameY1 - kFrameY0)

  for (UWORD plane_idx = 0; plane_idx < kDispDepth; ++ plane_idx) {
    BOOL draw = text && (kFramePen & (1 << plane_idx));
    UBYTE* plane = planes + (plane_idx * kDispSlice);
    blit_rect(plane, kDispStride, kFrameX0, kFrameY0,
              NULL, 0, 0, 0, kPlayButtonWidth, kPlayButtonHeight, draw);
  }

  if (text) {
    UWORD top = kFrameY0 + kFrameWidth + kFramePad;
    UWORD left = (kPlayButtonWidth - ((string_length(text) * kFontSpacing) - 1)) / 2;
    gfx_draw_text(text, -1, left, top, kLightPen, FALSE);
  }
}

static void redraw_file_list_highlight(UWORD entry_idx,
                                       BOOL highlighted) {
  dirlist_entry_t* entries = dirlist_entries(&g.file_list);
  dirlist_entry_t* entry = entries + entry_idx;

  UBYTE* disp_planes = gfx_display_planes();
  UBYTE* hl_plane = disp_planes + (0 * kDispSlice);
  UBYTE* mask_plane = disp_planes + ((entry->type == EntryMod ? 2 : 1) * kDispSlice);

  if (entry_idx >= g.fl_entry_offset && entry_idx < (g.fl_entry_offset + kTableNumRows)) {
    UWORD row = entry_idx - g.fl_entry_offset;
    UWORD left = kFrameX1 + 1;
    UWORD top = kTableTop + (row * kTableRowHeight);
    UWORD width = (kFrameX2 - kFrameX1 + 1) - (2 * kFrameWidth);

    blit_rect(hl_plane, kDispStride, left, top,
              mask_plane, kDispStride, left, top, width, kTableRowHeight, highlighted);
  }
}

static void draw_file_list_row(dirlist_entry_t* entries,
                               STRPTR names,
                               UWORD row) {
  dirlist_entry_t* entry = &entries[row + g.fl_entry_offset];

  STRPTR name = names + entry->name_offset;
  UWORD top = kTableTop + (kFramePad / 2) + (row * kTableRowHeight);
  UWORD entry_colors[] = {kLightPen, kDarkPen, kTextModPen};

  gfx_draw_text(name, kFileListMaxChars, kFileListLeft, top, entry_colors[entry->type], TRUE);
}

static void draw_frames() {
  static UWORD lines[][4] = {
    {kFrameX1, kFrameY0, kFrameX3, kFrameY0},
    {kFrameX0, kFrameY1, kFrameX3, kFrameY1},
    {kFrameX0, kFrameY2, kFrameX3, kFrameY2},
    {kFrameX0, kFrameY1, kFrameX0, kFrameY2},
    {kFrameX1, kFrameY0, kFrameX1, kFrameY2},
    {kFrameX2, kFrameY1, kFrameX2, kFrameY2},
    {kFrameX3, kFrameY0, kFrameX3, kFrameY2},
  };

  UBYTE* plane = gfx_display_planes();

  for (UWORD i = 0; i < ARRAY_NELEMS(lines); ++ i) {
    UWORD* line = (UWORD*)&lines[i];

    blit_line(plane, kDispStride, line[0], line[1], line[2], line[3]);
  }
}

static void draw_footer_text() {
  STRPTR text = "2018 EAB GAMEDEV COMPETITION ENTRY BY ARCANIST";
  UWORD left = (kDispWidth - (string_length(text) * kFontSpacing - 1)) / 2;
  UWORD top = kDispHeight - kFontHeight;

  gfx_draw_text(text, -1, left, top, kLightPen, TRUE);
}
