#include "track.h"
#include "build/tables.h"
#include "module.h"

#include <proto/exec.h>
#include <stdio.h> // FIXME
#include <stdlib.h> // FIXME

static struct {
  TrackStep* track_steps;
  ULONG track_steps_size;
  ULONG track_length;
  ULONG pat_select_samples[kNumPatternsMax];
  TrackScore scores[kNumPatternsMax];
} g;

#define kTrackDataAllocGranule 0x1000
#define kTrackDataLengthGranule (kTrackDataAllocGranule / sizeof(TrackStep))
#define kEffectPosJump     0xB
#define kEffectPatBreak    0xD
#define kEffectExtend      0xE
#define kEffectSetSpeed    0xF
#define kEffectExtPatLoop  0x6
#define kEffectExtPatDelay 0xE

//#define DEBUG_PATTERN 10

static Status track_realloc() {
  Status status = StatusOK;

  ULONG new_size = g.track_steps_size + kTrackDataAllocGranule;
  TrackStep* new_data;
  CHECK(new_data = AllocMem(new_size, 0));

  if (g.track_steps) {
    CopyMemQuick(g.track_steps, new_data, g.track_steps_size);
    track_free();
  }

  g.track_steps = new_data;
  g.track_steps_size = new_size;

cleanup:
  return status;
}

static Status pad_visible() {
  Status status = StatusOK;

  TrackStep step = (TrackStep){ 0 };
  step.speed = 0;

  for (UWORD i = 0; i < kNumVisibleSteps; ++ i) {
    // Reallocate track data if song length exceeds the last granule.
    if ((g.track_length & (kTrackDataLengthGranule - 1)) == 0) {
      CHECK(track_realloc());
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

WORD FIX_MPY(WORD a, WORD b) {
	/* shift right one less bit (i.e. 15-1) */
	LONG c = (a * b) >> 14;
	/* last bit shifted out = rounding-bit */
	b = c & 0x01;
	/* last shift + rounding bit */
	a = (c >> 1) + b;
	return a;
}

static Status analyze_samples() {
  Status status = StatusOK;
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

#if 0
    char n[20];
    sprintf(n, "sample%lu.dat", (ULONG)(samp_idx + 1));
    FILE* f = fopen(n, "wb");
    for (UWORD data_idx = 0; data_idx < kFFTSize*2; ++ data_idx) {
      char v = 0;
      if (data_idx < samp_size_b) {
        v = samples[data_idx];
      }
      fputc(v, f);
    }
    fclose(f);
#endif

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

          WORD tr = FIX_MPY(wr, fft_data[kFFTReal][j]) - FIX_MPY(wi, fft_data[kFFTImag][j]);
          WORD ti = FIX_MPY(wr, fft_data[kFFTImag][j]) + FIX_MPY(wi, fft_data[kFFTReal][j]);
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

#if 1
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

    if (samp_idx == (7 - 1)) {
      FILE* f = fopen("sample.bin", "wb");
      for (UWORD i = 0; i < kFFTSize / 2; ++ i) {
        LONG ampl_sqr =
          ((fft_data[kFFTReal][i] * fft_data[kFFTReal][i]) >> 1) +
          ((fft_data[kFFTImag][i] * fft_data[kFFTImag][i]) >> 1);

        // FIXME: check for overflow
        UWORD tmp = (UWORD)(ampl_sqr >> 15);
        fwrite(&tmp, 2, 1, f);
      }
      fclose(f);
    }

    samp_dom_freq[samp_idx] = max_freq_idx;
#else
    static ULONG ampls_sqr[kFFTSize / 2];
    ULONG total_ampl_sqr = 0;

    for (UWORD i = 1; i < kFFTSize / 2; ++ i) {
      ULONG ampl_sqr =
        ((fft_data[kFFTReal][i] * fft_data[kFFTReal][i]) >> 8) +
        ((fft_data[kFFTImag][i] * fft_data[kFFTImag][i]) >> 8);

      ampls_sqr[i] = ampl_sqr;
      total_ampl_sqr += ampl_sqr;
    }

    ULONG median_ampl_sqr = total_ampl_sqr / 2;
    UWORD median_freq_idx = 0;
    total_ampl_sqr = 0;

    for (median_freq_idx = 1; median_freq_idx < kFFTSize / 2; ++ median_freq_idx) {
      total_ampl_sqr += ampls_sqr[median_freq_idx];

      if (total_ampl_sqr > median_ampl_sqr) {
        break;
      }
    }

    samp_dom_freq[samp_idx] = median_freq_idx;
#endif

    samples += samp_size_b;

#if DEBUG_PATTERN
    printf("sample %lu dom_freq %lu\n", (ULONG)(samp_idx + 1), (ULONG)max_freq_idx);
#endif
  }

cleanup:
  return status;
}

static UWORD prng() {
  // LFSR PRNG (http://codebase64.org/doku.php?id=base:small_fast_16-bit_prng)
#define kLFSRMagic 0xC2DF
  static UWORD seed = 0;

  if (seed == 0) {
    seed ^= kLFSRMagic;
  }
  else if (seed == 0x8000) {
    seed = 0;
  }
  else {
    UWORD carry = seed & 0x8000;
    seed <<= 1;

    if (carry) {
      seed ^= kLFSRMagic;
    }
  }

  return seed;
}

// 0 = treat command as new sample playback
// 1 = skip command, sample does not restart
// 2 = FIXME

static UBYTE skip_commands[0x10] = {
  0, /* Normal/arpeggio */
  0, /* Slide up */
  0, /* Slide down */
  0, /* Slide to note */
  0, /* Vibrato */
  0, /* Continue slide to note, volume slide */
  0, /* Continue vibrato, volume slide */
  1, /* Tremolo */
  2, /* Unused */
  0, /* Set sample offset */
  0, /* Volume slide */
  1, /* Position jump */
  3, /* Set volume */
  1, /* Pattern break */
  2, /* Extended */
  0, /* Set speed */
};

static UBYTE skip_commands_ext[0x10] = {
  0, /* Set filter on/off */
  2, /* Fineslide up */
  2, /* Fineslide down */
  2, /* Set glissando on/off */
  2, /* Set vibrato waveform */
  2, /* Set finetune value */
  2, /* Loop pattern */
  2, /* Set tremolo waveform */
  2, /* Unused */
  2, /* Retrigger sample */
  0, /* Fine volume slide up */
  0, /* Fine volume slide down */
  2, /* Cut sample */
  0, /* Delay sample */
  2, /* Delay pattern */
  2, /* Invert loop*/
};

static BOOL skip_command(PatternCommand* cmd) {
  if (cmd->parameter == 0) {
    return TRUE;
  }

  UWORD effect_major = cmd->effect >> 8;

  if (effect_major != 0xE) {
    UBYTE ignore = skip_commands[effect_major];

    if (ignore == 2) {
      /* printf("effect %lX\n", cmd->effect); */
      exit(1);
    }

    if (ignore == 1) {
      return TRUE;
    }

    // FIXME: Make this part of the score!
    if (ignore == 3 && (cmd->effect & 0xFF) < 32) {
      return TRUE;
    }

    return FALSE;
  }
  else {
    UWORD effect_ext = (cmd->effect >> 4) & 0xF;
    UBYTE ignore = skip_commands_ext[effect_ext];

    if (ignore == 2) {
      /* printf("effect %lX\n", cmd->effect); */
      exit(1);
    }

    return (ignore == 1);
  }
}

static Status select_samples() {
  Status status = StatusOK;
  ModuleNonChip* nonchip = module_get_nonchip();
  UWORD num_patterns = module_get_num_patterns();

  CHECK(analyze_samples());

  for (UWORD pat_idx = 0; pat_idx < num_patterns; ++ pat_idx) {
    // Count active samples in the pattern.
    static UBYTE sample_count[0x20];
    static ULONG sample_periods[0x20];

    for (UWORD i = 0; i < 0x20; ++ i) {
      sample_count[i] = 0;
      sample_periods[i] = 0;
      g.scores[pat_idx].per_sample[i].in_pattern = FALSE;
      g.scores[pat_idx].per_sample[i].pitch = 0;
      g.scores[pat_idx].per_sample[i].count = 0;
      g.scores[pat_idx].selected_sample = 0;
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

        if (sample) {
          ++ sample_count[sample];
          sample_periods[sample] += cmd->parameter;
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
        UWORD score_pitch = ABS(pitch - kPitchTarget);

#if DEBUG_PATTERN
        if (pat_idx == DEBUG_PATTERN) {
          printf("sample %lu pitch %ld count %lu\n", (ULONG)i, (LONG)pitch, (ULONG)sample_count[i]);
        }
#endif
        g.scores[pat_idx].per_sample[i].in_pattern = TRUE;
        g.scores[pat_idx].per_sample[i].pitch = score_pitch;
        g.scores[pat_idx].per_sample[i].count = score_count;

        ULONG score = (score_count * kCountWeight) + (score_pitch * kPitchWeight);

        if (score < best_score) {
          best_score = score;
          best_score_idx = i;
        }
      }
    }

    g.pat_select_samples[pat_idx] = 1UL << best_score_idx;
    g.scores[pat_idx].selected_sample = best_score_idx;
  }

cleanup:
  return status;
}

static Status walk_pattern(UWORD pat_idx,
                           UWORD* pat_tbl_idx,
                           UWORD* div_start_idx,
                           UWORD* last_active_lane) {
  Status status = StatusOK;
  ModuleNonChip* nonchip = module_get_nonchip();

  // Walk through the pattern, starting from the first division.
  Pattern* pat = &nonchip->patterns[pat_idx];
  ULONG select_samples = g.pat_select_samples[pat_idx];

  UWORD div_idx = *div_start_idx;
  *div_start_idx = 0;

  while (div_idx < kDivsPerPat) {
    PatternDivision* div = &pat->divisions[div_idx];

    // Reallocate track data if song length exceeds the last granule.
    if ((g.track_length & (kTrackDataLengthGranule - 1)) == 0) {
      CHECK(track_realloc());
    }

    UBYTE sample_in_step = 0;

    for (UWORD cmd_idx = 0; cmd_idx < 4; ++ cmd_idx) {
      PatternCommand* cmd = (PatternCommand*)&div->commands[cmd_idx];

      if (skip_command(cmd)) {
        continue;
      }

      UBYTE sample = (cmd->sample_hi << 4) | cmd->sample_lo;

      if (sample && (select_samples & (1UL << sample))) {
        sample_in_step = sample;
      }
    }

    TrackStep* step = &g.track_steps[g.track_length];
    step->collected = 0;

    if (sample_in_step != 0) {
      step->sample = sample_in_step;

      if (*last_active_lane != 0) {
        step->active_lane = *last_active_lane;
        *last_active_lane = 0;
      }
      else {
        step->active_lane = 1 + (prng() % 3); // FIXME: optimize
        *last_active_lane = step->active_lane;
      }
    }
    else {
      step->active_lane = 0;
      step->sample = 0;
      *last_active_lane = 0;
    }

#if 0
    // FIXME: compiler bug?
    if (block_in_step && g.track_length < 1) {
      printf("%lu\n", (ULONG)step->active_lane);
    }
#endif

    // Handle commands which change the next division step or speed.
    UBYTE speed = 0;

    for (ULONG cmd_idx = 0; cmd_idx < 4; ++ cmd_idx) {
      UWORD effect = div->commands[cmd_idx].effect;

      switch (effect >> 8) {
      case kEffectPosJump:
        div_idx = kDivsPerPat;
        *pat_tbl_idx = effect & 0xFF;
        break;

      case kEffectPatBreak:
        *div_start_idx = effect & 0xFF;

        // If division index exceeds kDivsPerPat ptplayer jumps to first division.
        if (*div_start_idx >= kDivsPerPat) {
          *div_start_idx = 0;
        }

        div_idx = kDivsPerPat;
        break;

      case kEffectSetSpeed:
        speed = effect & 0xFF;
        break;

      case kEffectExtend:
        switch (effect >> 4) {
        case kEffectExtPatLoop:
        case kEffectExtPatDelay:
          /* printf("Unsupported extension effect %lu\n", (ULONG)(effect & 0xF0)); */
          CHECK(FALSE);
        }
      }
    }

    step->speed = speed;

    ++ g.track_length;
    ++ div_idx;
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
  UWORD div_start_idx = 0;
  UWORD last_active_lane = 0;

  for (UWORD pat_tbl_idx = kSongStartPos; pat_tbl_idx < nonchip->header.pat_tbl_size; ) {
    UWORD pat_idx = nonchip->header.pat_tbl[pat_tbl_idx];

    // Stop if we looped in the pattern table.
    if (pat_tbl_visited[pat_tbl_idx]) {
      break;
    }

    pat_tbl_visited[pat_tbl_idx] = 1;
    pat_tbl_idx += 1;

    CHECK(walk_pattern(pat_idx, &pat_tbl_idx, &div_start_idx, &last_active_lane));
  }

cleanup:
  return status;
}

Status track_build() {
  Status status = StatusOK;

  CHECK(g.track_steps == NULL);
  g.track_length = 0;

  // Select samples in each pattern corresponding to collectibles.
  CHECK(select_samples());

  // Start with empty steps covering the visible track.
  // Match the speed to the beginning of the song. // FIXME
  CHECK(pad_visible());

  // Create steps for every division in the song, in playback order.
  CHECK(walk_song_table());

  // Finish with empty steps covering the visible track.
  CHECK(pad_visible());

cleanup:
  if (status == StatusError) {
    track_free();
  }

  return status;
}

VOID track_free() {
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

TrackScore* track_get_scores() {
  return g.scores;
}
