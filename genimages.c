#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT(x)                                \
  if (! (x)) {                                  \
    fprintf(stderr, "assert(%s) failed\n", #x); \
    ret = false;                                \
    goto cleanup;                               \
  }

#define BE16_TO_LE(x) (                         \
    (((x) & 0x00FF) << 0x8) |                   \
    (((x) & 0xFF00) >> 0x8)                     \
  )

#define LE16_TO_BE(x) BE16_TO_LE(x)

#define BE32_TO_LE(x) (                         \
    (((x) & 0x000000FF) << 0x18) |              \
    (((x) & 0x0000FF00) <<  0x8) |              \
    (((x) & 0x00FF0000) >>  0x8) |              \
    (((x) & 0xFF000000) >> 0x18)                \
  )

#define IFF_CHUNK_ID(a, b, c, d) (((a) << 0x18) | ((b) << 0x10) | ((c) << 0x8) | (d))

typedef struct {
  uint16_t bmh_Width;
  uint16_t bmh_Height;
  int16_t  bmh_Left;
  int16_t  bmh_Top;
  uint8_t  bmh_Depth;
  uint8_t  bmh_Masking;
  uint8_t  bmh_Compression;
  uint8_t  bmh_Pad;
  uint16_t bmh_Transparent;
  uint8_t  bmh_XAspect;
  uint8_t  bmh_YAspect;
  int16_t  bmh_PageWidth;
  int16_t  bmh_PageHeight;
} BitMapHeader;

bool load_ilbm(const char* path,
               uint8_t** out_bpls,
               uint32_t* out_bpls_size_b,
               uint32_t* out_width,
               uint32_t* out_height,
               uint32_t* out_depth) {
  bool ret = true;
  FILE* iff_file = NULL;
  uint8_t* iff_data = NULL;
  BitMapHeader* bmh = NULL;
  uint8_t* bpls = NULL;

  // Read the IFF/ILBM glyph image into memory.
  long iff_size = 0;

  ASSERT(iff_file = fopen(path, "rb"));
  ASSERT(fseek(iff_file, 0, SEEK_END) == 0);
  ASSERT((iff_size = ftell(iff_file)) != -1);
  ASSERT(fseek(iff_file, 0, SEEK_SET) == 0);

  ASSERT(iff_data = malloc(iff_size));
  ASSERT(fread(iff_data, 1, iff_size, iff_file) == iff_size);

  // Walk through each chunk in the IFF data.
  uint32_t bpls_stride_b = 0;
  uint32_t bpls_size_b = 0;

  for (uint32_t iff_offset = 0; iff_offset < iff_size; ) {
    ASSERT(iff_offset + 8 <= iff_size);

    uint32_t chunk_id = BE32_TO_LE(*(uint32_t*)(iff_data + iff_offset));
    uint32_t chunk_size = BE32_TO_LE(*(uint32_t*)(iff_data + iff_offset + 4));

    switch (chunk_id) {
    case IFF_CHUNK_ID('F', 'O', 'R', 'M'):
      iff_offset += 8;
      continue;

    case IFF_CHUNK_ID('I', 'L', 'B', 'M'):
      iff_offset += 4;
      continue;

    case IFF_CHUNK_ID('B', 'M', 'H', 'D'): {
      ASSERT(iff_offset + chunk_size <= iff_size);
      ASSERT(chunk_size == sizeof(BitMapHeader));

      bmh = (BitMapHeader*)(iff_data + iff_offset + 8);
      uint16_t width = BE16_TO_LE(bmh->bmh_Width);
      uint16_t height = BE16_TO_LE(bmh->bmh_Height);
      uint8_t depth = bmh->bmh_Depth;

      bpls_stride_b = ((width + 0xF) / 0x10) * 0x2;
      bpls_size_b = bpls_stride_b * depth * height;

      *out_width = width;
      *out_height = height;
      *out_depth = depth;

      break;
    }

    case IFF_CHUNK_ID('B', 'O', 'D', 'Y'):
      // Decompress bitplane data if needed.
      ASSERT(iff_offset + chunk_size <= iff_size);
      ASSERT(bpls == NULL);

      ASSERT(bpls = (uint8_t*)malloc(bpls_size_b));

      if (bmh->bmh_Compression == 0) {
        ASSERT(chunk_size == bpls_size_b);
        memcpy(bpls, iff_data + iff_offset + 8, chunk_size);
      }
      else {
        // ILBM RLE decompression.
        uint8_t* body_data = iff_data + iff_offset + 8;
        uint32_t bpls_idx = 0;

        for (uint32_t body_idx = 0; body_idx < chunk_size; ) {
          uint8_t in_byte = body_data[body_idx ++];

          if (in_byte > 0x80) {
            // Repeat following byte into output.
            uint8_t rep_count = (uint8_t)(0x101 - in_byte);
            ASSERT(body_idx < chunk_size);
            ASSERT(bpls_idx + rep_count <= bpls_size_b);

            uint8_t rep_byte = body_data[body_idx ++];

            for (uint8_t rep_idx = 0; rep_idx < rep_count; ++ rep_idx) {
              bpls[bpls_idx ++] = rep_byte;
            }
          }
          else if (in_byte < 0x80) {
            // Copy following bytes into output.
            uint8_t lit_count = in_byte + 1;
            ASSERT(body_idx + lit_count <= chunk_size);
            ASSERT(bpls_idx + lit_count <= bpls_size_b);

            for (uint32_t lit_idx = 0; lit_idx < lit_count; ++ lit_idx) {
              bpls[bpls_idx ++] = body_data[body_idx ++];
            }
          }
          else {
            break;
          }
        }

        ASSERT(bpls_idx == bpls_size_b);
      }

      break;
    }

    iff_offset += 8 + chunk_size + (chunk_size & 1);
  }

  ASSERT(bpls);
  *out_bpls = bpls;
  *out_bpls_size_b = bpls_size_b;

cleanup:
  if (ret == false) {
    free(bpls);
  }

  free(iff_data);

  if (iff_file) {
    fclose(iff_file);
  }

  return ret;
}

static bool make_img_array(const char* img_name,
                           bool in_chip) {
  bool ret = true;
  uint8_t* bpls = NULL;
  uint32_t bpls_size_b = 0;
  uint32_t width, height, depth;

  char path[100];
  sprintf(path, "images/%s.iff", img_name);
  ASSERT(load_ilbm(path, &bpls, &bpls_size_b, &width, &height, &depth));

  printf("\nstatic UWORD %s%s_planes[] = {",
         in_chip ? "__chip " : "", img_name);

  for (uint32_t i = 0; i < bpls_size_b / 2; ++ i) {
    if (i % 8 == 0) {
      printf("\n ");
    }

    printf(" 0x%04X,", LE16_TO_BE(((uint16_t*)bpls)[i]));
  }

  printf("\n};\n");
  printf("#define %s_width %u\n", img_name, width);
  printf("#define %s_height %u\n", img_name, height);
  printf("#define %s_depth %u\n", img_name, depth);

cleanup:
  free(bpls);

  return ret;
}

int main() {
  bool ret = true;

  printf("#include <exec/types.h>\n");

  ASSERT(make_img_array("logo", true));
  ASSERT(make_img_array("font", true));
  ASSERT(make_img_array("pointer", true));

cleanup:
  return ret ? 0 : 1;
}
