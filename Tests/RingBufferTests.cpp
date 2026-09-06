#include "TestFramework.h"
#include "RingBuffer.h"

#include <thread>
#include <cmath>

TEST_CASE (ringBuffer_basicWriteRead)
{
    RingBuffer rb;
    rb.prepare (1024);
    EXPECT (rb.getCapacity() == 1024);
    EXPECT (rb.getNumReady() == 0);

    float l[16], r[16];
    for (int i = 0; i < 16; ++i) { l[i] = (float) i; r[i] = (float) -i; }

    EXPECT (rb.write (l, r, 16) == 16);
    EXPECT (rb.getNumReady() == 16);

    float ol[16] {}, orr[16] {};
    EXPECT (rb.read (ol, orr, 16) == 16);
    EXPECT (rb.getNumReady() == 0);

    for (int i = 0; i < 16; ++i)
    {
        EXPECT (ol[i] == (float) i);
        EXPECT (orr[i] == (float) -i);
    }
}

TEST_CASE (ringBuffer_fullBufferDropsExcess)
{
    RingBuffer rb;
    rb.prepare (64);   // rounds to 64

    std::vector<float> data (100, 1.0f);
    const int written = rb.write (data.data(), data.data(), 100);
    EXPECT (written == 64);
    EXPECT (rb.getNumReady() == 64);
    EXPECT (rb.write (data.data(), data.data(), 10) == 0);
}

TEST_CASE (ringBuffer_wrapAround)
{
    RingBuffer rb;
    rb.prepare (64);

    float in[48], ol[48], orr[48];
    float counter = 0.0f;

    // Push/pop repeatedly so the indices wrap many times; verify sequence.
    float expected = 0.0f;
    for (int iter = 0; iter < 100; ++iter)
    {
        for (int i = 0; i < 48; ++i) in[i] = counter++;
        EXPECT (rb.write (in, in, 48) == 48);
        EXPECT (rb.read (ol, orr, 48) == 48);
        for (int i = 0; i < 48; ++i)
        {
            EXPECT (ol[i] == expected);
            EXPECT (orr[i] == expected);
            ++expected;
        }
    }
}

TEST_CASE (ringBuffer_discardAndFlush)
{
    RingBuffer rb;
    rb.prepare (256);
    std::vector<float> data (100, 1.0f);
    rb.write (data.data(), data.data(), 100);

    EXPECT (rb.discard (30) == 30);
    EXPECT (rb.getNumReady() == 70);
    rb.flushFromConsumer();
    EXPECT (rb.getNumReady() == 0);
}

TEST_CASE (ringBuffer_concurrentStress)
{
    RingBuffer rb;
    rb.prepare (1 << 12);

    constexpr long long total = 2'000'000;
    std::atomic<bool> failed { false };

    std::thread producer ([&]
    {
        float block[128];
        long long sent = 0;
        while (sent < total)
        {
            const int n = (int) std::min<long long> (128, total - sent);
            for (int i = 0; i < n; ++i)
                block[i] = (float) ((sent + i) % 1000000);

            // Only write what fits — count what was accepted.
            const int written = rb.write (block, block, n);
            sent += written;
            if (written == 0)
                std::this_thread::yield();
        }
    });

    std::thread consumer ([&]
    {
        float ol[97], orr[97];   // deliberately mismatched block size
        long long received = 0;
        while (received < total)
        {
            const int n = rb.read (ol, orr, 97);
            for (int i = 0; i < n; ++i)
            {
                const float expected = (float) ((received + i) % 1000000);
                if (ol[i] != expected || orr[i] != expected)
                {
                    failed.store (true);
                    return;
                }
            }
            received += n;
            if (n == 0)
                std::this_thread::yield();
        }
    });

    producer.join();
    consumer.join();
    EXPECT (! failed.load());
    EXPECT (rb.getNumReady() == 0);
}
