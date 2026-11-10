#pragma once

#include <cmath>
#include <concepts>
#include <cstdint>
#include <map>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace datapump::triad::probe {

struct Json {
    using Array = std::vector<Json>;
    using Object = std::map<std::string, Json, std::less<>>;
    std::variant<std::monostate, bool, std::int64_t, std::uint64_t, double,
                 std::string, Array, Object> value;

    Json() = default;
    Json(bool item) : value(item) {}
    template<std::integral T> requires (!std::same_as<T, bool>)
    Json(T item) : value(static_cast<std::conditional_t<std::is_signed_v<T>,
                                                        std::int64_t, std::uint64_t>>(item)) {}
    Json(double item) : value(item) {}
    Json(std::string_view item) : value(std::string(item)) {}
    Json(const char* item) : Json(std::string_view(item)) {}
    Json(std::string item) : value(std::move(item)) {}
    Json(Array item) : value(std::move(item)) {}
    Json(Object item) : value(std::move(item)) {}
    Json& operator[](std::string key) { return std::get<Object>(value)[std::move(key)]; }
    Array& array() { return std::get<Array>(value); }
};

inline Json object(std::initializer_list<Json::Object::value_type> fields) {
    return Json::Object(fields);
}

inline void write_string(std::ostream& output, std::string_view text) {
    output << '"';
    constexpr std::string_view hex = "0123456789abcdef";
    for (const unsigned char character : text) {
        if (character == '"' || character == '\\') output << '\\' << character;
        else if (character < 0x20 || character >= 0x7f)
            output << "\\u00" << hex[character >> 4] << hex[character & 0x0f];
        else output << character;
    }
    output << '"';
}

inline void write_json(std::ostream& output, const Json& item) {
    std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::same_as<T, std::monostate>) output << "null";
        else if constexpr (std::same_as<T, bool>) output << (value ? "true" : "false");
        else if constexpr (std::same_as<T, double>) {
            if (std::isfinite(value)) output << value;
            else output << "null";
        } else if constexpr (std::same_as<T, std::string>) write_string(output, value);
        else if constexpr (std::same_as<T, Json::Array>) {
            output << '[';
            bool first = true;
            for (const auto& child : value) {
                if (!first) output << ',';
                first = false;
                write_json(output, child);
            }
            output << ']';
        } else if constexpr (std::same_as<T, Json::Object>) {
            output << '{';
            bool first = true;
            for (const auto& [key, child] : value) {
                if (!first) output << ',';
                first = false;
                write_string(output, key);
                output << ':';
                write_json(output, child);
            }
            output << '}';
        } else output << value;
    }, item.value);
}

} // namespace datapump::triad::probe
