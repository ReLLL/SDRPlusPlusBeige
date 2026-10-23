#pragma once
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>

namespace beige::iqreplay {
struct TuneContext {
    std::string receiver;
    uint64_t revision;
    double center, liveFrequency;
    bool operator==(const TuneContext& other) const {
        return receiver == other.receiver && revision == other.revision &&
            center == other.center && liveFrequency == other.liveFrequency;
    }
};

// Preview immediately, but defer a live retune until the double-click window
// closes. Center tuning would otherwise invalidate IQ before the second click.
class PendingTune {
public:
    void start(double frequency, const TuneContext& context, double now, double delay) {
        cancel();
        if (!std::isfinite(frequency) || !std::isfinite(now) || !std::isfinite(delay) || delay < 0) return;
        value = previewValue = frequency; saved = context; deadline = now + delay;
    }
    void cancel() { value.reset(); previewValue.reset(); }
    std::optional<double> preview(const TuneContext& context) const {
        return previewValue && context == saved && *previewValue != context.liveFrequency ? previewValue : std::nullopt;
    }
    std::optional<double> take(double now, bool held, const TuneContext& context) {
        if (!(context == saved)) cancel();
        if (!value || held || now < deadline) return std::nullopt;
        const auto result = value; value.reset(); return result;
    }
private:
    std::optional<double> value, previewValue;
    TuneContext saved{};
    double deadline = 0;
};

inline double arrowFrequency(double frequency, double snap, int direction, bool shift, bool alt) {
    if (shift) return frequency + direction * 100.0;
    const double step = alt ? snap * .1 : snap;
    return step > 0 ? std::round((frequency + direction * step) / step) * step : frequency;
}
}
