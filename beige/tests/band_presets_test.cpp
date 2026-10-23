#include <gui/widgets/band_presets.h>
#include <cassert>
#include <set>
#include <string>

using namespace beige;

int main() {
    BandCategory category = BandCategory::Utility;
    assert(parseCategory("HAM", category) && category == BandCategory::Ham);
    assert(!parseCategory("unknown", category) && category == BandCategory::Ham);
    assert(!parseCategory("", category) && category == BandCategory::Ham);
    std::set<uint32_t> broadcastColors;
    for (const auto group : bandCategories) {
        assert(parseCategory(categoryLabel(group), category) && category == group);
        const auto bands = bandsFor(group);
        assert(bands.size > 0 && bands.size <= maxBandCount);
        std::set<std::string> labels;
        std::array<float, maxBandCount> widths{};
        for (std::size_t i = 0; i < bands.size; ++i) {
            const auto& b = bands[i];
            assert(labels.insert(b.label).second);
            assert(b.lowerHz > 0 && b.upperHz > b.lowerHz);
            assert(b.contains(b.centerHz()) && !b.contains(b.upperHz + 1));
            assert(*b.bestTime && *b.typicalUse && *b.typicalDistance);
            if (group == BandCategory::Broadcast) { broadcastColors.insert(b.color); }
            if (group == BandCategory::Utility) { assert(!broadcastColors.count(b.color)); }
            widths[i] = std::string(b.label).size() * 6 + 8;
        }
        // The fitted split must accommodate every entry, including the 17th band.
        const float fitted = twoRowWidth(widths.data(), bands.size, 3);
        float x = 0; unsigned rows = 1;
        for (std::size_t i = 0; i < bands.size; ++i) {
            if (x && x + widths[i] > fitted) { x = 0; ++rows; }
            assert(widths[i] <= fitted);
            x += widths[i] + 3;
        }
        assert(rows <= 2);
    }
    assert(bandsFor(BandCategory::Ham).data == hamBands.data());
    assert(hamBands.front().color == 0xAC2932 && hamBands.back().color == 0x75558F);
    assert(broadcastBands.size() == 17 && utilityBands.size() == 16);
    assert(broadcastBands[1].centerHz() == 1066500);
    assert(broadcastBands.back().centerHz() == 97750000);
    assert(utilityBands.back().centerHz() == 446100000);
    assert(twoRowWidth(nullptr, 0, 3) == 0);
    const float one[] = {20};
    assert(twoRowWidth(one, 1, 3) == 20);
}
