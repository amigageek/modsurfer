#include "dtypes.h"

#include <proto/exec.h>

#define kVectorAllocGranule 0x400
#define kVectorAllocMask (kVectorAllocGranule - 1)

static void merge_entries(dirlist_entry_t* entries,
                          dirlist_entry_t* tmp_entries,
                          STRPTR names,
                          UWORD left_start,
                          UWORD left_end,
                          UWORD right_end);
static BOOL entry_order_before(dirlist_entry_t* entry1,
                               dirlist_entry_t* entry2,
                               STRPTR names);

void vector_init(UWORD elem_size,
                 vector_t* vec) {
  *vec = (vector_t){0};
  vec->elem_size = elem_size;
}

void vector_free(vector_t* vec) {
  if (vec->storage) {
    FreeMem(vec->storage, vec->storage_size);
  }

  *vec = (vector_t){0};
}

ULONG vector_size(vector_t* vec) {
  return vec->num_elems;
}

APTR vector_elems(vector_t* vec) {
  return vec->storage;
}

Status vector_append(vector_t* vec,
                     ULONG num_elems,
                     APTR elems) {
  Status status = StatusOK;
  APTR new_storage = NULL;
  ULONG new_storage_size = 0;

  ULONG req_storage_size = (vec->num_elems + num_elems) * vec->elem_size;

  if (req_storage_size > vec->storage_size) {
    new_storage_size = (req_storage_size + kVectorAllocMask) & ~kVectorAllocMask;
    CHECK(new_storage = AllocMem(new_storage_size, 0), StatusOutOfMemory);

    if (vec->storage) {
      CopyMem(vec->storage, new_storage, vec->storage_size);
      FreeMem(vec->storage, vec->storage_size);
    }

    vec->storage = new_storage;
    vec->storage_size = new_storage_size;
  }

  APTR append_start = vec->storage + (vec->num_elems * vec->elem_size);
  CopyMem(elems, append_start, num_elems * vec->elem_size);

  vec->num_elems += num_elems;

cleanup:
  if ((status != StatusOK) && new_storage) {
    FreeMem(new_storage, new_storage_size);
  }

  return status;
}

void dirlist_init(dirlist_t* dl) {
  vector_init(sizeof(dirlist_entry_t), &dl->entries);
  vector_init(sizeof(char), &dl->names);
}

void dirlist_free(dirlist_t* dl) {
  vector_free(&dl->names);
  vector_free(&dl->entries);
}

UWORD dirlist_size(dirlist_t* dl) {
  return (UWORD)vector_size(&dl->entries);
}

dirlist_entry_t* dirlist_entries(dirlist_t* dl) {
  return (dirlist_entry_t*)vector_elems(&dl->entries);
}

STRPTR dirlist_names(dirlist_t* dl) {
  return (STRPTR)vector_elems(&dl->names);
}

Status dirlist_append(dirlist_t* dl,
                      dirlist_entry_type_t type,
                      STRPTR name) {
  Status status = StatusOK;

  // Entry count and name offsets are UWORDs.
  UWORD name_size = string_length(name) + 1;

  ASSERT((vector_size(&dl->entries) + 1) <= kUWordMax);
  ASSERT((vector_size(&dl->names) + name_size) <= kUWordMax);

  dirlist_entry_t entry = {
    .type = type,
    .name_offset = (UWORD)vector_size(&dl->names),
  };

  ASSERT(vector_append(&dl->entries, 1, &entry) == StatusOK);
  ASSERT(vector_append(&dl->names, name_size, name) == StatusOK);

cleanup:
  return status;
}

Status dirlist_sort(dirlist_t* dl) {
  Status status = StatusOK;
  dirlist_entry_t* tmp_entries = NULL;
  ULONG entries_size = 0;

  // Make a temporary array for merge sort swaps.
  UWORD num_entries = dirlist_size(dl);
  entries_size = num_entries * sizeof(dirlist_entry_t);
  ASSERT(tmp_entries = (dirlist_entry_t*)AllocMem(entries_size, 0));

  dirlist_entry_t* entries = dirlist_entries(dl);
  STRPTR names = dirlist_names(dl);

  // Iterate through sub-arrays of size 1,2,4,8...N-1
  for (UWORD subarr_size = 1; subarr_size < num_entries; subarr_size *= 2) {
    // Iterate through adjacent pairs of sub-arrays.
    UWORD last_subarr_start = num_entries - subarr_size;

    for (UWORD left_start = 0; left_start < last_subarr_start; left_start += (subarr_size * 2)) {
      // Merge elements of this pair of sub-arrays in sorted order.
      UWORD left_end = left_start + (subarr_size - 1);
      UWORD right_end = MIN(left_end + subarr_size, num_entries - 1);

      merge_entries(entries, tmp_entries, names, left_start, left_end, right_end);
    }
  }

cleanup:
  if (tmp_entries) {
    FreeMem(tmp_entries, entries_size);
  }

  return status;
}

static void merge_entries(dirlist_entry_t* entries,
                          dirlist_entry_t* tmp_entries,
                          STRPTR names,
                          UWORD left_start,
                          UWORD left_end,
                          UWORD right_end) {
  for (UWORD i = left_start; i <= right_end; ++ i) {
    tmp_entries[i] = entries[i];
  }

  UWORD left = left_start;
  UWORD right = left_end + 1;
  UWORD out = left_start;

  // While both left/right pairs have data, merge in sorted order.
  while (left <= left_end && right <= right_end) {
    if (entry_order_before(&tmp_entries[left], &tmp_entries[right], names)) {
      entries[out ++] = tmp_entries[left ++];
    }
    else {
      entries[out ++] = tmp_entries[right ++];
    }
  }

  // Only one pair left, merge remainder.
  while (left <= left_end) {
    entries[out ++] = tmp_entries[left ++];
  }

  while (right <= right_end) {
    entries[out ++] = tmp_entries[right ++];
  }
}

static BOOL entry_order_before(dirlist_entry_t* entry1,
                               dirlist_entry_t* entry2,
                               STRPTR names) {
  // Order directories before files.
  if ((entry1->type == EntryDir) != (entry2->type == EntryDir)) {
    return (entry1->type == EntryDir);
  }

  STRPTR name1 = names + entry1->name_offset;
  STRPTR name2 = names + entry2->name_offset;
  UWORD name_idx = 0;

  // Order parent directory before others.
  if (name1[0] == '/') {
    return TRUE;
  }

  for (; name1[name_idx] && name2[name_idx]; ++ name_idx) {
    UBYTE char1 = name1[name_idx];
    UBYTE char2 = name2[name_idx];

    // Order alphabetically.
    if (char1 < char2) {
      return TRUE;
    }

    if (char1 > char2) {
      return FALSE;
    }
  }

  // Order shorter names before longer names.
  return (name2[name_idx] ? TRUE : FALSE);
}
