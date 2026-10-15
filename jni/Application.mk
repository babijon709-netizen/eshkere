# jni/Application.mk — общепроектные настройки ndk-build (модуль — в Android.mk).

# Единственный ABI, который ждут build.sh и CI-артефакт benzware-sh-arm64-v8a.
APP_ABI := arm64-v8a

# Пол исполняемого файла. В коде нет ничего API-gated (pread/ioctl/socket/
# syscall/uinput доступны с доисторических уровней), так что стоим на минимуме
# NDK r26 — выше поднимать смысла нет, ниже устройство уже не соберётся.
APP_PLATFORM := android-21

# Статическая libc++: бинарник уходит в /data/local/tmp/xvcen и запускается из
# Termux/su, libc++_shared.so рядом с ним никто не положит. Исполняемый файл
# один — конфликта нескольких копий STL в процессе не будет.
APP_STL := c++_static

# PIE обязателен для /data/local/tmp на android-21+; linker откажется грузить
# не-PIE исполняемый файл.
APP_PIE := true

APP_OPTIM := release

# Читалка памяти приводит сырые байты к структурам игры и обратно — это чистое
# нарушение strict aliasing. Без этого флага -O2 имеет право переупорядочить
# такие чтения, и оффсеты «плывут» только на релизной сборке.
APP_CPPFLAGS := -fno-strict-aliasing
