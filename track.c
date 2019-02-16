#include "track.h"
#include "build/tables.h"
#include "dtypes.h"
#include "module.h"

#define kPeriodTableSize 857 // C-1 = 856
#define kFirstSampleNum 1  // Protracker uses samples 1-31
#define kFFTImag 0
#define kFFTReal 1
#define kEffectPosJump 0xB
#define kEffectSetVolume 0xC
#define kEffectPatBreak 0xD
#define kEffectExtend 0xE
#define kEffectSetSpeed 0xF
#define kEffectExtPatLoop 0x6
#define kEffectExtPatDelay 0xE
#define kCountTargetMin1 16
#define kCountTargetMin2 4
#define kCountTargetMin2Penalty 8
#define kPitchTarget 3000
#define kScoreCountWeight 0x100
#define kScorePitchWeight 2

typedef struct {
  UWORD pat_tbl_idx;
  UWORD div_idx;
  UWORD div_start_idx;
  UWORD loop_idx;
  UWORD loop_count;
  UWORD active_contiguous_count;
  UWORD last_active_lane;
} BuildState;

static void select_samples();
static void analyze_samples();
static void real_to_fft_input(BYTE* samples,
                              ULONG samp_size_b);
static void apply_fft();
static WORD fix_mult(WORD a,
                     WORD b);
static void find_dominant_freq(UWORD samp_idx);
static void count_samples(UWORD pat_idx);
static BOOL skip_command(PatternCommand* cmd);
static void select_lead_sample(UWORD pat_idx);
static Status pad_visible(BOOL lead_in);
static Status walk_pattern_table();
static Status walk_pattern(UWORD pat_idx,
                           BuildState* state);
static Status make_step(PatternDivision* div,
                        BuildState* state,
                        ULONG select_samples);
static Status handle_commands(PatternDivision* div,
                              BuildState* state);

static struct {
  vector_t track_steps;
  UWORD track_num_blocks;
  ULONG pat_select_samples[kNumPatternsMax];
  UBYTE period_to_color[kPeriodTableSize];
  UWORD samp_dom_freq[kNumSamplesMax];
  UBYTE samp_count[kNumSamplesMax];
  ULONG samp_period_sum[kNumSamplesMax];
  WORD fft_data[2][kFFTSize];
} g;

void track_init() {
  // Protracker periods with 0 finetune.
  // These are matched with notes in the module to color blocks by pitch.
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

  // Loop over all periods in descending order (ascending pitch).
  for (WORD period = kPeriodTableSize - 1; period >= 0; -- period) {
    // Maintain the color until we reach the next Protracker note.
    if (period == next_period) {
      next_color = period_idx / ((ARRAY_NELEMS(period_table) - 1) / kNumBlockColors);

      ++ period_idx;
      next_period = period_table[period_idx];
    }

    g.period_to_color[period] = next_color;
  }
}

Status track_build() {
  Status status = StatusOK;

  ASSERT(vector_size(&g.track_steps) == 0);
  vector_init(sizeof(TrackStep), &g.track_steps);

  g.track_num_blocks = 0;

  // Choose samples in each pattern to correspond with blocks.
  select_samples();

  // Start with empty steps covering the visible track.
  CATCH(pad_visible(TRUE), 0);

  // Create steps for every division in the song, in playback order.
  CATCH(walk_pattern_table(), 0);

  // Finish with empty steps covering the visible track.
  CATCH(pad_visible(FALSE), 0);

 cleanup:
  if (status != StatusOK) {
    track_free();
  }

  return status;
}

static void select_samples() {
  analyze_samples();

  UWORD num_patterns = module_num_patterns();

  for (UWORD pat_idx = 0; pat_idx < num_patterns; ++ pat_idx) {
    count_samples(pat_idx);
    select_lead_sample(pat_idx);
  }
}

static void analyze_samples() {
  ModuleHeader* mod_hdr = &module_nonchip()->header;
  BYTE* next_sample = (BYTE*)module_samples();

  for (UWORD samp_idx = 0; samp_idx < kNumSamplesMax; ++ samp_idx) {
    ULONG samp_size_b = mod_hdr->sample_info[samp_idx].length_w * 2;

    // Zero-sized samples are not used in the module.
    if (samp_size_b == 0) {
      g.samp_dom_freq[samp_idx] = 0;
      continue;
    }

    real_to_fft_input(next_sample, samp_size_b);
    apply_fft();
    find_dominant_freq(samp_idx);

    next_sample += samp_size_b;
  }
}

static void real_to_fft_input(BYTE* samples,
                              ULONG samp_size_b) {
  // Real FFT to half-size complex FFT, decimation in time, bytes to words.
  for (UWORD data_idx = 0; data_idx < kFFTSize; ++ data_idx) {
    WORD value_real = 0;
    WORD value_imag = 0;

    UWORD reorder_idx = FFTReorder[data_idx];

    if (reorder_idx < samp_size_b) {
      value_real = (WORD)(samples[reorder_idx + 2]) << 8;
      value_imag = (WORD)(samples[reorder_idx]) << 8;
    }

    g.fft_data[kFFTReal][data_idx] = value_real;
    g.fft_data[kFFTImag][data_idx] = value_imag;
  }
}

static void apply_fft() {
  // Based on fix_fft: https://gist.github.com/Tomwi/3842231
  UWORD k = kFFTSizeLog2 - 1;

  for (UWORD level = 1; level < kFFTSize; level *= 2) {
    for (UWORD m = 0; m < level; ++ m) {
      UWORD j = m << k;
      WORD wr = FFTSinLUT[j + (kFFTSize / 4)] >> 1;
      WORD wi = -FFTSinLUT[j] >> 1;

      for (UWORD i = m; i < kFFTSize; i += (level * 2)) {
        j = i + level;

        WORD tr = fix_mult(wr, g.fft_data[kFFTReal][j]) - fix_mult(wi, g.fft_data[kFFTImag][j]);
        WORD ti = fix_mult(wr, g.fft_data[kFFTImag][j]) + fix_mult(wi, g.fft_data[kFFTReal][j]);
        WORD qr = g.fft_data[kFFTReal][i] >> 1;
        WORD qi = g.fft_data[kFFTImag][i] >> 1;

        g.fft_data[kFFTReal][j] = qr - tr;
        g.fft_data[kFFTImag][j] = qi - ti;
        g.fft_data[kFFTReal][i] = qr + tr;
        g.fft_data[kFFTImag][i] = qi + ti;
      }
    }

    -- k;
  }
}

static WORD fix_mult(WORD a,
                     WORD b) {
  // Fixed-point multiplication with normalization for FFT.
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

static void find_dominant_freq(UWORD samp_idx) {
  LONG max_ampl_sqr = 0;
  UWORD dom_freq_idx = 0;

  for (UWORD i = 1; i < kFFTSize / 2; ++ i) {
    LONG ampl_sqr =
      ((g.fft_data[kFFTReal][i] * g.fft_data[kFFTReal][i]) >> 1) +
      ((g.fft_data[kFFTImag][i] * g.fft_data[kFFTImag][i]) >> 1);

    if (ampl_sqr > max_ampl_sqr) {
      max_ampl_sqr = ampl_sqr;
      dom_freq_idx = i;
    }
  }

  g.samp_dom_freq[samp_idx] = dom_freq_idx;
}

static void count_samples(UWORD pat_idx) {
  ModuleNonChip* nonchip = module_nonchip();
  Pattern* pat = &nonchip->patterns[pat_idx];

  for (UWORD i = 0; i < kNumSamplesMax; ++ i) {
    g.samp_count[i] = 0;
    g.samp_period_sum[i] = 0;
  }

  for (UWORD div_idx = 0; div_idx < kDivsPerPattern; ++ div_idx) {
    PatternDivision* div = &pat->divisions[div_idx];

    for (UWORD cmd_idx = 0; cmd_idx < 4; ++ cmd_idx) {
      PatternCommand* cmd = &div->commands[cmd_idx];

      if (skip_command(cmd)) {
        continue;
      }

      // Some MODs place commands after a pattern break, exit early.
      UWORD effect_major = cmd->effect >> 8;

      if (effect_major == kEffectPatBreak) {
        div_idx = kDivsPerPattern;
        break;
      }

      UBYTE samp_num = (cmd->sample_hi << 4) | cmd->sample_lo;
      UWORD period = cmd->parameter;

      if (samp_num && period) {
        UWORD samp_idx = samp_num - kFirstSampleNum;

        ++ g.samp_count[samp_idx];
        g.samp_period_sum[samp_idx] += period;
      }
    }
  }
}

static BOOL skip_command(PatternCommand* cmd) {
  BOOL skip = FALSE;

  // Skip over samples played at half volume or less.
  if (((cmd->effect >> 8) == kEffectSetVolume) && ((cmd->effect & 0xFF) < 0x20)) {
    skip = TRUE;
  }

  return skip;
}

static void select_lead_sample(UWORD pat_idx) {
  UWORD best_samp_idx = 0;
  ULONG best_score = (ULONG)-1;

  for (UWORD samp_idx = 0; samp_idx < kNumSamplesMax; ++ samp_idx) {
    if (g.samp_count[samp_idx] > 0) {
      // Combine FFT-derived frequency with average resample rate.
      // Resulting pitch is in non-standard units but linearly correlated.
      UWORD avg_period = g.samp_period_sum[samp_idx] / g.samp_count[samp_idx];
      UWORD pitch = ((UWORD)(0x10000 / avg_period) * g.samp_dom_freq[samp_idx]) / 5;

      // Penalize samples with counts below the first minimum threshold.
      UWORD score_count = MAX(kCountTargetMin1 - g.samp_count[samp_idx], 0);

      // Penalize heavily below the second minimum sample count threshold.
      if (g.samp_count[samp_idx] < kCountTargetMin2) {
        score_count *= kCountTargetMin2Penalty;
      }

      // Weight pitch and count scores to form lead instrument score.
      UWORD score_pitch = ABS(pitch - kPitchTarget);
      ULONG score = (score_count * kScoreCountWeight) + (score_pitch * kScorePitchWeight);

      if (score < best_score) {
        best_score = score;
        best_samp_idx = samp_idx;
      }
    }
  }

  g.pat_select_samples[pat_idx] = 1UL << (best_samp_idx + kFirstSampleNum);
}

static Status pad_visible(BOOL lead_in) {
  Status status = StatusOK;

  UWORD num_steps = lead_in ? kNumVisibleSteps : kNumPaddingSteps;
  TrackStep step = {0};

  for (UWORD i = 0; i < num_steps; ++ i) {
    CATCH(vector_append(&g.track_steps, 1, &step), 0);
  }

 cleanup:
  return status;
}

static Status walk_pattern_table() {
  Status status = StatusOK;
  ModuleNonChip* nonchip = module_nonchip();

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

    // Increment here because walk_pattern may overwrite with a different pattern.
    ++ state.pat_tbl_idx;

    CATCH(walk_pattern(pat_idx, &state), 0);
  }

 cleanup:
  return status;
}

static Status walk_pattern(UWORD pat_idx,
                           BuildState* state) {
  Status status = StatusOK;
  ModuleNonChip* nonchip = module_nonchip();
  Pattern* pat = &nonchip->patterns[pat_idx];

  ULONG select_samples = g.pat_select_samples[pat_idx];
  state->loop_idx = 0;
  state->loop_count = -1;

  // Begin from division specified by the previous jump, or 0 otherwise.
  state->div_idx = state->div_start_idx;
  state->div_start_idx = 0;

  while (state->div_idx < kDivsPerPattern) {
    PatternDivision* div = &pat->divisions[state->div_idx];

    CATCH(make_step(div, state, select_samples), 0);
    CATCH(handle_commands(div, state), 0);
  }

cleanup:
  return status;
}

Status make_step(PatternDivision* div,
                 BuildState* state,
                 ULONG select_samples) {
  Status status = StatusOK;
  UBYTE sample_in_step = 0;
  UBYTE step_color = 0;

  for (UWORD cmd_idx = 0; cmd_idx < 4; ++ cmd_idx) {
    PatternCommand* cmd = (PatternCommand*)&div->commands[cmd_idx];

    if (skip_command(cmd)) {
      continue;
    }

    static UBYTE last_sample[4] = {0};
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

  TrackStep step = {0};

  if (sample_in_step != 0) {
    step.sample = sample_in_step;
    step.color = step_color;

    static UWORD next_lane_lut[4][4] = {
      // Row indexed by last active lane, column indexed by random number.
      2, 1, 2, 3,
      1, 2, 2, 3,
      2, 1, 2, 3,
      3, 2, 2, 1,
    };

    UWORD random4 = random_mod4();

    if (state->active_contiguous_count == 1) {
      // Avoid lane change in contiguous segments until length is >= 2.
      random4 = 0;
    }

    if (state->active_contiguous_count && (state->last_active_lane != 2) && (random4 == 3)) {
      // Avoid left-right and right-left lane changes in contiguous segments.
      random4 = 0;
    }

    step.active_lane = next_lane_lut[state->last_active_lane][random4];

    if (step.active_lane != state->last_active_lane) {
      state->last_active_lane = step.active_lane;
      state->active_contiguous_count = 0;
    }

    ++ state->active_contiguous_count;
    ++ g.track_num_blocks;
  }
  else {
    state->active_contiguous_count = 0;
  }

  CATCH(vector_append(&g.track_steps, 1, &step), 0);

cleanup:
  return status;
}

Status handle_commands(PatternDivision* div,
                       BuildState* state) {
  Status status = StatusOK;
  ModuleNonChip* nonchip = module_nonchip();
  UBYTE delay = 0;
  UWORD next_div_idx = state->div_idx + 1;

  for (ULONG cmd_idx = 0; cmd_idx < 4; ++ cmd_idx) {
    UWORD effect = div->commands[cmd_idx].effect;

    switch (effect >> 8) {
    case kEffectPosJump:
      next_div_idx = kDivsPerPattern;
      state->pat_tbl_idx = effect & 0xFF;
      break;

    case kEffectPatBreak:
      state->div_start_idx = effect & 0xFF;

      // If division index exceeds kDivsPerPattern ptplayer jumps to first division.
      if (state->div_start_idx >= kDivsPerPattern) {
        state->div_start_idx = 0;
      }

      next_div_idx = kDivsPerPattern;
      break;

    case kEffectExtend:
      switch ((effect >> 4) & 0xF) {
      case kEffectExtPatDelay:
        delay = effect & 0xF;
        break;

      case kEffectExtPatLoop: {
        UWORD cmd_count = effect & 0xF;

        if (cmd_count == 0) {
          state->loop_idx = state->div_idx;
        }
        else if (state->loop_count == 0) {
          state->loop_count = -1;
        }
        else {
          if (state->loop_count == (UWORD)-1) {
            state->loop_count = cmd_count;
          }

          -- state->loop_count;
          next_div_idx = state->loop_idx;
        }

        break;
      }
      }

    case kEffectSetSpeed:
      if ((effect & 0xFF) == 0) {
        // Speed 0 indicates the end of the track.
        state->pat_tbl_idx = nonchip->header.pat_tbl_size;
        next_div_idx = kDivsPerPattern;
      }
    }
  }

  state->div_idx = next_div_idx;

  TrackStep delay_step = {0};

  for (UWORD i = 0; i < delay; ++ i) {
    CATCH(vector_append(&g.track_steps, 1, &delay_step), 0);
  }

cleanup:
  return status;
}

void track_free() {
  vector_free(&g.track_steps);
}

TrackStep* track_steps() {
  return (TrackStep*)vector_elems(&g.track_steps);
}

UWORD track_unpadded_length() {
  return (UWORD)vector_size(&g.track_steps) - kNumPaddingSteps;
}

UWORD track_num_blocks() {
  return g.track_num_blocks;
}
