#pragma once

// Display formatting for schema fields, shared by the desktop app, CSV export and reports.

#include "openpractice/model.hpp"

#include <string>
#include <type_traits>

namespace op {

// "$1,234.56" / "-$1,234.56".
inline std::string usd(Money m) {
    std::string s = m.formatted();
    if (!s.empty() && s[0] == '-') return "-$" + s.substr(1);
    return "$" + s;
}

// "$1.2M", "$350K", "$900" for compact cards.
inline std::string usdShort(Money m) {
    const std::int64_t cents = m.cents() < 0 ? -m.cents() : m.cents();
    const std::string sign = m.cents() < 0 ? "-" : "";
    if (cents >= 100000000) return sign + "$" + Decimal::fromRaw(cents / 10000).fixed(1) + "M";  // millions
    if (cents >= 1000000) return sign + "$" + std::to_string((cents + 50000) / 100000) + "K";     // thousands
    const std::string whole = Money::fromCents((cents + 50) / 100 * 100).formatted();  // "1,234.00"
    return sign + "$" + whole.substr(0, whole.size() - 3);
}

// The value of a field as people read it: amounts with $ and grouping, dates as YYYY-MM-DD,
// choices by label, references by name, booleans as Yes / blank.
template <class T>
std::string displayValue(const Practice& practice, const T& record, const Field<T>& field) {
    return std::visit(
        [&](auto member) -> std::string {
            const auto& v = record.*member;
            using V = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<V, std::string>) return v;
            else if constexpr (std::is_same_v<V, Money>) return v.isZero() ? std::string() : usd(v);
            else if constexpr (std::is_same_v<V, Decimal>) return v.isZero() ? std::string() : v.str();
            else if constexpr (std::is_same_v<V, std::optional<Date>>) return v ? v->str() : std::string();
            else if constexpr (std::is_same_v<V, bool>) return v ? "Yes" : "";
            else if constexpr (std::is_same_v<V, int>) {
                if (field.ref != Ref::None) return practice.refName(field.ref, v);
                return v == 0 ? std::string() : std::to_string(v);
            } else return choiceLabel(v);
        },
        field.member);
}

// Orders two records by a field: numbers and dates by value (blank dates last), text and
// references alphabetically. Returns <0, 0 or >0.
template <class T>
int compareField(const Practice& practice, const T& a, const T& b, const Field<T>& field) {
    return std::visit(
        [&](auto member) -> int {
            const auto& va = a.*member;
            const auto& vb = b.*member;
            using V = std::decay_t<decltype(va)>;
            if constexpr (std::is_same_v<V, std::optional<Date>>) {
                if (va.has_value() != vb.has_value()) return va.has_value() ? -1 : 1;
                if (!va) return 0;
                return *va < *vb ? -1 : *vb < *va ? 1 : 0;
            } else if constexpr (std::is_same_v<V, std::string>) {
                return va.compare(vb);
            } else if constexpr (std::is_same_v<V, int>) {
                if (field.ref != Ref::None) return practice.refName(field.ref, va).compare(practice.refName(field.ref, vb));
                return va < vb ? -1 : vb < va ? 1 : 0;
            } else if constexpr (std::is_same_v<V, bool>) {
                return static_cast<int>(va) - static_cast<int>(vb);
            } else if constexpr (std::is_enum_v<V>) {
                return static_cast<int>(va) - static_cast<int>(vb);
            } else {
                return va < vb ? -1 : vb < va ? 1 : 0;
            }
        },
        field.member);
}

}  // namespace op
