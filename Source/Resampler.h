#pragma once

#include <cmath>
#include <vector>
#include "RingBuffer.h"

/**
    Pull-based stereo resampler with a continuously adjustable ratio.

    ratio = input frames consumed per output frame produced
          = dawSampleRate / deviceSampleRate, times the drift corrector's
            multiplier.

    Uses 4-point Catmull-Rom interpolation with a persistent fractional
    phase — good enough quality for a program feed, and the ratio can be
    nudged by a few PPM at any time without artefacts.

    Consumer-thread only. No allocations after prepare().
*/
class Resampler
{
public:
    void prepare (double ratioIn)
    {
        scratchL.assign (chunkSize, 0.0f);
        scratchR.assign (chunkSize, 0.0f);
        setRatio (ratioIn);
        reset();
    }

    void setRatio (double r) noexcept
    {
        ratio = r > 0.001 ? r : 1.0;
    }

    void reset() noexcept
    {
        phase = 0.0;
        for (int i = 0; i < 4; ++i)
            histL[i] = histR[i] = 0.0f;
        scratchPos = scratchCount = 0;
        primed = false;
    }

    /** Renders up to numOut frames, pulling input from the ring buffer.
        Returns the number of frames actually rendered (fewer on underrun). */
    int render (RingBuffer& ring, float* outL, float* outR, int numOut) noexcept
    {
        for (int i = 0; i < numOut; ++i)
        {
            if (! primed)
            {
                // Fill the 4-tap history before producing the first sample.
                for (int t = 0; t < 4; ++t)
                    if (! popFrame (ring, histL[t], histR[t]))
                        return i;
                primed = true;
                phase = 0.0;
            }

            while (phase >= 1.0)
            {
                float l, r;
                if (! popFrame (ring, l, r))
                    return i;   // underrun — caller decides what to do

                histL[0] = histL[1]; histL[1] = histL[2]; histL[2] = histL[3]; histL[3] = l;
                histR[0] = histR[1]; histR[1] = histR[2]; histR[2] = histR[3]; histR[3] = r;
                phase -= 1.0;
            }

            const float t = static_cast<float> (phase);
            outL[i] = catmullRom (histL, t);
            outR[i] = catmullRom (histR, t);
            phase += ratio;
        }

        return numOut;
    }

    double getRatio() const noexcept { return ratio; }

private:
    bool popFrame (RingBuffer& ring, float& l, float& r) noexcept
    {
        if (scratchPos >= scratchCount)
        {
            scratchCount = ring.read (scratchL.data(), scratchR.data(), chunkSize);
            scratchPos = 0;
            if (scratchCount == 0)
                return false;
        }
        l = scratchL[static_cast<size_t> (scratchPos)];
        r = scratchR[static_cast<size_t> (scratchPos)];
        ++scratchPos;
        return true;
    }

    static float catmullRom (const float (&p)[4], float t) noexcept
    {
        // Interpolates between p[1] and p[2].
        const float t2 = t * t;
        const float t3 = t2 * t;
        return 0.5f * ((2.0f * p[1])
                     + (-p[0] + p[2]) * t
                     + (2.0f * p[0] - 5.0f * p[1] + 4.0f * p[2] - p[3]) * t2
                     + (-p[0] + 3.0f * p[1] - 3.0f * p[2] + p[3]) * t3);
    }

    // Kept small (a few ms) so frames parked in scratch don't skew the ring's
    // fill-level reading, which the drift servo uses as its error signal.
    static constexpr int chunkSize = 256;

    double ratio = 1.0;
    double phase = 0.0;
    float histL[4] {};
    float histR[4] {};
    bool primed = false;

    std::vector<float> scratchL, scratchR;
    int scratchPos = 0;
    int scratchCount = 0;
};
