#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace stardraft {
struct DoodadFootprintUnion {
    int width = 0, height = 0;
    bool dirty = false;
    std::vector<int> difference;
    std::vector<uint8_t> cells;
    void reset(int w, int h) {
        width = std::max(0, w), height = std::max(0, h);
        difference.assign(size_t(width + 1) * (height + 1), 0);
        cells.assign(size_t(width) * height, 0);
        dirty = false;
    }
    void add(int x, int y, int w, int h) {
        const int left = std::clamp(x, 0, width), top = std::clamp(y, 0, height);
        const int right = std::clamp(x + w, 0, width), bottom = std::clamp(y + h, 0, height);
        if (left >= right || top >= bottom)
            return;
        const size_t stride = size_t(width + 1);
        ++difference[size_t(top) * stride + left];
        --difference[size_t(top) * stride + right];
        --difference[size_t(bottom) * stride + left];
        ++difference[size_t(bottom) * stride + right];
        dirty = true;
    }
    void update() {
        if (!dirty)
            return;
        std::vector<int> above(size_t(width), 0);
        const size_t stride = size_t(width + 1);
        for (int y = 0; y < height; ++y) {
            int row = 0;
            for (int x = 0; x < width; ++x) {
                row += difference[size_t(y) * stride + x];
                above[size_t(x)] += row;
                cells[size_t(y) * width + x] = above[size_t(x)] > 0;
            }
        }
        dirty = false;
    }
};
} // namespace stardraft
