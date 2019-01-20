#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#define MAX(x, y) ((x) > (y) ? (x) : (y))

#define kBallEdge 0x20
#define kSpriteWidth 0x10
#define kNumColors 0xE
#define kNumSpritesPerFrame 4
#define kNumFrameAngles 8
#define kNumFrames (kNumFrameAngles * 2 + 1)
#define kMaxAngle 1.0
#define kAngleStep (kMaxAngle / kNumFrameAngles)

typedef struct {
  float v[2];
} vec2;

typedef struct {
  float v[3];
} vec3;

void make_colors(int colors[kNumFrames][kBallEdge][kBallEdge]);
vec2 tex_center_norm(int spr_x, int spr_y);
vec2 tex_sphere_map(vec2 uv, bool* inside_ball);
vec2 tex_rotate(vec2 uv, float angle);
void make_sprites(int colors[kNumFrames][kBallEdge][kBallEdge]);
float checker_pattern(float x, float y);
float fraction(float n);

int main() {
  static int colors[kNumFrames][kBallEdge][kBallEdge];

  make_colors(colors);
  make_sprites(colors);
}

void make_colors(int colors[kNumFrames][kBallEdge][kBallEdge]) {
  for (int frame = 0; frame < kNumFrames; ++ frame) {
    float angle = (kAngleStep * frame) + (M_PI / 2) - kMaxAngle;

    for (int spr_y = 0; spr_y < kBallEdge; ++ spr_y) {
      for (int spr_x = 0; spr_x < kBallEdge; ++ spr_x) {
        bool inside_ball = false;
        vec2 tex_uv = tex_center_norm(spr_x, spr_y);
        tex_uv = tex_sphere_map(tex_uv, &inside_ball);
        tex_uv = tex_rotate(tex_uv, angle);

        int color = 0;

        if (inside_ball) {
          float checker = checker_pattern(tex_uv.v[0], tex_uv.v[1]);
          color = 1 + (int)floorf(checker * kNumColors);
        }

        colors[frame][spr_y][spr_x] = color;
      }
    }
  }
}

vec2 tex_center_norm(int spr_x,
                     int spr_y) {
  return (vec2) {
    1.0f - (((spr_y * 2) + 1) / (float)kBallEdge),
    (((spr_x * 2) + 1) / (float)kBallEdge) - 1.0f,
  };
}

float solve_one_root(float a,
                     float b,
                     float c) {
  float d = (b * b) - (4 * a * c);
  float x1 = (-b + sqrtf(d)) / (2 * a);
  float x2 = (-b - sqrtf(d)) / (2 * a);

  return x1 < x2 ? x1 : x2;
}

vec2 tex_sphere_map(vec2 uv,
                    bool* inside_ball) {
  float radius_sqr =
    (uv.v[0] * uv.v[0]) +
    (uv.v[1] * uv.v[1]);

  *inside_ball = (radius_sqr < 1.0f);

  vec2 sph_uv = uv;//{0};

  if (*inside_ball) {
    float fov = M_PI / 4.0f;
    vec3 cam = { 0.0f, 0.0f, -1.0f / sin(fov / 2.0f) };
    float near_z = 1.0f;
    float near_w = tan(fov / 2) * near_z;
    vec3 ray = { uv.v[0] * near_w, uv.v[1] * near_w, near_z };

    float normalize = 0.0f;

    for (int i = 0; i < 3; ++ i) {
      normalize += ray.v[i] * ray.v[i];
    }

    normalize = sqrtf(normalize);

    for (int i = 0; i < 3; ++ i) {
      ray.v[i] /= normalize;
    }

    float quad_a = 1.0f;
    float quad_b = 2 * ray.v[2] * cam.v[2];
    float quad_c = (cam.v[2] * cam.v[2]) - 1.0f;
    float isec_dist = solve_one_root(quad_a, quad_b, quad_c);

    vec3 isec;

    for (int i = 0; i < 3; ++ i) {
      isec.v[i] = cam.v[i] + ray.v[i] * isec_dist;
    }

    sph_uv.v[0] = 3.0f * (0.5f + atan2f(isec.v[2], isec.v[0]) / M_PI);
    sph_uv.v[1] = -3.0f * asinf(isec.v[1]) / M_PI;
  }

  return sph_uv;
}

vec2 tex_rotate(vec2 uv,
                float angle) {
  return (vec2){
    (uv.v[0] * cosf(angle)) - (uv.v[1] * sinf(angle)),
    (uv.v[1] * cosf(angle)) + (uv.v[0] * sinf(angle))
  };
}

float checker_pattern(float x,
                      float y) {
  float x_frac = fraction(x);
  return fraction(y + (x_frac <= 0.5f ? 0.0f : 0.5f)); // FIXME: < 0.5f
}

float fraction(float n) {
  return n - floorf(n);
}

void make_sprites(int colors[kNumFrames][kBallEdge][kBallEdge]) {
  printf("#include <exec/types.h>\n\n");
  printf("#define kBallNumAngles %d\n\n", kNumFrameAngles);
  printf("static UWORD ball_sprs[%d][%d][0x%X + 2][2] __chip = {\n",
         kNumFrames, kNumSpritesPerFrame, kBallEdge);

  for (int frame = 0; frame < kNumFrames; ++ frame) {
    printf("  {\n");

    for (int sprite = 0; sprite < kNumSpritesPerFrame; ++ sprite) {
      printf("    {\n");
      printf("      {0x0000, 0x0000},\n");

      for (int y = 0; y < kBallEdge; ++ y) {
        int plane_start = (sprite & 1) ? 2 : 0;
        int x_start = (sprite < 2) ? 0 : kSpriteWidth;

        printf("      {");

        for (int plane = plane_start; plane <= (plane_start + 1); ++ plane) {
          int plane_mask = 1 << plane;
          unsigned short color_word = 0;

          for (int bit = 0; bit < kSpriteWidth; ++ bit) {
            int x = x_start + (kSpriteWidth - 1 - bit);
            color_word |= ((colors[frame][y][x] & plane_mask) ? 1 : 0) << bit;
          }

          printf("%s0x%04hX", (plane > plane_start ? ", " : ""), color_word);
        }

        printf("},\n");
      }

      printf("      {0x0000, 0x0000},\n");
      printf("    },\n");
    }

    printf("  },\n");
  }

  printf("};\n");
}
