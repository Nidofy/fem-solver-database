#pragma once
#include <algorithm>
#include <functional>
#include <span>
#include <stdexcept>
#include <vector>

namespace sdb::flat_detail {
// Preserve the wire order. Already ordered input needs no scratch allocation.
template<class T>
std::span<const T> unique_sorted(std::span<const T> input, std::vector<T> &scratch, const char *error) {
    if (std::adjacent_find(input.begin(), input.end(), std::greater_equal<T>{}) == input.end())
        return input;
    scratch.assign(input.begin(), input.end());
    std::sort(scratch.begin(), scratch.end());
    if (std::adjacent_find(scratch.begin(), scratch.end()) != scratch.end())
        throw std::invalid_argument(error);
    return scratch;
}
} // namespace sdb::flat_detail
