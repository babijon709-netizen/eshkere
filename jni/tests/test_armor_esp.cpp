#include "armor_esp.h"

#include <cassert>

int main() {
    using namespace armoresp;
    auto metal_head = classify_name("ArmorMetalHead_Model");
    assert(metal_head.valid && metal_head.slot == Head && metal_head.icon == ARMOR_ICON_METALHEAD);
    auto mask = classify_name("metal.mask.military");
    assert(mask.valid && mask.slot == Head && mask.icon == ARMOR_ICON_METALMASK);
    auto leather_top = classify_name("Leather.Chestplate(Clone)");
    assert(leather_top.valid && leather_top.slot == Torso && leather_top.icon == ARMOR_ICON_LEATHERTORSO);
    auto bone_legs = classify_name("Bone Leggings");
    assert(bone_legs.valid && bone_legs.slot == Legs && bone_legs.icon == ARMOR_ICON_BONELEGS);
    auto boots = classify_name("ArmorMetalFeet_Model");
    assert(boots.valid && boots.slot == Feet && boots.icon == ARMOR_ICON_METALFEET);
    auto hazmat = classify_name("hazmat_protection");
    assert(hazmat.valid && hazmat.slot == Torso && hazmat.icon == ARMOR_ICON_HAZMAT);
    auto pumpkin = classify_name("pumpkin.head_protection");
    assert(pumpkin.valid && pumpkin.slot == Head && pumpkin.icon == ARMOR_ICON_PUMPKINHEAD);
    assert(!classify_name("Head").valid);
    assert(!classify_name("HandPinky2.R").valid);
    assert(valid_icon(ARMOR_ICON_MILITARYFEET));
    assert(!valid_icon(255));
}
