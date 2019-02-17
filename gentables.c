#include <math.h>
#include <stdio.h>

#define kFFTSizeLog2 9
#define kFFTSize (1 << (kFFTSizeLog2))

int main() {
  printf("#include <exec/types.h>\n\n");
  printf("#define kFFTSizeLog2 %d\n", kFFTSizeLog2);
  printf("#define kFFTSize (1 << (kFFTSizeLog2))\n\n", kFFTSizeLog2);
  printf("static WORD FFTSinLUT[kFFTSize - (kFFTSize / 4)] = {");

  // Fixed-point sin() lookup table for FFT (3/4 of whole cycle).
  for (int i = 0; i < (kFFTSize - (kFFTSize / 4)); ++ i) {
    double ang = (double)i / (double)kFFTSize * 2.0 * M_PI;
    double ang_sin = sin(ang);
    short ang_sin_fix = (short)round(ang_sin * 32767.0);

    if ((i & 7) == 0) {
      printf("\n ");
    }

    printf(" 0x%04hX,", ang_sin_fix);
  }

  printf("\n};\n\n");

  // Indices for reordering real data to FFT input.
  // Real to half-size complex FFT, and decimation in time.
  printf("static UWORD FFTReorder[kFFTSize] = {");

  for (int i = 0; i < kFFTSize; ++ i) {
    // Decimation in time: reverse bits of index.
    int rev = 0;

    for (int bit = 0; bit < kFFTSizeLog2; ++ bit) {
      if (i & (1 << bit)) {
        rev |= (1 << (kFFTSizeLog2 - 1 - bit));
      }
    }

    // Reorder even pairs of samples to bottom half, odd pairs to top half.
    int reorder = ((rev / 2) * 4) + (rev & 1);

    if ((i & 7) == 0) {
      printf("\n ");
    }

    printf(" 0x%03hX,", reorder);
  }

  printf("\n};\n");
}
