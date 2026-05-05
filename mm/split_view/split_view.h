#pragma once

#include <string_view>
#include <iostream>

struct SplitView {
    std::string_view str;

    struct Iterator {
        std::string_view remaining;

        std::string_view current;

        Iterator(std::string_view s) : remaining(s) {
            ++(*this); // инициализируем первый элемент
        }

        Iterator() : remaining("") {}

        std::string_view operator*() const {
            return current;
        }

        Iterator& operator++() {
            if (remaining.empty()) {
                current = {};
                return *this;
            }

            size_t pos = remaining.find(',');

            if (pos == std::string_view::npos) {
                current = remaining;
                remaining = {};
            } else {
                current = remaining.substr(0, pos);
                remaining.remove_prefix(pos + 1);
            }

            return *this;
        }

        bool operator!=(const Iterator& other) const {
            return !remaining.empty() || !current.empty();
        }
    };

    Iterator begin() const {
        return Iterator{str};
    }

    Iterator end() const {
        return Iterator{};
    }
};