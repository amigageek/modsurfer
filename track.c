#include "track.h"
#include "build/tables.h"
#include "module.h"
#include "system.h"

#include <proto/exec.h>
#include <stdio.h> // FIXME
#include <stdlib.h> // FIXME

#define kPeriodTableSize 857 // C-1 = 856

static struct {
  TrackStep* track_steps;
  ULONG track_steps_size;
  ULONG track_length;
  UWORD track_num_blocks;
  ULONG pat_select_samples[kNumPatternsMax];
  UBYTE period_to_color[kPeriodTableSize];
  UWORD prng_seed;
} g;

// FIXME: test all effects

#define kTrackDataAllocGranule 0x1000
#define kTrackDataLengthGranule (kTrackDataAllocGranule / sizeof(TrackStep))
#define kEffectPosJump     0xB
#define kEffectPatBreak    0xD
#define kEffectExtend      0xE
#define kEffectSetSpeed    0xF
#define kEffectExtPatLoop  0x6
#define kEffectExtPatDelay 0xE

//#define DEBUG_PATTERN 10

Status track_init() {
  Status status = StatusOK;

  UWORD period_table[] = {
  // C    C#   D    D#   E    F    F#   G    G#   A    A#   B
    856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453, // Octave 1
    428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226, // Octave 2
    214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113, // Octave 3
    0,
  };

  UWORD period_idx = 0;
  UWORD next_period = period_table[period_idx];
  UWORD next_color = 0;

  for (WORD i = kPeriodTableSize - 1; i >= 0; -- i) {
    if (i == next_period) {
      next_color = period_idx / 3;

      ++ period_idx;
      next_period = period_table[period_idx];
    }

    g.period_to_color[i] = next_color;
  }

  ULONG time_micros = 0;
  ASSERT(system_time_micros(&time_micros));

  g.prng_seed = (UWORD)time_micros;

cleanup:
  return status;
}

static Status track_realloc() {
  Status status = StatusOK;

  ULONG new_size = g.track_steps_size + kTrackDataAllocGranule;
  TrackStep* new_data;
  CHECK(new_data = AllocMem(new_size, 0), StatusOutOfMemory);

  if (g.track_steps) {
    CopyMemQuick(g.track_steps, new_data, g.track_steps_size);
    track_free();
  }

  g.track_steps = new_data;
  g.track_steps_size = new_size;

cleanup:
  return status;
}

static Status pad_visible(BOOL lead_in) {
  Status status = StatusOK;

  UWORD num_steps = lead_in ? kNumVisibleSteps : kNumPaddingSteps;
  TrackStep step = {0};

  for (UWORD i = 0; i < num_steps; ++ i) {
    // Reallocate track data if song length exceeds the last granule.
    if ((g.track_length & (kTrackDataLengthGranule - 1)) == 0) {
      CATCH(track_realloc(), 0);
    }

    g.track_steps[g.track_length ++] = step;
  }

cleanup:
  return status;
}

// FIXME: pitch variability?

#define kFFTImag 0
#define kFFTReal 1
static WORD fft_data[2][kFFTSize];

static UWORD samp_dom_freq[kNumSamples];

static WORD fix_mult(WORD a,
                     WORD b) {
  asm (
    "muls.w  %1,%0;"        // c = a*b
    "swap    %0;"           // c = a*b[15:0, 31:16]
    "rol.l   #1,%0;"        // c = a*b[14:0, 31, 30:15]
    "bpl     .no_carry_%=;" // branch if a*b[14] is 0
    "addq.w  #1,%0;"        // c = a*b[30:15] + a*b[14]
    ".no_carry_%=:;"
    : "+d"(a), "+d"(b)
  );

  return a;
}

static void analyze_samples() {
  ModuleHeader* mod_hdr = &module_get_nonchip()->header;
  BYTE* samples = (BYTE*)module_get_samples();

  for (UWORD samp_idx = 0; samp_idx < kNumSamples; ++ samp_idx) {
    UWORD samp_size_b = mod_hdr->sample_info[samp_idx].length_w * 2;

    if (samp_size_b == 0) {
      samp_dom_freq[samp_idx] = 0;
      continue;
    }

    // Center FFT window on sample data to avoid attack/decay.
    if (samp_size_b > kFFTSize) {
      // FIXME
    }

    // Real FFT to half-size complex FFT, decimation in time, bytes to words.
    for (UWORD data_idx = 0; data_idx < kFFTSize; ++ data_idx) {
      WORD value_real = 0;
      WORD value_imag = 0;

      UWORD reorder_idx = FFTReorder[data_idx];

      if (reorder_idx < samp_size_b) {
        value_real = (WORD)(samples[reorder_idx + 2]) << 8;
        value_imag = (WORD)(samples[reorder_idx]) << 8;
      }

      fft_data[kFFTReal][data_idx] = value_real;
      fft_data[kFFTImag][data_idx] = value_imag;
    }

    UWORD k = kFFTSizeLog2 - 1;

    for (UWORD level = 1; level < kFFTSize; level *= 2) {
      for (UWORD m = 0; m < level; ++ m) {
        UWORD j = m << k;
        WORD wr = FFTSinLUT[j + (kFFTSize / 4)] >> 1;
        WORD wi = -FFTSinLUT[j] >> 1;

        for (UWORD i = m; i < kFFTSize; i += (level * 2)) {
          j = i + level;

          WORD tr = fix_mult(wr, fft_data[kFFTReal][j]) - fix_mult(wi, fft_data[kFFTImag][j]);
          WORD ti = fix_mult(wr, fft_data[kFFTImag][j]) + fix_mult(wi, fft_data[kFFTReal][j]);
          WORD qr = fft_data[kFFTReal][i] >> 1;
          WORD qi = fft_data[kFFTImag][i] >> 1;

          fft_data[kFFTReal][j] = qr - tr;
          fft_data[kFFTImag][j] = qi - ti;
          fft_data[kFFTReal][i] = qr + tr;
          fft_data[kFFTImag][i] = qi + ti;
        }
      }

      -- k;
    }

    LONG max_ampl_sqr = 0;
    UWORD max_freq_idx = 0;

    for (UWORD i = 1; i < kFFTSize / 2; ++ i) {
      LONG ampl_sqr =
        ((fft_data[kFFTReal][i] * fft_data[kFFTReal][i]) >> 1) +
        ((fft_data[kFFTImag][i] * fft_data[kFFTImag][i]) >> 1);

      if (ampl_sqr > max_ampl_sqr) {
        max_ampl_sqr = ampl_sqr;
        max_freq_idx = i;
      }
    }

    samp_dom_freq[samp_idx] = max_freq_idx;

    samples += samp_size_b;
  }
}

static UWORD prng() {
  // LFSR PRNG (http://codebase64.org/doku.php?id=base:small_fast_16-bit_prng)
#define kLFSRMagic 0xC2DF
  if (g.prng_seed == 0) {
    g.prng_seed ^= kLFSRMagic;
  }
  else if (g.prng_seed == 0x8000) {
    g.prng_seed = 0;
  }
  else {
    UWORD carry = g.prng_seed & 0x8000;
    g.prng_seed <<= 1;

    if (carry) {
      g.prng_seed ^= kLFSRMagic;
    }
  }

  return g.prng_seed;
}

static UWORD prng4() {
  static UWORD last_random = 0;

  if (last_random == 0) {
    last_random = prng();
  }

  UWORD random4 = last_random & 3;
  last_random >>= 2;

  return random4;
}

static BOOL skip_command(PatternCommand* cmd) {
  BOOL skip = FALSE;

  if (((cmd->effect >> 8) == 0xC) && ((cmd->effect & 0xFF) < 32)) {
    skip = TRUE;
  }

  return skip;
}

static void select_samples() {
  ModuleNonChip* nonchip = module_get_nonchip();
  UWORD num_patterns = module_get_num_patterns();

  analyze_samples();

  for (UWORD pat_idx = 0; pat_idx < num_patterns; ++ pat_idx) {
    // Count active samples in the pattern.
    static UBYTE sample_count[0x20];
    static ULONG sample_periods[0x20];

    for (UWORD i = 0; i < 0x20; ++ i) {
      sample_count[i] = 0;
      sample_periods[i] = 0;
    }

    Pattern* pat = &nonchip->patterns[pat_idx];

    for (UWORD div_idx = 0; div_idx < kDivsPerPat; ++ div_idx) {
      PatternDivision* div = &pat->divisions[div_idx];

      for (UWORD cmd_idx = 0; cmd_idx < 4; ++ cmd_idx) {
        PatternCommand* cmd = &div->commands[cmd_idx];

        UWORD effect_major = cmd->effect >> 8;

        if (effect_major == kEffectPatBreak) {
          div_idx = kDivsPerPat;
          break;
        }

        if (skip_command(cmd)) {
          continue;
        }

        UBYTE sample = (cmd->sample_hi << 4) | cmd->sample_lo;
        UWORD period = cmd->parameter;

        if (sample && period) {
          ++ sample_count[sample];
          sample_periods[sample] += period;
        }
      }
    }

// FIXME: Instead of pitch target, sort and choose percentile?
#define kCountTargetMin 16
#define kPitchTarget 3000
#define kCountWeight 1
#define kPitchWeight 2

    UWORD best_score_idx = 0;
    ULONG best_score = (ULONG)-1;

    for (UWORD i = 1; i < 31; ++ i) {
      if (sample_count[i] > 0) {
        UWORD avg_period = sample_periods[i] / sample_count[i];
        UWORD pitch = ((UWORD)(0x10000 / avg_period) * samp_dom_freq[i - 1]) / 5;

        UWORD score_count = MAX(kCountTargetMin - sample_count[i], 0) << 8;

        if (score_count > 0xC00) {
          score_count *= 8;
        }

        UWORD score_pitch = ABS(pitch - kPitchTarget);

        ULONG score = (score_count * kCountWeight) + (score_pitch * kPitchWeight);

        if (score < best_score) {
          best_score = score;
          best_score_idx = i;
        }
      }
    }

    g.pat_select_samples[pat_idx] = 1UL << best_score_idx;
  }
}

typedef struct {
  UWORD pat_tbl_idx;
  UWORD div_start_idx;
  UWORD active_contiguous_count;
  UWORD last_active_lane;
} BuildState;

static Status walk_pattern(UWORD pat_idx,
                           BuildState* state) {
  Status status = StatusOK;
  ModuleNonChip* nonchip = module_get_nonchip();

  // Walk through the pattern, starting from the first division.
  Pattern* pat = &nonchip->patterns[pat_idx];
  ULONG select_samples = g.pat_select_samples[pat_idx];

  UWORD div_idx = state->div_start_idx;
  state->div_start_idx = 0;

  UWORD loop_idx = 0;
  UWORD loop_count = -1;

  while (div_idx < kDivsPerPat) {
    PatternDivision* div = &pat->divisions[div_idx];
    UWORD next_div_idx = div_idx + 1;

    // Reallocate track data if song length exceeds the last granule.
    if ((g.track_length & (kTrackDataLengthGranule - 1)) == 0) {
      CATCH(track_realloc(), 0);
    }

    UBYTE sample_in_step = 0;
    UBYTE step_color = 0;

    for (UWORD cmd_idx = 0; cmd_idx < 4; ++ cmd_idx) {
      PatternCommand* cmd = (PatternCommand*)&div->commands[cmd_idx];

      if (skip_command(cmd)) {
        continue;
      }

      static UBYTE last_sample[4] = {0, 0, 0, 0};
      UBYTE sample = (cmd->sample_hi << 4) | cmd->sample_lo;

      if (! sample) {
        sample = last_sample[cmd_idx];
      }
      else {
        last_sample[cmd_idx] = sample;
      }

      UWORD period = cmd->parameter;

      if (period && sample && (select_samples & (1UL << sample))) {
        sample_in_step = sample;
        step_color = g.period_to_color[period];
      }
    }

    TrackStep* step = &g.track_steps[g.track_length];
    *step = (TrackStep){0};

    if (sample_in_step != 0) {
      step->sample = sample_in_step;
      step->color = step_color;

      static UWORD next_lane_lut[4][4] = {
        // Row indexed by last active lane, column indexed by random number.
        2, 1, 2, 3,
        1, 2, 2, 3,
        2, 1, 2, 3,
        3, 2, 2, 1,
      };

      UWORD random4 = prng4();

      if (state->active_contiguous_count == 1) {
        // Avoid lane change in contiguous segments until length is >= 2.
        random4 = 0;
      }

      if (state->active_contiguous_count && (state->last_active_lane != 2) && (random4 == 3)) {
        // Avoid left-right and right-left lane changes in contiguous segments.
        random4 = 0;
      }

      step->active_lane = next_lane_lut[state->last_active_lane][random4];

      if (step->active_lane != state->last_active_lane) {
        state->last_active_lane = step->active_lane;
        state->active_contiguous_count = 0;
      }

      ++ state->active_contiguous_count;
      ++ g.track_num_blocks;
    }
    else {
      state->active_contiguous_count = 0;
    }

    // Handle commands which change the next division step.
    UBYTE delay = 0;

    for (ULONG cmd_idx = 0; cmd_idx < 4; ++ cmd_idx) {
      UWORD effect = div->commands[cmd_idx].effect;

      switch (effect >> 8) {
      case kEffectPosJump:
        next_div_idx = kDivsPerPat;
        state->pat_tbl_idx = effect & 0xFF;
        break;

      case kEffectPatBreak:
        state->div_start_idx = effect & 0xFF;

        // If division index exceeds kDivsPerPat ptplayer jumps to first division.
        if (state->div_start_idx >= kDivsPerPat) {
          state->div_start_idx = 0;
        }

        next_div_idx = kDivsPerPat;
        break;

      case kEffectExtend:
        switch ((effect >> 4) & 0xF) {
        case kEffectExtPatDelay:
          delay = effect & 0xF;
          break;

        case kEffectExtPatLoop: {
          UWORD cmd_count = effect & 0xF;

          if (cmd_count == 0) {
            loop_idx = div_idx;
          }
          else if (loop_count == 0) {
            loop_count = -1;
          }
          else {
            if (loop_count == (UWORD)-1) {
              loop_count = cmd_count;
            }

            -- loop_count;
            next_div_idx = loop_idx;
          }

          break;
        }
        }

      case kEffectSetSpeed:
        if ((effect & 0xFF) == 0) {
          status = StatusTrackEnd;
          next_div_idx = kDivsPerPat;
        }
      }
    }

    ++ g.track_length;
    div_idx = next_div_idx;

    for (UWORD i = 0; i < delay; ++ i) {
      // FIXME: realloc with vector_append
      g.track_steps[g.track_length] = (TrackStep){0};
      ++ g.track_length;
 }
  }

cleanup:
  return status;
}

static Status walk_song_table() {
  Status status = StatusOK;
  ModuleNonChip* nonchip = module_get_nonchip();

  // Keep track of which pattern table entries have been visited, to detect loops.
  static UBYTE pat_tbl_visited[kNumPatternsMax];

  for (UWORD i = 0; i < kNumPatternsMax; ++ i) {
    pat_tbl_visited[i] = 0;
  }

  // Walk through the pattern table, starting from the first entry.
  BuildState state = {0};

  for (state.pat_tbl_idx = 0; state.pat_tbl_idx < nonchip->header.pat_tbl_size; ) {
    UWORD pat_idx = nonchip->header.pat_tbl[state.pat_tbl_idx];

    // Stop if we looped in the pattern table.
    if (pat_tbl_visited[state.pat_tbl_idx]) {
      break;
    }

    pat_tbl_visited[state.pat_tbl_idx] = 1;
    ++ state.pat_tbl_idx;

    CATCH(walk_pattern(pat_idx, &state), StatusTrackEnd);

    if (status == StatusTrackEnd) {
      status = StatusOK;
      break;
    }
  }

cleanup:
  return status;
}

Status track_build() {
  Status status = StatusOK;

  ASSERT(g.track_steps == NULL);
  g.track_length = 0;
  g.track_num_blocks = 0;

  // Select samples in each pattern corresponding to collectibles.
  select_samples();

  // Start with empty steps covering the visible track.
  CATCH(pad_visible(TRUE), 0);

  // Create steps for every division in the song, in playback order.
  CATCH(walk_song_table(), 0);

  // Finish with empty steps covering the visible track.
  CATCH(pad_visible(FALSE), 0);

cleanup:
  if (status != StatusOK) {
    track_free();
  }

  return status;
}

void track_free() {
  if (g.track_steps) {
    FreeMem(g.track_steps, g.track_steps_size);
    g.track_steps = NULL;
    g.track_steps_size = 0;
  }
}

TrackStep* track_get_steps() {
  return g.track_steps;
}

ULONG track_get_length() {
  return g.track_length;
}

UWORD track_get_num_blocks() {
  return g.track_num_blocks;
}
