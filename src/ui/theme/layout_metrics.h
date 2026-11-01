#pragma once
#include <QtGlobal>
#include <algorithm>

namespace desktop::metrics {
inline constexpr int NavigationHeight = 51;
inline constexpr int MaximumContentWidth = 1320;
inline int ContentInset(int width, bool compact) {
    return compact ? 12 : std::max(18, (width - MaximumContentWidth) / 2);
}
}
