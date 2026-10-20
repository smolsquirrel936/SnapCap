#include "core/HistoryStrip.h"

#include <algorithm>

namespace HistoryStrip {

Window window(int count, int selected, int maxVisible)
{
    if (count <= 0 || maxVisible <= 0)
        return {0, 0};

    const int len = std::min(count, maxVisible);
    if (len >= count)
        return {0, count};

    // Centre the window on the selection, then clamp into [0, count - len].
    int start = selected - len / 2;
    start = std::clamp(start, 0, count - len);
    return {start, len};
}

}  // namespace HistoryStrip
