LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := benzware

# Штамп сборки (commit + UTC-время): попадает в шапку журнала, чтобы строки с
# устройства однозначно сопоставлялись со сборкой. Устройство уже дважды
# прошивалось устаревшим artifact'ом, и версию приходилось угадывать по
# формулировкам строк.
_BUILD_SHA  := $(shell git rev-parse --short HEAD 2>/dev/null || echo nogit)
_BUILD_DATE := $(shell date -u +%Y-%m-%dT%H:%M:%SZ)

# LTO — главный структурный обфускатор: границы функций исчезают, константы
# и helper'ы вклеиваются в огромные процедуры, --icf=all дополнительно склеивает
# одинаковые куски. BW_NO_LTO=1 ./build.sh выключает (для отладки падений).
_LTO := -flto
ifneq ($(filter 1 yes true,$(BW_NO_LTO)),)
    _LTO :=
endif

_CF := -DBENZWARE_BUILD_SHA=\"$(_BUILD_SHA)\" -DBENZWARE_BUILD_DATE=\"$(_BUILD_DATE)\" \
       -O3 -ffast-math -march=armv8-a+simd \
       -fomit-frame-pointer -ffunction-sections -fdata-sections \
       -fvisibility=hidden -fvisibility-inlines-hidden \
       -DUSE_OPENGL -DIMGUI_IMPL_OPENGL_ES3 -DNDEBUG \
       -fstack-protector-strong \
       -fPIE \
       -fno-unwind-tables \
       -fno-asynchronous-unwind-tables \
       -fno-ident \
       -fno-jump-tables \
       $(_LTO) \
       -ffile-prefix-map=$(LOCAL_PATH)/=. -fmacro-prefix-map=$(LOCAL_PATH)/=. \
       -fstrict-aliasing

LOCAL_CPPFLAGS := $(_CF) -std=gnu++17 -fexceptions -fno-rtti
LOCAL_CFLAGS   := $(_CF) -std=c11

# --icf=all сливает одинаковые функции (меньше кода и меньше зацепок для
# дизассемблера), --exclude-libs,ALL прячет символы, приехавшие из c++_static.
# --rosegment ВАЖЕН для обфускации: без него lld кладёт .rodata и .text в один
# сегмент R|E. Тогда elf_crypt.py не сможет сделать строки доступными на запись
# без W+X и уронит сборку вместо того, чтобы отдать падающий бинарь. С отдельным
# R-сегментом он просто выставляет ему PF_W в program headers — рантайм пишет
# туда БЕЗ mprotect (тот и давал SIGSEGV на устройстве).
LOCAL_LDFLAGS := -Wl,--gc-sections,-s,--strip-all -Wl,-x -Wl,--build-id=none \
                 -Wl,--exclude-libs,ALL -Wl,--icf=all -pie $(_LTO) \
                 -Wl,--rosegment -Wl,-z,separate-code \
                 -Wl,-z,max-page-size=16384
# max-page-size=16384: NDK r26d по умолчанию выравнивает PT_LOAD по 4K. На
# 16K-ядре сегменты с p_align=0x1000 тогда делят страницу, и хвост R-сегмента
# со строками перемапливается правами соседнего R E-сегмента — писать туда
# нельзя. 16K-выравнивание делает p_vaddr и p_offset конгруэнтными mod 16K и
# разносит сегменты по разным страницам; на 4K-устройствах это тоже работает.

# Собственная обфускация (bw_obf.h/cpp + tools/obf/elf_crypt.py) включена
# ВСЕГДА и работает на стоковом NDK из CI. OBF=1 — дополнительные проходы
# OLLVM/obfuscator-llvm, если вместо стокового clang подложен форк с этими
# проходами (-fla, -bcf, -sub, -sobf, -split). Стоковый NDK их не имеет:
# флаги уйдут в LLVM warnings, но сборку не сломают.
ifneq ($(filter 1 yes true,$(OBF)),)
    _CF += -mllvm -fla -mllvm -bcf -mllvm -sub -mllvm -sobf -mllvm -split
endif

LOCAL_C_INCLUDES += $(LOCAL_PATH)/include $(LOCAL_PATH)/include/ImGui $(LOCAL_PATH)/include/ImGui/backends $(LOCAL_PATH)/src

LOCAL_SRC_FILES := \
    src/main.cpp src/bw_obf.cpp src/startup_ime.cpp \
    src/process.cpp src/audio.cpp src/theme.cpp src/hud.cpp \
    src/esp_draw.cpp src/aim.cpp src/silent_aim.cpp src/farm.cpp src/farm_joy.cpp \
    src/widgets.cpp src/menu.cpp src/config.cpp src/FakeLag.cpp src/screen_capture.cpp src/capture_merge.cpp \
    src/record_capture.cpp src/record_display.cpp src/record_mp4.cpp \
    src/game.cpp src/game_viewmodel.cpp src/game_markers.cpp src/game_farm.cpp \
    src/lang.cpp \
    src/Android_draw/draw.cpp src/Android_touch/TouchHelperA.cpp \
    src/VidAvatar.cpp \
    src/third_party/tjpgd/tjpgd.c \
    src/Blur/Blur.cpp \
    src/ImGui/imgui.cpp src/ImGui/imgui_draw.cpp src/ImGui/imgui_tables.cpp \
    src/ImGui/imgui_widgets.cpp src/ImGui/backends/imgui_impl_android.cpp \
    src/ImGui/backends/imgui_impl_opengl3.cpp

LOCAL_LDLIBS := -llog -landroid -lEGL -lGLESv3 -lOpenSLES -lmediandk -lz -ldl

include $(BUILD_EXECUTABLE)
