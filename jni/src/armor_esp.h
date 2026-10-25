#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

enum ArmorIconId : unsigned char {
    ARMOR_ICON_NONE = 0,
    ARMOR_ICON_METALHEAD = 1,
    ARMOR_ICON_LEATHERHEAD = 2,
    ARMOR_ICON_BONEHEAD = 3,
    ARMOR_ICON_WOODHEAD = 4,
    ARMOR_ICON_METALMASK = 5,
    ARMOR_ICON_WITCHHAT = 6,
    ARMOR_ICON_ELFHAT = 7,
    ARMOR_ICON_SANTAHAT = 8,
    ARMOR_ICON_PUMPKINHEAD = 9,
    ARMOR_ICON_PARTYCONE = 10,
    ARMOR_ICON_SNOWMANHEAD = 11,
    ARMOR_ICON_METALTORSO = 12,
    ARMOR_ICON_LEATHERTORSO = 13,
    ARMOR_ICON_BONETORSO = 14,
    ARMOR_ICON_WOODTORSO = 15,
    ARMOR_ICON_HAZMAT = 16,
    ARMOR_ICON_MILITARYTORSO = 17,
    ARMOR_ICON_METALLEGS = 18,
    ARMOR_ICON_LEATHERLEGS = 19,
    ARMOR_ICON_BONELEGS = 20,
    ARMOR_ICON_WOODLEGS = 21,
    ARMOR_ICON_MILITARYLEGS = 22,
    ARMOR_ICON_METALFEET = 23,
    ARMOR_ICON_LEATHERFEET = 24,
    ARMOR_ICON_BONEFEET = 25,
    ARMOR_ICON_WOODFEET = 26,
    ARMOR_ICON_MILITARYFEET = 27,
};


namespace armoresp {

enum Slot : uint8_t { Head = 0, Torso = 1, Legs = 2, Feet = 3, SlotCount = 4 };

struct Match {
    Slot slot = Head;
    ArmorIconId icon = ARMOR_ICON_NONE;
    bool valid = false;
};

inline bool has(const char* text, const char* needle) {
    return text && needle && std::strstr(text, needle) != nullptr;
}

// GameObject names from the player's equipment rig and item labels both occur
// in the supplied Oxide bundles. Normalize separators/case, then match only
// recognizable armor prefab tokens (never generic bones such as "Head").
inline Match classify_name(const char* raw) {
    if (!raw) return {};
    char s[96]{};
    size_t n = 0;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(raw);
         *p && n + 1 < sizeof(s); ++p) {
        if ((*p >= 'A' && *p <= 'Z')) s[n++] = static_cast<char>(*p - 'A' + 'a');
        else if ((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9')) s[n++] = static_cast<char>(*p);
    }
    s[n] = '\0';
    auto match = [](Slot slot, ArmorIconId icon) { return Match{slot, icon, true}; };

    // Full-body outfits take precedence over per-piece tokens in their names.
    if (has(s, "hazmat")) return match(Torso, ARMOR_ICON_HAZMAT);
    if (has(s, "militaryarmor") || has(s, "militarysuit"))
        return match(Torso, ARMOR_ICON_MILITARYTORSO);

    if (has(s, "armormetalhead") || has(s, "metalhelmet"))
        return match(Head, ARMOR_ICON_METALHEAD);
    if (has(s, "metalmask")) return match(Head, ARMOR_ICON_METALMASK);
    if (has(s, "armorleatherhelmet") || has(s, "leatherhelmet"))
        return match(Head, ARMOR_ICON_LEATHERHEAD);
    if (has(s, "bonehelmet")) return match(Head, ARMOR_ICON_BONEHEAD);
    if (has(s, "woodhelmet") || has(s, "woodenhelmet"))
        return match(Head, ARMOR_ICON_WOODHEAD);
    if (has(s, "witchhat")) return match(Head, ARMOR_ICON_WITCHHAT);
    if (has(s, "elfhat")) return match(Head, ARMOR_ICON_ELFHAT);
    if (has(s, "santahat")) return match(Head, ARMOR_ICON_SANTAHAT);
    if (has(s, "pumpkinhead")) return match(Head, ARMOR_ICON_PUMPKINHEAD);
    if (has(s, "partycone")) return match(Head, ARMOR_ICON_PARTYCONE);
    if (has(s, "snowmanhead")) return match(Head, ARMOR_ICON_SNOWMANHEAD);

    if (has(s, "armormetaltorso") || has(s, "metalchestplate"))
        return match(Torso, ARMOR_ICON_METALTORSO);
    if (has(s, "armorleathertop") || has(s, "leatherchestplate"))
        return match(Torso, ARMOR_ICON_LEATHERTORSO);
    if (has(s, "bonechestplate")) return match(Torso, ARMOR_ICON_BONETORSO);
    if (has(s, "woodchestplate") || has(s, "woodenchestplate"))
        return match(Torso, ARMOR_ICON_WOODTORSO);

    if (has(s, "armormetallegs") || has(s, "metalleggings"))
        return match(Legs, ARMOR_ICON_METALLEGS);
    if (has(s, "armorleatherlegs") || has(s, "leatherleggings"))
        return match(Legs, ARMOR_ICON_LEATHERLEGS);
    if (has(s, "boneleggings")) return match(Legs, ARMOR_ICON_BONELEGS);
    if (has(s, "woodleggings") || has(s, "woodenleggings"))
        return match(Legs, ARMOR_ICON_WOODLEGS);
    if (has(s, "metallegginsmilitary") || has(s, "militaryleggings"))
        return match(Legs, ARMOR_ICON_MILITARYLEGS);

    if (has(s, "armormetalfeet") || has(s, "metalboots"))
        return match(Feet, ARMOR_ICON_METALFEET);
    if (has(s, "armorleathershoes") || has(s, "leatherboots"))
        return match(Feet, ARMOR_ICON_LEATHERFEET);
    if (has(s, "boneshoes") || has(s, "boneboots"))
        return match(Feet, ARMOR_ICON_BONEFEET);
    if (has(s, "woodfootwear") || has(s, "woodensandals"))
        return match(Feet, ARMOR_ICON_WOODFEET);
    if (has(s, "metalbootsmilitary") || has(s, "militaryboots"))
        return match(Feet, ARMOR_ICON_MILITARYFEET);
    return {};
}

inline bool valid_icon(unsigned icon) {
    return icon > ARMOR_ICON_NONE && icon <= ARMOR_ICON_MILITARYFEET;
}

}  // namespace armoresp
