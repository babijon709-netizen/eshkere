#include "marker_range.h"

#include <cassert>
#include <limits>

int main() {
    using marker_range::within_limit;

    assert(within_limit(149.9f, 150.0f, false));
    assert(!within_limit(150.1f, 150.0f, false));
    assert(within_limit(100000.0f, 150.0f, true));
    assert(!within_limit(std::numeric_limits<float>::infinity(), 150.0f, true));

    assert(marker_range::is_airdrop_name("AirdropCrate"));
    assert(marker_range::is_airdrop_name("SupplyDrop(Clone)"));
    assert(marker_range::is_airdrop_name("CarePackage"));
    assert(!marker_range::is_airdrop_name("LootboxBaloon"));
    assert(!marker_range::is_airdrop_name("LootboxBaloonBig"));
}
