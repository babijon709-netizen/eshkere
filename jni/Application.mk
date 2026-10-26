APP_ABI      := arm64-v8a
APP_PLATFORM := android-24
APP_STL      := c++_static
APP_OPTIM    := release

# Стрипаем на этапе link, а не только утилитой: без .symtab и локальных имён в
# бинаре не остаётся ничего, по чему восстанавливается структура исходника.
APP_STRIP_MODE := --strip-all
# -fno-ident убирает секцию .comment (версия тулчейна) из каждого объекта.
APP_CFLAGS   := -fno-ident
APP_CPPFLAGS := -fno-ident
