#pragma once

#include <string_view>
#include <charconv>

template<typename T>
T to_num(std::string_view sv) {
    T value{};
    std::from_chars(sv.data(), sv.data() + sv.size(), value);
    return value;
}
