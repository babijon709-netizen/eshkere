# jni/Android.mk — benzware (xvcen): arm64-v8a PIE-исполняемый файл.
#
# build.sh зовёт ndk-build так:
#   NDK_PROJECT_PATH=. NDK_APPLICATION_MK=jni/Application.mk APP_BUILD_SCRIPT=jni/Android.mk
# и ждёт на выходе libs/arm64-v8a/xvcen (потом копирует его в benzware.sh и
# xvcen-arm64-v8a). Имя модуля ниже менять нельзя — build.sh ищет именно xvcen.
#
# Источники собираются wildcard-ом: когда в jni/src досыпаются остальные файлы
# проекта (process.cpp, silent_aim.cpp и т.д.), этот mk править не нужно.

LOCAL_PATH := $(call my-dir)

# src/*.cpp и src/*/*.cpp — подкаталоги вроде Blur/ и Android_touch/ тоже.
_xvcen_src := $(wildcard $(LOCAL_PATH)/src/*.cpp $(LOCAL_PATH)/src/*/*.cpp)

include $(CLEAR_VARS)

LOCAL_MODULE    := xvcen
LOCAL_SRC_FILES := $(_xvcen_src:$(LOCAL_PATH)/%=%)

# Инклюды в кавычках clang ищет сначала рядом с включающим файлом (то есть в
# src/), остальное закрывают эти пути. game.h лежит в include/, внутренние
# заголовки — в src/, «Blur/Blur.h» и «Android_touch/TouchHelperA.h» —
# подкаталоги одного из двух; LOCAL_PATH добавлен, чтобы оба варианта работали.
LOCAL_C_INCLUDES := $(LOCAL_PATH) \
                    $(LOCAL_PATH)/src \
                    $(LOCAL_PATH)/include

# Только системные библиотеки bionic/NDK:
#   log      — __android_log_print
#   android  — ANativeWindow / AInputQueue / сенсоры (оверлей, тач)
#   EGL, GLESv2 — контекст оверлея и Blur/esp_draw
#   dl       — dlopen/dlsym
# Недостающая вылезет как undefined reference на линковке, лишняя не мешает.
LOCAL_LDLIBS := -llog -landroid -lEGL -lGLESv2 -ldl

# Явно, а не «как ndk-build решит»: -fno-rtti ломает dynamic_cast, -fno-exceptions
# ломает throw/catch в чужих TU. Цена — несколько килобайт бинарника.
LOCAL_CPP_FEATURES += exceptions rtti

# Мёртвый код выкидываем по секциям; предупреждения не глушим и -Werror не
# ставим: дерево досыпается частями, сборка не должна падать на ворнингах.
LOCAL_CPPFLAGS += -ffunction-sections -fdata-sections
LOCAL_LDFLAGS  += -Wl,--gc-sections

include $(BUILD_EXECUTABLE)
