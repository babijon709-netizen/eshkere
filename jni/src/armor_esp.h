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

// Имена приходят из двух миров и пишутся по-разному: у предметов это
// "metal.chestplate"/"bone.helmet", у заспавненных на игроке моделей —
// префаб рига ("ArmorMetalHead", "ArmorLeatherShoes", "metal_boots_military").
// Поэтому имя не сравнивается с фиксированным списком целиком: из него
// вытаскивается материал и слот, а значок выбирается по паре (материал, слот).
// Кости скелета при этом отсекаются сами: в них нет материала.
inline int armor_material(const char* s) {
    // 1 металл, 2 кожа, 3 кость, 4 дерево, 5 военное; 0 — не броня.
    if (has(s, "military")) return 5;
    if (has(s, "metal")) return 1;
    if (has(s, "leather")) return 2;
    if (has(s, "bone")) return 3;
    if (has(s, "wood")) return 4;          // wood / wooden
    return 0;
}

// Порядок важен: "ArmorMetalHead" содержит и "armor", и "head".
inline int armor_slot_of(const char* s) {
    if (has(s, "head") || has(s, "helmet") || has(s, "hat") || has(s, "mask") ||
        has(s, "goggle") || has(s, "cone")) return Head;
    if (has(s, "legs") || has(s, "leggin") || has(s, "pants")) return Legs;
    if (has(s, "feet") || has(s, "boot") || has(s, "shoe") || has(s, "sandal") ||
        has(s, "footwear")) return Feet;
    if (has(s, "torso") || has(s, "chest") || has(s, "top") || has(s, "suit") ||
        has(s, "armor") || has(s, "jacket") || has(s, "body")) return Torso;
    return -1;
}

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

    // Цельный костюм: ни одного слотового токена в имени нет.
    if (has(s, "hazmat")) return match(Torso, ARMOR_ICON_HAZMAT);

    // Головные уборы-исключения: свой значок, материала в имени может не быть,
    // а у скинов материал стоит в том же имени — поэтому до общей таблицы.
    if (has(s, "witchhat")) return match(Head, ARMOR_ICON_WITCHHAT);
    if (has(s, "elfhat")) return match(Head, ARMOR_ICON_ELFHAT);
    if (has(s, "santahat")) return match(Head, ARMOR_ICON_SANTAHAT);
    if (has(s, "pumpkin")) return match(Head, ARMOR_ICON_PUMPKINHEAD);
    if (has(s, "snowman")) return match(Head, ARMOR_ICON_SNOWMANHEAD);
    if (has(s, "partycone")) return match(Head, ARMOR_ICON_PARTYCONE);
    // Лыжная шапка/очки: отдельного значка в атласе нет, ближе всего тёмная маска.
    if (has(s, "ski") && (has(s, "head") || has(s, "goggle") || has(s, "mask")))
        return match(Head, ARMOR_ICON_METALMASK);

    const int material = armor_material(s);
    if (material == 0) return {};
    const int slot = armor_slot_of(s);
    if (slot < 0) return {};

    switch (material) {
        case 1:  // металл
            if (slot == Head) return match(Head, has(s, "mask") ? ARMOR_ICON_METALMASK
                                                               : ARMOR_ICON_METALHEAD);
            if (slot == Torso) return match(Torso, ARMOR_ICON_METALTORSO);
            if (slot == Legs) return match(Legs, ARMOR_ICON_METALLEGS);
            return match(Feet, ARMOR_ICON_METALFEET);
        case 2:  // кожа
            if (slot == Head) return match(Head, ARMOR_ICON_LEATHERHEAD);
            if (slot == Torso) return match(Torso, ARMOR_ICON_LEATHERTORSO);
            if (slot == Legs) return match(Legs, ARMOR_ICON_LEATHERLEGS);
            return match(Feet, ARMOR_ICON_LEATHERFEET);
        case 3:  // кость
            if (slot == Head) return match(Head, ARMOR_ICON_BONEHEAD);
            if (slot == Torso) return match(Torso, ARMOR_ICON_BONETORSO);
            if (slot == Legs) return match(Legs, ARMOR_ICON_BONELEGS);
            return match(Feet, ARMOR_ICON_BONEFEET);
        case 4:  // дерево
            if (slot == Head) return match(Head, ARMOR_ICON_WOODHEAD);
            if (slot == Torso) return match(Torso, ARMOR_ICON_WOODTORSO);
            if (slot == Legs) return match(Legs, ARMOR_ICON_WOODLEGS);
            return match(Feet, ARMOR_ICON_WOODFEET);
        default:  // военное: маска идёт под металлическую, своего значка нет
            if (slot == Head) return match(Head, ARMOR_ICON_METALMASK);
            if (slot == Torso) return match(Torso, ARMOR_ICON_MILITARYTORSO);
            if (slot == Legs) return match(Legs, ARMOR_ICON_MILITARYLEGS);
            return match(Feet, ARMOR_ICON_MILITARYFEET);
    }
}

inline bool valid_icon(unsigned icon) {
    return icon > ARMOR_ICON_NONE && icon <= ARMOR_ICON_MILITARYFEET;
}

}  // namespace armoresp
