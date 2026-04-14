#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sndfile.h>
#include <assert.h>

/* =========================================================
   PC-FX ADPCM tables (must match decoder exactly)
   ========================================================= */

static const int StepSizes[49] =
{
 16,17,19,21,23,25,28,31,34,37,41,45,50,
 55,60,66,73,80,88,97,107,118,130,143,157,
 173,190,209,230,253,279,307,337,371,408,
 449,494,544,598,658,724,796,876,963,1060,
 1166,1282,1411,1552
};

static const int StepIndexDeltas[16] =
{
 -1,-1,-1,-1, 2,4,6,8,
 -1,-1,-1,-1, 2,4,6,8
};

/* =========================================================
   Encoder state (mono)
   ========================================================= */

static int32_t Predictor;
static int32_t StepIndex;

static void ResetADPCM()
{
    Predictor = 0;
    StepIndex = 0;
}

/* =========================================================
   Encode one PCM sample → ADPCM nibble
   (exact inverse of your pcfx-adpcm decoder)
   ========================================================= */

static inline uint8_t EncodeNibble(int16_t target)
{
    int best_nibble = 0;
    int32_t best_error = INT32_MAX;

    int32_t base = StepSizes[StepIndex];

    for (int n = 0; n < 16; n++)
    {
        int32_t delta = base * ((n & 7) + 1) * 2;
        if (n & 8)
            delta = -delta;

        int32_t pred = Predictor + delta;

        if (pred >  0x3FFF) pred =  0x3FFF;
        if (pred < -0x4000) pred = -0x4000;

        int32_t err = abs(target - pred);
        if (err < best_error)
        {
            best_error  = err;
            best_nibble = n;
        }
    }

    /* Commit the best nibble */
    int32_t delta = base * ((best_nibble & 7) + 1) * 2;
    if (best_nibble & 8)
        delta = -delta;

    Predictor += delta;
    if (Predictor >  0x3FFF) Predictor =  0x3FFF;
    if (Predictor < -0x4000) Predictor = -0x4000;

    StepIndex += StepIndexDeltas[best_nibble];
    if (StepIndex < 0)  StepIndex = 0;
    if (StepIndex > 48) StepIndex = 48;

    return (uint8_t)best_nibble;
}

/* =========================================================
   Main
   ========================================================= */

int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        printf("Usage: %s input.wav output.adpcm\n", argv[0]);
        return 1;
    }

    /* Open WAV */
    SF_INFO sfi{};
    SNDFILE* sf = sf_open(argv[1], SFM_READ, &sfi);
    if (!sf)
    {
        printf("Failed to open WAV: %s\n", argv[1]);
        return 1;
    }

    /* Sanity checks */
    assert(sfi.channels == 1);
    assert((sfi.format & SF_FORMAT_TYPEMASK) == SF_FORMAT_WAV);
    assert((sfi.format & SF_FORMAT_SUBMASK) == SF_FORMAT_PCM_16);

    FILE* out = fopen(argv[2], "wb");
    if (!out)
    {
        sf_close(sf);
        printf("Failed to open output\n");
        return 1;
    }

    ResetADPCM();

    int16_t sample;
    uint8_t outbyte = 0;
    int nibble_index = 0;

    while (sf_read_short(sf, &sample, 1) == 1)
    {
        uint8_t nib = EncodeNibble(sample);

        if ((nibble_index & 1) == 0)
        {
            /* low nibble first */
            outbyte = nib;
        }
        else
        {
            outbyte |= (nib << 4);
            fwrite(&outbyte, 1, 1, out);
        }

        nibble_index++;
    }

    sf_close(sf);
    fclose(out);

    printf("Encoded ADPCM: %s\n", argv[2]);
    return 0;
}
