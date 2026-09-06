#include "TestFramework.h"
#include "Resampler.h"

#include <cmath>

namespace
{
    void fillSine (std::vector<float>& v, double freq, double rate, double& phase)
    {
        for (auto& s : v)
        {
            s = (float) std::sin (phase);
            phase += 2.0 * 3.14159265358979323846 * freq / rate;
        }
    }

    // Count zero crossings to estimate frequency.
    int zeroCrossings (const std::vector<float>& v)
    {
        int count = 0;
        for (size_t i = 1; i < v.size(); ++i)
            if ((v[i - 1] < 0.0f) != (v[i] < 0.0f))
                ++count;
        return count;
    }
}

TEST_CASE (resampler_unityRatioPassesThrough)
{
    RingBuffer ring;
    ring.prepare (65536);
    Resampler rs;
    rs.prepare (1.0);

    std::vector<float> in (8192);
    double phase = 0.0;
    fillSine (in, 1000.0, 48000.0, phase);
    ring.write (in.data(), in.data(), (int) in.size());

    std::vector<float> outL (4096), outR (4096);
    const int rendered = rs.render (ring, outL.data(), outR.data(), 4096);
    EXPECT (rendered == 4096);

    for (int i = 0; i < rendered; ++i)
    {
        EXPECT (std::isfinite (outL[i]));
        EXPECT (std::abs (outL[i]) <= 1.01f);
        EXPECT (outL[i] == outR[i]);
    }

    // Frequency preserved: 1 kHz at 48 kHz over 4096 samples ≈ 170 crossings.
    std::vector<float> trimmed (outL.begin() + 16, outL.end());
    const int zc = zeroCrossings (trimmed);
    EXPECT (zc > 160 && zc < 180);
}

TEST_CASE (resampler_downRate48to44)
{
    RingBuffer ring;
    ring.prepare (131072);
    Resampler rs;
    const double ratio = 48000.0 / 44100.0;
    rs.prepare (ratio);

    std::vector<float> in (48000);   // 1 second @ 48k
    double phase = 0.0;
    fillSine (in, 1000.0, 48000.0, phase);
    ring.write (in.data(), in.data(), (int) in.size());

    std::vector<float> outL (44100), outR (44100);
    const int rendered = rs.render (ring, outL.data(), outR.data(), 44100);

    // Should render nearly a full second at 44.1k (minus interpolation priming).
    EXPECT (rendered > 44000);

    std::vector<float> valid (outL.begin(), outL.begin() + rendered);
    const int zc = zeroCrossings (valid);
    // 1 kHz for ~1s -> ~2000 crossings regardless of output rate.
    EXPECT (zc > 1950 && zc < 2050);

    for (int i = 0; i < rendered; ++i)
        EXPECT (std::isfinite (outL[i]) && std::abs (outL[i]) <= 1.05f);
}

TEST_CASE (resampler_smallPpmNudgeIsContinuous)
{
    RingBuffer ring;
    ring.prepare (65536);
    Resampler rs;
    rs.prepare (1.0);

    std::vector<float> in (16384);
    double phase = 0.0;
    fillSine (in, 440.0, 48000.0, phase);
    ring.write (in.data(), in.data(), (int) in.size());

    std::vector<float> outL (512), outR (512);
    float last = 0.0f;
    bool first = true;

    // Nudge the ratio every block; output must stay continuous (no jumps).
    for (int block = 0; block < 20; ++block)
    {
        rs.setRatio (1.0 + (block - 10) * 5.0e-6);
        const int n = rs.render (ring, outL.data(), outR.data(), 512);
        EXPECT (n == 512);

        for (int i = 0; i < n; ++i)
        {
            if (! first)
            {
                // 440 Hz at 48k moves at most ~0.058 per sample; allow slack.
                EXPECT (std::abs (outL[i] - last) < 0.1f);
            }
            last = outL[i];
            first = false;
        }
    }
}

TEST_CASE (resampler_underrunReturnsShortCount)
{
    RingBuffer ring;
    ring.prepare (1024);
    Resampler rs;
    rs.prepare (1.0);

    std::vector<float> in (100, 0.5f);
    ring.write (in.data(), in.data(), 100);

    std::vector<float> outL (500), outR (500);
    const int rendered = rs.render (ring, outL.data(), outR.data(), 500);
    EXPECT (rendered < 100);   // 4 frames used for priming
    EXPECT (rendered > 90);
}
