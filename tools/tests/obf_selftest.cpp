// Хостовый интеграционный тест пост-линковочной обфускации.
// Собирается нативная PIE, прогоняется через tools/obf/elf_crypt.py, затем
// запускается: строки должны быть нечитаемы в файле, но читаемы в рантайме.
#include "bw_obf.h"

#include <cstdio>
#include <cstring>

static const char* kSecret = "BW_OBF_CANARY_7f3a91c4e2";
static const char* kDriver = "C61StIeKasRIz89g3c6xLzIc9L0LaRbI";
static const char* kPackage = "com.catsbit.oxidesurvivalisland";

int main() {
    BW_GUARD();
    const int cmd = BW_CONST(0x301);
    const int auth = BW_CONST(0x300);
    if (!bw::opaque()) return 2;
    if (cmd != 0x301 || auth != 0x300) return 3;
    if (std::strcmp(kSecret, "BW_OBF_CANARY_7f3a91c4e2") != 0) return 4;
    std::printf("%s|%s|%s|%d|%d\n", kSecret, kDriver, kPackage, cmd, auth);
    return 0;
}
