#include "../src/potato_graphics_prefs.h"

#include <cassert>
#include <string>
#include <vector>

int main() {
    using namespace potato_graphics_prefs;
    std::string xml =
        "<?xml version='1.0' encoding='utf-8' standalone='yes' ?>\n"
        "<map>\n"
        "    <int name=\"graphics_quality_v2\" value=\"3\" />\n"
        "    <int name=\"shadows_enabled\" value=\"1\" />\n"
        "    <int name=\"postprocessing\" value=\"2\" />\n"
        "    <int name=\"anti_aliasing_v3\" value=\"1\" />\n"
        "    <float name=\"draw_distance_v2\" value=\"450\" />\n"
        "    <string name=\"unrelated\" value=\"untouched\" />\n"
        "</map>\n";

    std::vector<SavedValue> saved;
    assert(snapshot(xml, saved));
    const std::string backup = serialize_backup(saved);
    std::vector<SavedValue> parsed;
    assert(parse_backup(backup, parsed));
    assert(parsed.size() == 6);

    std::string low = xml;
    assert(apply_low(low));
    assert(low.find("name=\"graphics_quality_v2\" value=\"0\"") != std::string::npos);
    assert(low.find("name=\"shadows_enabled\" value=\"0\"") != std::string::npos);
    assert(low.find("name=\"postprocessing\" value=\"0\"") != std::string::npos);
    assert(low.find("name=\"anti_aliasing_v3\" value=\"0\"") != std::string::npos);
    assert(low.find("name=\"outline_enabled\" value=\"0\"") != std::string::npos);
    assert(low.find("name=\"draw_distance_v2\" value=\"250\"") != std::string::npos);
    assert(low.find("name=\"unrelated\" value=\"untouched\"") != std::string::npos);

    assert(restore(low, parsed));
    assert(low == xml);

    std::string already_near =
        "<map><float name=\"draw_distance_v2\" value=\"125.0\" /></map>";
    assert(apply_low(already_near));
    assert(already_near.find("name=\"draw_distance_v2\" value=\"125.0\"") != std::string::npos);

    std::string sparse = "<map>\n  <string name=\"other\" value=\"x\" />\n</map>\n";
    assert(snapshot(sparse, saved));
    assert(apply_low(sparse));
    assert(sparse.find("name=\"graphics_quality_v2\" value=\"0\"") != std::string::npos);
    assert(restore(sparse, saved));
    assert(sparse == "<map>\n  <string name=\"other\" value=\"x\" />\n</map>\n");

    std::string invalid = "<root></root>";
    assert(!apply_low(invalid));
    assert(!restore(invalid, parsed));
    return 0;
}
