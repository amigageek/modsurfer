#include "dtypes.h"

#include <proto/exec.h>

#define kVectorAllocGranule 0x400
#define kVectorAllocMask ((ULONG)(kVectorAllocGranule - 1))

VOID vector_init(UWORD elem_size,
                 vector_t* vec) {
  vec->elem_size = elem_size;
  vec->num_elems = 0;
  vec->storage = NULL;
  vec->storage_size = 0;
}

VOID vector_free(vector_t* vec) {
  if (vec->storage) {
    FreeMem(vec->storage, vec->storage_size);
    vec->storage = NULL;
  }
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

  ULONG req_storage_size = (vec->num_elems + num_elems) * vec->elem_size;

  if (req_storage_size > vec->storage_size) {
    ULONG new_storage_size = (req_storage_size + kVectorAllocMask) & ~kVectorAllocMask;
    APTR new_storage = AllocMem(new_storage_size, 0);
    CHECK(new_storage);

    if (vec->storage) {
      CopyMem(vec->storage, new_storage, vec->storage_size);
      vector_free(vec);
    }

    vec->storage = new_storage;
    vec->storage_size = new_storage_size;
  }

  APTR append_start = (APTR)((ULONG)vec->storage + (vec->num_elems * vec->elem_size));
  CopyMem(elems, append_start, num_elems * vec->elem_size);

  vec->num_elems += num_elems;

cleanup:
  return status;
}

VOID dirlist_init(dirlist_t* dl) {
  vector_init(sizeof(dirlist_entry_t), &dl->entries);
  vector_init(1, &dl->names);
}

VOID dirlist_free(dirlist_t* dl) {
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

  ASSERT(dl->names.num_elems <= kUWordMax);

  dirlist_entry_t entry = {
    .type = type,
    .name_offset = (UWORD)dl->names.num_elems,
  };

  ASSERT(vector_append(&dl->entries, 1, &entry));
  ASSERT(vector_append(&dl->names, string_length(name) + 1, name));

cleanup:
  return status;
}

static BOOL entry_order_before(dirlist_entry_t* entry1,
                               dirlist_entry_t* entry2,
                               STRPTR names) {
  if ((entry1->type == EntryDir) && (entry2->type != EntryDir)) {
    return TRUE;
  }

  if ((entry1->type != EntryDir) && (entry2->type == EntryDir)) {
    return FALSE;
  }

  STRPTR name1 = names + entry1->name_offset;
  STRPTR name2 = names + entry2->name_offset;
  UWORD name_idx;

  if (name1[0] == '/') {
    return TRUE;
  }

  for (name_idx = 0; name1[name_idx] && name2[name_idx]; ++ name_idx) {
    UBYTE char1 = name1[name_idx];
    UBYTE char2 = name2[name_idx];

    if (char1 < char2) {
      return TRUE;
    }

    if (char1 > char2) {
      return FALSE;
    }
  }

  return (name2[name_idx] ? TRUE : FALSE);
}

static VOID merge_entries(dirlist_entry_t* entries,
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

  while (left <= left_end && right <= right_end) {
    if (entry_order_before(&tmp_entries[left], &tmp_entries[right], names)) {
      entries[out ++] = tmp_entries[left ++];
    }
    else {
      entries[out ++] = tmp_entries[right ++];
    }
  }

  while (left <= left_end) {
    entries[out ++] = tmp_entries[left ++];
  }

  while (right <= right_end) {
    entries[out ++] = tmp_entries[right ++];
  }
}

Status dirlist_sort(dirlist_t* dl) {
  Status status = StatusOK;
  dirlist_entry_t* tmp_entries = NULL;

  // Make a temporary array for merge sort swaps.
  UWORD num_entries = dirlist_size(dl);
  ULONG entries_size = num_entries * sizeof(dirlist_entry_t);
  CHECK(tmp_entries = (dirlist_entry_t*)AllocMem(entries_size, 0));

  dirlist_entry_t* entries = dirlist_entries(dl);
  STRPTR names = dirlist_names(dl);

  // Iterate through sub-arrays of size 1,2,4,8...N-1
  for (UWORD subarr_size = 1; subarr_size < num_entries; subarr_size *= 2) {
    // Iterate through pairs of sub-arrays.
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
