#include <iq_replay/framer.h>
#include <iq_replay/rows.h>
#include <iq_replay/retention.h>
#include <iq_replay/navigation.h>
#include <cassert>
#include <limits>
#include <random>
#include <iostream>

using namespace beige::iqreplay;

static Stamp stamp(uint64_t sample, uint64_t epoch = 7) { return {epoch, sample, 800000, 7100000, 100}; }

int main() {
    const TuneContext context{"Radio", 8, 7100000, 7120000};
    PendingTune click;
    click.start(7080000, context, 1, .3);
    assert(click.preview(context) == 7080000);
    assert(!click.take(1.1, false, context));
    assert(!click.take(1.4, true, context));
    assert(click.take(1.4, false, context) == 7080000);
    assert(click.preview(context) == 7080000); // Avoid a one-frame indicator flash before commit.
    assert(!click.take(1.5, false, context));
    click.start(7080000, context, 1, .3);
    click.cancel(); // A double-click or deliberate drag supersedes single tune.
    assert(!click.take(2, false, context));
    for (TuneContext changed : {TuneContext{"Other", 8, 7100000, 7120000},
             TuneContext{"Radio", 9, 7100000, 7120000}, TuneContext{"Radio", 8, 7200000, 7120000},
             TuneContext{"Radio", 8, 7100000, 7120100}}) {
        click.start(7080000, context, 1, .3);
        assert(!click.preview(changed) && !click.take(2, false, changed));
    }
    assert(arrowFrequency(7123456, 1000, 1, true, false) == 7123556);
    assert(arrowFrequency(7123456, 1000, -1, true, false) == 7123356);
    assert(arrowFrequency(7123456.5, 5000, 1, true, true) == 7123556.5);
    assert(arrowFrequency(7120000, 1000, 1, false, false) == 7121000);
    assert(arrowFrequency(7120000, 1000, -1, false, true) == 7119900);
    const auto plan = Retention::plan(800000, 20, 400);
    assert(plan.capacity == 17920000 && plan.seconds == 22.4);
    assert(!Retention::plan(800000, 20, 401).replace(plan.capacity));
    assert(Retention::plan(800000, 10, 400).replace(plan.capacity));
    assert(Retention::plan(800000, 60, 400).replace(plan.capacity));
    for (double rate : {0., -1., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        bool bad = false; try { Retention::plan(rate, 20, 400); } catch (const std::invalid_argument&) { bad = true; } assert(bad);
        bad = false; try { Retention::plan(800000, rate, 400); } catch (const std::invalid_argument&) { bad = true; } assert(bad);
    }
    StoreState bounds; bounds.ready = true; bounds.epoch = 7; bounds.head = bounds.captured = 300; bounds.capacity = 100;
    bounds.rate = 800000; bounds.center = 7100000;
    assert(retainedPoint(bounds, stamp(200), 6700000));
    assert(retainedPoint(bounds, stamp(299), 7499999.999));
    assert(!retainedPoint(bounds, stamp(199), 7100000));
    assert(!retainedPoint(bounds, stamp(300), 7100000));
    assert(!retainedPoint(bounds, stamp(200, 8), 7100000));
    assert(!retainedPoint(bounds, stamp(200), 6699999.999));
    assert(!retainedPoint(bounds, stamp(200), 7500000));
    assert(!retainedPoint(bounds, stamp(200), std::numeric_limits<double>::quiet_NaN()));
    // Arbitrary USB packet boundaries must not alter FFT contents or timestamps.
    for (size_t keep : {1, 3, 32, 16384}) {
        for (size_t skip : {0, 1, 17, 103}) {
            const size_t interval = keep + skip, total = interval * 20 + keep / 2;
            std::vector<dsp::complex_t> input(total);
            for (size_t i = 0; i < total; ++i) input[i] = {float(i), -float(i)};
            Framer framer(keep, interval);
            size_t frames = 0, offset = 0;
            std::mt19937 random(123);
            while (offset < total) {
                const size_t count = std::min(total - offset, size_t(1 + random() % 1103));
                framer.push(input.data() + offset, count, stamp(offset), [&](auto* data, int n, const Stamp& at) {
                    assert(size_t(n) == keep && at.epoch == 7 && at.sample == frames * interval + keep / 2);
                    for (int j = 0; j < n; ++j) assert(data[j].re == float(frames * interval + size_t(j)));
                    ++frames;
                });
                offset += count;
            }
            assert(frames == (total < keep ? 0 : (total - keep) / interval + 1));
        }
    }
    // Discontinuities and new generations discard partially assembled old windows.
    Framer changed(4, 7);
    dsp::complex_t input[10]{};
    int frames = 0;
    auto cb = [&](auto*, int, const Stamp& at) { ++frames; assert(at.epoch == 8 && at.sample == 102); };
    changed.push(input, 3, stamp(0), cb);
    changed.push(input, 2, stamp(99), cb);
    changed.push(input, 4, stamp(100, 8), cb);
    assert(frames == 1);
    bool rejected = false;
    try { Framer invalid(10, 9); } catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);

    // Exercise thousands of ring wraps, resize rotations, and fractional clicks.
    Rows rows;
    for (int height : {1, 2, 17, 431}) {
        rows.resize(-1, 0, height);
        int head = 0, filled = 0;
        for (int i = 0; i < height * 9; ++i) {
            head = (head + height - 1) % height;
            rows.put(head, height, stamp(uint64_t(i)));
            filled = std::min(filled + 1, height);
            const auto display = rows.snapshot(head, filled, height);
            for (int j = 0; j < filled; ++j) {
                assert(Rows::point(display, j).sample == uint64_t(i - j));
                assert(Rows::point(display, j + .999999).sample == uint64_t(i - j));
            }
            assert(!Rows::point(display, -.000001));
            assert(!Rows::point(display, height));
            assert(!Rows::point(display, height + .000001));
            assert(!Rows::point(display, std::numeric_limits<double>::quiet_NaN()));
            assert(!Rows::point(display, std::numeric_limits<double>::infinity()));
            // Displayed pixels remain mapped to the same capture even after new FFTs.
            auto frozen = display;
            rows.put(head, height, stamp(999999));
            assert(Rows::point(frozen, 0).sample == uint64_t(i));
            rows.put(head, height, stamp(uint64_t(i)));
        }
        const auto old = rows.snapshot(head, filled, height);
        rows.resize(head, height, height + 3);
        auto grown = rows.snapshot(0, filled, height + 3);
        for (int i = 0; i < height; ++i) assert(grown[i].sample == old[i].sample);
        assert(!grown[height]);
        rows.resize(0, height + 3, 1);
        assert(rows.snapshot(0, 1, 1)[0].sample == old[0].sample);
        rows.clear();
        assert(rows.snapshot(0, 1, 1).empty());
    }
    rows.resize(0, 1, -1);
    rows.put(-1, 10, stamp(1)); rows.put(10, 10, stamp(1));
    assert(rows.snapshot(-1, 1, 10).empty());
    std::cout << "IQ FFT timing, row mapping, resize and boundary tests passed\n";
}
