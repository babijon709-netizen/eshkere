#pragma once

#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

// PlayerPrefs are edited only while the game process is stopped. This keeps
// Unity's in-memory PlayerPrefs cache from overwriting the preset on exit.
namespace potato_graphics_prefs {

enum class ValueType { Int, Float };

struct Setting {
    const char* key;
    ValueType type;
    const char* low_value;
};

struct SavedValue {
    std::string key;
    bool existed = false;
    ValueType type = ValueType::Int;
    std::string value;
};

inline constexpr Setting kLowPreset[] = {
    {"graphics_quality_v2", ValueType::Int, "0"},
    {"shadows_enabled",     ValueType::Int, "0"},
    {"postprocessing",      ValueType::Int, "0"},
    {"anti_aliasing_v3",    ValueType::Int, "0"},
    {"outline_enabled",     ValueType::Int, "0"},
    // The game itself caps draw distance at 250 in LiteModeRules.
    {"draw_distance_v2",    ValueType::Float, "250"},
};

namespace detail {

inline std::string element_name(ValueType type) {
    return type == ValueType::Float ? "float" : "int";
}

struct Element {
    size_t begin = std::string::npos;
    size_t end = std::string::npos;
    size_t value_begin = std::string::npos;
    size_t value_end = std::string::npos;
    ValueType type = ValueType::Int;
};

inline bool find_element(const std::string& xml, const std::string& key, Element& out) {
    const std::string needle = "name=\"" + key + "\"";
    const size_t name = xml.find(needle);
    if (name == std::string::npos) return false;

    const size_t begin = xml.rfind('<', name);
    const size_t close = xml.find('>', name + needle.size());
    if (begin == std::string::npos || close == std::string::npos) return false;
    const size_t tag_end = xml.find_first_of(" \t\r\n/>", begin + 1);
    if (tag_end == std::string::npos || tag_end > name) return false;
    const std::string tag = xml.substr(begin + 1, tag_end - begin - 1);
    if (tag != "int" && tag != "float") return false;

    const std::string value_attr = "value=\"";
    const size_t value = xml.find(value_attr, name + needle.size());
    if (value == std::string::npos || value >= close) return false;
    const size_t value_begin = value + value_attr.size();
    const size_t value_end = xml.find('"', value_begin);
    if (value_end == std::string::npos || value_end > close) return false;

    const size_t end = xml.find("/>", close > 0 ? close - 1 : 0);
    out.begin = begin;
    out.end = end == std::string::npos ? close + 1 : end + 2;
    out.value_begin = value_begin;
    out.value_end = value_end;
    out.type = tag == "float" ? ValueType::Float : ValueType::Int;
    return true;
}

inline bool replace_value(std::string& xml, const std::string& key,
                          ValueType type, const std::string& value) {
    Element e;
    if (find_element(xml, key, e)) {
        xml.replace(e.value_begin, e.value_end - e.value_begin, value);
        return true;
    }

    const std::string close_map = "</map>";
    const size_t map_end = xml.rfind(close_map);
    if (map_end == std::string::npos) return false;
    const std::string entry = "    <" + element_name(type) + " name=\"" + key +
                              "\" value=\"" + value + "\" />\n";
    xml.insert(map_end, entry);
    return true;
}

inline bool remove_value(std::string& xml, const std::string& key) {
    Element e;
    if (!find_element(xml, key, e)) return true;
    size_t begin = e.begin, end = e.end;
    const size_t line_start = xml.rfind('\n', begin);
    const size_t indent_start = line_start == std::string::npos ? 0 : line_start + 1;
    bool indentation_only = true;
    for (size_t i = indent_start; i < begin; ++i)
        if (xml[i] != ' ' && xml[i] != '\t' && xml[i] != '\r') indentation_only = false;
    if (indentation_only) begin = indent_start;
    if (end < xml.size() && xml[end] == '\r') ++end;
    if (end < xml.size() && xml[end] == '\n') ++end;
    xml.erase(begin, end - begin);
    return true;
}

} // namespace detail

inline bool snapshot(const std::string& xml, std::vector<SavedValue>& saved) {
    if (xml.find("<map") == std::string::npos || xml.find("</map>") == std::string::npos)
        return false;
    saved.clear();
    for (const Setting& setting : kLowPreset) {
        SavedValue value;
        value.key = setting.key;
        detail::Element e;
        if (detail::find_element(xml, value.key, e)) {
            value.existed = true;
            value.type = e.type;
            value.value = xml.substr(e.value_begin, e.value_end - e.value_begin);
        }
        saved.push_back(value);
    }
    return true;
}

inline bool apply_low(std::string& xml) {
    if (xml.find("<map") == std::string::npos || xml.find("</map>") == std::string::npos)
        return false;
    for (const Setting& setting : kLowPreset) {
        std::string target = setting.low_value;
        if (std::string(setting.key) == "draw_distance_v2") {
            detail::Element current;
            if (detail::find_element(xml, setting.key, current)) {
                const std::string raw = xml.substr(current.value_begin,
                                                   current.value_end - current.value_begin);
                char* end = nullptr;
                const float value = std::strtof(raw.c_str(), &end);
                if (end != raw.c_str() && *end == '\0' && std::isfinite(value) && value <= 250.f)
                    target = raw; // never raise a distance already below LiteMode's cap
            }
        }
        if (!detail::replace_value(xml, setting.key, setting.type, target)) return false;
    }
    return true;
}

inline bool restore(std::string& xml, const std::vector<SavedValue>& saved) {
    if (xml.find("<map") == std::string::npos || xml.find("</map>") == std::string::npos)
        return false;
    for (const SavedValue& value : saved) {
        if (value.existed) {
            if (!detail::replace_value(xml, value.key, value.type, value.value)) return false;
        } else if (!detail::remove_value(xml, value.key)) {
            return false;
        }
    }
    return true;
}

inline std::string serialize_backup(const std::vector<SavedValue>& saved) {
    std::string out = "POTATO_PREFS_V1\n";
    for (const SavedValue& value : saved) {
        out += value.key;
        out += '|';
        out += value.existed ? '1' : '0';
        out += '|';
        out += value.type == ValueType::Float ? 'f' : 'i';
        out += '|';
        out += value.value;
        out += '\n';
    }
    return out;
}

inline bool parse_backup(const std::string& text, std::vector<SavedValue>& saved) {
    saved.clear();
    const std::string header = "POTATO_PREFS_V1\n";
    if (text.compare(0, header.size(), header) != 0) return false;
    size_t pos = header.size();
    while (pos < text.size()) {
        const size_t end = text.find('\n', pos);
        const std::string line = text.substr(pos, end == std::string::npos ? end : end - pos);
        if (!line.empty()) {
            const size_t a = line.find('|');
            const size_t b = a == std::string::npos ? a : line.find('|', a + 1);
            const size_t c = b == std::string::npos ? b : line.find('|', b + 1);
            if (a == std::string::npos || b == std::string::npos || c == std::string::npos)
                return false;
            SavedValue value;
            value.key = line.substr(0, a);
            value.existed = line[a + 1] == '1';
            value.type = line[b + 1] == 'f' ? ValueType::Float : ValueType::Int;
            value.value = line.substr(c + 1);
            saved.push_back(value);
        }
        if (end == std::string::npos) break;
        pos = end + 1;
    }
    return saved.size() == sizeof(kLowPreset) / sizeof(kLowPreset[0]);
}

} // namespace potato_graphics_prefs
