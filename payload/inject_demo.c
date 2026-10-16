// inject_demo.c — демо-payload для inject.h (arm64, flat blob, без libc).
//
// Контракт (см. шапку inject.h): entry на +0x40 от начала blob; на входе
// x0-x7 — аргументы перехваченного вызова, LR — живой вызывающий игры.
// Обязан: защититься от повторного входа, сделать дело, br в header->orig_fn
// с исходными регистрами. Заголовок [0x40] хост заполняет до старта.
//
// Демо делает минимум: один u32-патч по адресу из заголовка (для проверки
// всей цепочки на устройстве: benzware inject --patch 0xADDR 0xVALUE) и
// аккуратный возврат. Реальные фичи строятся по этому же скелету: вместо
// патча — установка il2cpp-хуков/правка данных, всё в адресном пространстве
// игры, никаких syscall'ов наружу.
//
// Сборка: payload/build_payload.sh (нужен NDK; целевые в link.ld).
// Требования к бинарнику проверяет сборщик: entry ровно на 0x40, ноль
// динамических релокаций, никаких абсолютных литералов.

typedef unsigned long u64;
typedef unsigned int  u32;

typedef struct {
    u64 magic;              // +0x00: 0x494a5055 ("IJPU"), читеру
    u64 orig_fn;            // +0x08: куда вернуть управление
    volatile u64 flags;     // +0x10: 1 = старт; payload ставит 2
    u64 reserved;           // +0x18
    u64 patch_addr;         // +0x20: демо-фича: *(u32*)patch_addr = patch_value
    u32 patch_value;        // +0x28
    u32 patch_en;           // +0x2c
} InjHeader;

_Static_assert(sizeof(InjHeader) <= 0x40, "header fits in 0x40");

__attribute__((noinline))
static void inj_body(InjHeader* h) {
    // Повторный вход (два потока успели в увод слота): атомарный обмен гарантирует,
    // что патч выполнит ровно один вход; оба вернутся в orig_fn.
    u64 prev = __atomic_exchange_n(&h->flags, (u64)2, __ATOMIC_ACQ_REL);
    if (prev != 1) return;   // уже отработали или стартовали не мы
    if (h->patch_en && h->patch_addr)
        *(volatile u32*)h->patch_addr = h->patch_value;
}

// naked — никакой пролог/эпилог: регистры игры должны дойти до orig_fn
// нетронутыми. x19/x20 — калиброванные сохранения вокруг вызова тела
// (тело обязано сохранять callee-saved по ABI, но мы не полагаемся).
__attribute__((naked, noinline, used, section(".text.inj_entry")))
static void inj_entry(void) {
    __asm__ volatile(
        "9:  adr     x17, 9b            \n"  // x17 = &inj_entry = blob + 0x40
        "    sub     x17, x17, #64      \n"  // x17 = заголовок (0x40)
        "    mov     x19, x30           \n"  // LR игры
        "    mov     x20, x17           \n"
        "    stp     x0, x1, [sp, #-96]!\n"
        "    stp     x2, x3, [sp, #16]  \n"
        "    stp     x4, x5, [sp, #32]  \n"
        "    stp     x6, x7, [sp, #48]  \n"
        "    stp     x19, x20, [sp, #64]\n"
        "    mov     x0, x20            \n"
        "    bl      inj_body           \n"
        "    ldp     x19, x20, [sp, #64]\n"
        "    ldp     x6, x7, [sp, #48]  \n"
        "    ldp     x4, x5, [sp, #32]  \n"
        "    ldp     x2, x3, [sp, #16]  \n"
        "    ldp     x0, x1, [sp], #96  \n"
        "    mov     x30, x19           \n"
        "    ldr     x17, [x20, #8]     \n"  // orig_fn
        "    br      x17                \n"
    );
}
