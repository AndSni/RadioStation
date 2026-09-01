#include "audio/RingBuffer.h"

#include <QTest>
#include <thread>
#include <vector>

using namespace radio::audio;

class RingBufferTest : public QObject {
    Q_OBJECT

private slots:
    void writeThenReadRoundTrips();
    void partialWriteWhenFull();
    void partialReadWhenEmpty();
    void wraparoundPreservesOrder();
    void concurrentProducerConsumerNeverCorrupts();
};

void RingBufferTest::writeThenReadRoundTrips()
{
    RingBuffer rb(16, 2); // stereo, capacity rounds up to next pow2 already exact

    const std::vector<float> in = { 1.0f, -1.0f, 2.0f, -2.0f, 3.0f, -3.0f };
    QCOMPARE(rb.write(in.data(), 3), size_t(3));
    QCOMPARE(rb.availableToRead(), size_t(3));

    std::vector<float> out(6, 0.0f);
    QCOMPARE(rb.read(out.data(), 3), size_t(3));
    QCOMPARE(out, in);
    QCOMPARE(rb.availableToRead(), size_t(0));
}

void RingBufferTest::partialWriteWhenFull()
{
    RingBuffer rb(4, 1); // mono, 4 frames capacity

    std::vector<float> in(4, 1.0f);
    QCOMPARE(rb.write(in.data(), 4), size_t(4)); // fills exactly

    std::vector<float> more(2, 2.0f);
    QCOMPARE(rb.write(more.data(), 2), size_t(0)); // full — nothing fits
}

void RingBufferTest::partialReadWhenEmpty()
{
    RingBuffer rb(4, 1);

    std::vector<float> out(4, -1.0f);
    QCOMPARE(rb.read(out.data(), 4), size_t(0)); // nothing written yet
}

void RingBufferTest::wraparoundPreservesOrder()
{
    RingBuffer rb(4, 1); // small capacity to force wraparound quickly

    // Fill, drain half, refill past the wrap boundary, and confirm frame
    // order survives the internal index wraparound.
    std::vector<float> a = { 1, 2, 3, 4 };
    QCOMPARE(rb.write(a.data(), 4), size_t(4));

    std::vector<float> drained(2);
    QCOMPARE(rb.read(drained.data(), 2), size_t(2));
    QCOMPARE(drained[0], 1.0f);
    QCOMPARE(drained[1], 2.0f);

    std::vector<float> b = { 5, 6 };
    QCOMPARE(rb.write(b.data(), 2), size_t(2)); // wraps past the end of the backing store

    std::vector<float> rest(4);
    QCOMPARE(rb.read(rest.data(), 4), size_t(4));
    QCOMPARE(rest[0], 3.0f);
    QCOMPARE(rest[1], 4.0f);
    QCOMPARE(rest[2], 5.0f);
    QCOMPARE(rest[3], 6.0f);
}

void RingBufferTest::concurrentProducerConsumerNeverCorrupts()
{
    // Real producer/consumer threads writing/reading a known, verifiable
    // sequence — the actual scenario this class exists for (GStreamer's
    // thread producing, a real-time audio thread consuming), not just
    // single-threaded logic.
    RingBuffer rb(256, 1);
    constexpr int totalFrames = 200000;

    std::vector<float> received;
    received.reserve(totalFrames);

    std::thread producer([&] {
        int written = 0;
        while (written < totalFrames) {
            const float value = static_cast<float>(written);
            size_t n = rb.write(&value, 1);
            written += static_cast<int>(n);
        }
    });

    std::thread consumer([&] {
        while (static_cast<int>(received.size()) < totalFrames) {
            float value = 0.0f;
            if (rb.read(&value, 1) == 1)
                received.push_back(value);
        }
    });

    producer.join();
    consumer.join();

    QCOMPARE(static_cast<int>(received.size()), totalFrames);
    for (int i = 0; i < totalFrames; ++i)
        QCOMPARE(received[static_cast<size_t>(i)], static_cast<float>(i));
}

QTEST_MAIN(RingBufferTest)
#include "RingBufferTest.moc"
