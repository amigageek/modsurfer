#pragma once

#include "common.h"

typedef struct {
  UWORD elem_size;
  ULONG num_elems;
  APTR storage;
  ULONG storage_size;
} vector_t;

typedef struct {
  vector_t entries;
  vector_t names;
} dirlist_t;

typedef enum {
  EntryFile,
  EntryDir,
  EntryMod
} dirlist_entry_type_t;

typedef struct {
  dirlist_entry_type_t type;
  UWORD name_offset;
} dirlist_entry_t;

extern void vector_init(UWORD elem_size,
                        vector_t* vec);
extern void vector_free(vector_t* vec);
extern ULONG vector_size(vector_t* vec);
extern APTR vector_elems(vector_t* vec);
extern Status vector_append(vector_t* vec,
                            ULONG num_elems,
                            APTR elems);   // StatusError, StatusOutOfMemory

extern void dirlist_init(dirlist_t* dl);
extern void dirlist_free(dirlist_t* dl);
extern UWORD dirlist_size(dirlist_t* dl);
extern dirlist_entry_t* dirlist_entries(dirlist_t* dl);
extern STRPTR dirlist_names(dirlist_t* dl);
extern Status dirlist_append(dirlist_t* dl,
                             dirlist_entry_type_t type,
                             STRPTR name);  // StatusError
extern Status dirlist_sort(dirlist_t* dl);  // StatusError
