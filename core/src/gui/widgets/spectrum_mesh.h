#pragma once

#include <algorithm>
#include <cstddef>

namespace beige {

// More detail near the viewer; power-of-two interval counts join without cracks.
inline std::size_t spectrumColumns(float width, std::size_t age) {
    std::size_t columns = 512;
    while (columns < 4096 && columns < width) { columns *= 2; }
    const std::size_t limit = age == 0 ? 4096 : age == 1 ? 2048 : age < 8 ? 1024 : 512;
    return (std::min)(columns, limit);
}

// Walk both frequency grids in order, sharing every row vertex between strips.
template<class Triangle>
void connectSpectrumRows(std::size_t front, std::size_t frontColumns,
                         std::size_t back, std::size_t backColumns, Triangle triangle) {
    std::size_t f = 0, b = 0;
    while (f < frontColumns || b < backColumns) {
        const auto nextFront = (f + 1) * backColumns;
        const auto nextBack = (b + 1) * frontColumns;
        if (f < frontColumns && (b == backColumns || nextFront <= nextBack)) {
            triangle(front + f, back + b, front + f + 1);
            ++f;
        }
        else {
            triangle(front + f, back + b, back + b + 1);
            ++b;
        }
    }
}

} // namespace beige
