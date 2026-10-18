#include <stdio.h>
#include <stdlib.h>
#include <cstdint>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <cmath>
#include <ctime>
#include <cstring>
#include <poll.h>
#include <vector>
#include <algorithm>
#include <atomic>
#include <linux/input.h>
#include <linux/uinput.h>

#include "imgui.h"
#include "touch_device_filter.h"

#define maxE 5
#define maxF 10
#define UNGRAB 0
#define GRAB 1

// Слоты синтетических пальцев: 9 — «прежний» палец аимбота (Touch_Down/Move/Up,
// id 60000). Пальцы автофарма (Touch_*_N: 0 стик, 1 камера, 2 удар) больше не
// используются: движение, камера и удары бота идут через память игры
// (farm_joy.cpp и farm.cpp).
// Разделение важное: аимбот, в отличие от автофарма, держит палец на месте и
// ПЕРЕСЫЛАЕТ ту же точку каждый кадр — так у игры остаётся «живым» удержание
// камеры (см. UpdateAim: hold still, keep the touch alive). Поэтому пропуск
// повторов пакета к этому пальцу не применяется — см. Upload().
static constexpr int kAimFingerSlot = 9;
static constexpr int kAimFingerId   = 60000;

bool other_touch;

static uint32_t orientation = 0;
static float screenHeight = 0, screenWidth = 0;

struct touchObj {
    bool isDown = false;
    // Касание забрал оверлей: в игру оно не уходит (см. Touch_BlockRect).
    bool blocked = false;
    bool blockClassified = false; // block decision is latched for one contact
    int x = 0;
    int y = 0;
    int id = 0;
};

struct targ {
    int fdNum;
    float S2TX;
    float S2TY;
    int minX;
    int minY;
    int singleTouch;   // 1: устройство type-A (ABS_X/ABS_Y+BTN_TOUCH) — тачпад ВМ
};

static struct {
    input_event downEvent[2]{{{}, EV_KEY, BTN_TOUCH,       1},
                             {{}, EV_KEY, BTN_TOOL_FINGER, 1}};
    input_event event[512]{0};
} input;

static targ targF[maxE];

static touchObj Finger[maxE][maxF];

// Прямоугольник экрана, касания внутри которого оверлей забирает себе: в игру
// они не уходят вовсе. Нужен фрикаму — его джойстик лежит поверх игры, и без
// этого палец на нём ещё и ходил бы персонажем, а камера во фрикаме привязана
// к телу и поехала бы вместе с ним. Пока прямоугольник не задан, поведение
// ровно прежнее: ни один палец не блокируется.
static bool  g_block_on = false;
static float g_block_x0 = 0.f, g_block_y0 = 0.f, g_block_x1 = 0.f, g_block_y1 = 0.f;
static constexpr int kMaxBlockCircles = 4;
static int g_block_circle_count = 0;
static float g_block_circle_x[kMaxBlockCircles]{}, g_block_circle_y[kMaxBlockCircles]{},
             g_block_circle_r[kMaxBlockCircles]{};

struct BlockRectItem {
    float x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f;
};
static constexpr int kMaxExtraBlockRects = 16;
static pthread_mutex_t g_block_mu = PTHREAD_MUTEX_INITIALIZER;
static int g_extra_rect_count = 0;
static BlockRectItem g_extra_rects[kMaxExtraBlockRects]{};

void Touch_BlockRect(bool on, float x0, float y0, float x1, float y1) {
    pthread_mutex_lock(&g_block_mu);
    g_block_on = on;
    g_block_circle_count = 0;
    g_block_x0 = x0; g_block_y0 = y0; g_block_x1 = x1; g_block_y1 = y1;
    pthread_mutex_unlock(&g_block_mu);
}

void Touch_ClearExtraBlockRects() {
    pthread_mutex_lock(&g_block_mu);
    g_extra_rect_count = 0;
    pthread_mutex_unlock(&g_block_mu);
}

void Touch_AddBlockRect(float x0, float y0, float x1, float y1) {
    if (!std::isfinite(x0) || !std::isfinite(y0) ||
        !std::isfinite(x1) || !std::isfinite(y1)) return;
    if (x0 > x1) { const float t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { const float t = y0; y0 = y1; y1 = t; }
    pthread_mutex_lock(&g_block_mu);
    if (g_extra_rect_count < kMaxExtraBlockRects) {
        g_extra_rects[g_extra_rect_count++] = {x0, y0, x1, y1};
    }
    pthread_mutex_unlock(&g_block_mu);
}

void Touch_BlockCircles(bool on, const float* xs, const float* ys,
                        const float* radii, int count) {
    pthread_mutex_lock(&g_block_mu);
    g_block_on = on;
    g_block_x0 = g_block_y0 = g_block_x1 = g_block_y1 = 0.f;
    g_block_circle_count = 0;
    if (!on || !xs || !ys || !radii) {
        pthread_mutex_unlock(&g_block_mu);
        return;
    }
    if (count > kMaxBlockCircles) count = kMaxBlockCircles;
    for (int i = 0; i < count; ++i) {
        if (!std::isfinite(xs[i]) || !std::isfinite(ys[i]) ||
            !std::isfinite(radii[i]) || radii[i] <= 0.f) continue;
        const int n = g_block_circle_count++;
        g_block_circle_x[n] = xs[i]; g_block_circle_y[n] = ys[i];
        g_block_circle_r[n] = radii[i];
    }
    pthread_mutex_unlock(&g_block_mu);
}

static bool Touch_IsBlocked(float x, float y) {
    pthread_mutex_lock(&g_block_mu);
    for (int i = 0; i < g_extra_rect_count; ++i) {
        const BlockRectItem& r = g_extra_rects[i];
        if (x >= r.x0 && x <= r.x1 && y >= r.y0 && y <= r.y1) {
            pthread_mutex_unlock(&g_block_mu);
            return true;
        }
    }
    if (!g_block_on) {
        pthread_mutex_unlock(&g_block_mu);
        return false;
    }
    if (g_block_circle_count > 0) {
        for (int i = 0; i < g_block_circle_count; ++i) {
            const float dx = x - g_block_circle_x[i], dy = y - g_block_circle_y[i];
            if (dx * dx + dy * dy <= g_block_circle_r[i] * g_block_circle_r[i]) {
                pthread_mutex_unlock(&g_block_mu);
                return true;
            }
        }
        pthread_mutex_unlock(&g_block_mu);
        return false;
    }
    const bool hit = (x >= g_block_x0 && x <= g_block_x1 && y >= g_block_y0 && y <= g_block_y1);
    pthread_mutex_unlock(&g_block_mu);
    return hit;
}

static int fdNum = 0, origfd[maxE], nowfd;

static int devMaxX = 0, devMaxY = 0;

static float scale_x, scale_y;

static void Touch_UpdateScale() {
    if (devMaxX <= 0 || devMaxY <= 0) return;
    if (orientation == 1 || orientation == 3) {
        ::scale_x = (float) devMaxX / (::screenHeight > 0 ? ::screenHeight : 1);
        ::scale_y = (float) devMaxY / (::screenWidth  > 0 ? ::screenWidth  : 1);
    } else {
        ::scale_x = (float) devMaxX / (::screenWidth  > 0 ? ::screenWidth  : 1);
        ::scale_y = (float) devMaxY / (::screenHeight > 0 ? ::screenHeight : 1);
    }
}

static bool Touch_initialized = false;

static bool Touch_readOnly = false;

// Класс устройства /dev/input/eventN:
//   0 — не тачскрин;
//   1 — мультитач (ABS_MT_POSITION_X/Y; ABS_MT_SLOT НЕ требуем: часть
//       виртуальных тачскринов ВМ его не сообщает, а поток чтения и без
//       слота сводится к одному пальцу);
//   2 — однотачевый type-A (ABS_X/ABS_Y + BTN_TOUCH): так выглядят
//       virtio/qemu-тачпады и «мышиная интеграция» в VMOS/X8/F1 и облачных
//       телефонах — из-за них на ВМ и падало «Failed init touch!».
// INPUT_PROP_POINTER-тачпады и BTN_TOOL_PEN-дигитайзеры отбрасываются: на
// Xiaomi они идут рядом с настоящей DIRECT-панелью и сбрасывают MouseDown.
static int checkDeviceIsTouch(int fd);
static void genRandomString(char *string, int length) {
    int flag, i;
    srand((unsigned) time(NULL) + length);
    for (i = 0; i < length - 1; i++) {
        flag = rand() % 3;
        switch (flag) {
            case 0:
                string[i] = 'A' + rand() % 26;
                break;
            case 1:
                string[i] = 'a' + rand() % 26;
                break;
            case 2:
                string[i] = '0' + rand() % 10;
                break;
            default:
                string[i] = 'x';
                break;
        }
    }
    string[length - 1] = '\0';
}

// Диагностика лага автофарма: сколько раз пересобирали и отправляли пакет
// событий синтетического тача и сколько миллисекунд на это ушло суммарно.
// Upload() вызывается на КАЖДЫЙ SYN_REPORT, то есть десятки раз в секунду, и
// делает write() в uinput плюс ожидание флага — на телефоне это заметная
// статья расхода кадра, но из лога автофарма её раньше не было видно вовсе.
static unsigned long long g_touch_upload_calls = 0;
static double             g_touch_upload_ms    = 0.0;
static unsigned long long g_touch_upload_skipped = 0;
// Последний отправленный пакет: повторы не пишем в uinput (см. Upload()).
// input_event в этой сборке заголовков — 8 байт, но берём размер с запасом от
// самого типа: 11 пальцев × 6 событий + SYN_REPORT + до двух BTN_* при подъёме.
static struct input_event g_touch_last_packet[80];
static size_t             g_touch_last_bytes = 0;

void Touch_UploadStats(unsigned long long& calls, double& ms, unsigned long long& skipped) {
    calls   = g_touch_upload_calls;
    ms      = g_touch_upload_ms;
    skipped = g_touch_upload_skipped;
}

static inline double TouchNowMs() {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

// Upload() зовут ДВА потока: поток чтения тачскрина (на каждый SYN_REPORT) и
// поток оверлея через синтетические пальцы (каждый Touch_Down_N/Move/Up). Пока
// пакет собирается, второй должен ждать — раньше это было `while (bTouch);`,
// то есть активное ожидание на ядре: на телефоне такие ожидания съедают квант
// CPU у самой игры и превращаются в просадку кадров ровно тогда, когда бот
// активно водит пальцами. Обычный мьютекс ждёт без расхода CPU.
static pthread_mutex_t g_touch_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool            g_touch_first_down = true;

static void Upload() {
    const double t0 = TouchNowMs();
    pthread_mutex_lock(&g_touch_mutex);
    bool isFirstDown = g_touch_first_down;
    // Есть ли в собираемом пакете палец аимбота. Такой пакет уходит в uinput
    // ВСЕГДА, даже если он байт в байт повторяет предыдущий (см. пропуск
    // повторов ниже): удержание камеры аимботом держится именно на этих
    // повторах, и прошлая сборка писала их каждый кадр.
    bool aimFinger = false;
    int tmpCnt = 0, tmpCnt2 = 0, i, j;
    for (i = 0; i < fdNum; i++) {
        for (j = 0; j < maxF; j++) {
            if (Finger[i][j].isDown && !Finger[i][j].blocked) {
                if (tmpCnt2++ > 10) {
                    goto finish;
                }
                if (Finger[i][j].id == kAimFingerId) aimFinger = true;
                input.event[tmpCnt].type = EV_ABS;
                input.event[tmpCnt].code = ABS_X;
                input.event[tmpCnt].value = Finger[i][j].x;
                tmpCnt++;

                input.event[tmpCnt].type = EV_ABS;
                input.event[tmpCnt].code = ABS_Y;
                input.event[tmpCnt].value = Finger[i][j].y;
                tmpCnt++;

                input.event[tmpCnt].type = EV_ABS;
                input.event[tmpCnt].code = ABS_MT_POSITION_X;
                input.event[tmpCnt].value = Finger[i][j].x;
                tmpCnt++;

                input.event[tmpCnt].type = EV_ABS;
                input.event[tmpCnt].code = ABS_MT_POSITION_Y;
                input.event[tmpCnt].value = Finger[i][j].y;
                tmpCnt++;

                input.event[tmpCnt].type = EV_ABS;
                input.event[tmpCnt].code = ABS_MT_TRACKING_ID;
                input.event[tmpCnt].value = Finger[i][j].id;
                tmpCnt++;

                input.event[tmpCnt].type = EV_SYN;
                input.event[tmpCnt].code = SYN_MT_REPORT;
                input.event[tmpCnt].value = 0;
                tmpCnt++;
            }
        }
    }
    finish:
    bool is = false;
    if (tmpCnt == 0) {
        input.event[tmpCnt].type = EV_SYN;
        input.event[tmpCnt].code = SYN_MT_REPORT;
        input.event[tmpCnt].value = 0;
        tmpCnt++;
        if (!isFirstDown) {
            isFirstDown = true;
            input.event[tmpCnt].type = EV_KEY;
            input.event[tmpCnt].code = BTN_TOUCH;
            input.event[tmpCnt].value = 0;
            tmpCnt++;
            input.event[tmpCnt].type = EV_KEY;
            input.event[tmpCnt].code = BTN_TOOL_FINGER;
            input.event[tmpCnt].value = 0;
            tmpCnt++;
        }
    } else {
        is = true;
    }
    input.event[tmpCnt].type = EV_SYN;
    input.event[tmpCnt].code = SYN_REPORT;
    input.event[tmpCnt].value = 0;
    tmpCnt++;

    // ---- Пропуск повторов ---------------------------------------------------
    // В событие входит только список касаний, но Upload() зовётся на КАЖДЫЙ
    // SYN_REPORT от тачскрина и на каждое изменение синтетического пальца, а
    // полный пакет состоит из 6 событий на палец (ABS_X/Y, MT_POSITION_X/Y,
    // TRACKING_ID, SYN_MT_REPORT) плюс SYN_REPORT. Кадры, где пальцы стоят на
    // месте (а таких при фарме большинство: палец удара переставляется пару
    // раз в секунду, джойстик — только когда бот реально идёт), пересобирали и
    // переписывали ровно тот же пакет: на 4 пальцах это ~26 событий в uinput на
    // каждый SYN_REPORT — десятки лишних write() в секунду на обеих сторонах
    // (наш поток и input-поток системы). Сравниваем пакет с уже отправленным и
    // молчим, если он не изменился. Первый пакет после старта и всё, что
    // менялось (позиция, нажатие, отпускание, ids), уходят как раньше.
    //
    // Исключение — палец аимбота (aimFinger). Аимбот держит камеру на цели
    // ПЕРЕСЫЛКОЙ одной и той же точки каждый кадр (UpdateAim: «держим тач живым,
    // ничего не двигаем»), и в сборке до этих правок такие пакеты уходили всегда.
    // Как только мы начали их глушить, аим перестал удерживать прицел: у игры
    // палец числится нажатым, а удержание камеры теряется (автофарм при этом
    // работает — его пальцы меняют позицию каждый такт). Поэтому повтор
    // пропускаем, только когда пакет НЕ несёт пальца аимбота: экономия на
    // простое бота сохраняется полностью, а путь аимбота остаётся прежним.
    const size_t bytes = sizeof(struct input_event) * (size_t)tmpCnt;
    if (!aimFinger && bytes == g_touch_last_bytes && memcmp(input.event, g_touch_last_packet, bytes) == 0) {
        g_touch_first_down = isFirstDown;
        ++g_touch_upload_calls;      // вызов был, но отправки не потребовал
        ++g_touch_upload_skipped;
        g_touch_upload_ms += TouchNowMs() - t0;
        pthread_mutex_unlock(&g_touch_mutex);
        return;
    }
    memcpy(g_touch_last_packet, input.event, bytes);
    g_touch_last_bytes = bytes;

    if (is && isFirstDown) {
        isFirstDown = false;
        write(nowfd, &input, sizeof(struct input_event) * (tmpCnt + 2));
    } else {
        write(nowfd, input.event, sizeof(struct input_event) * tmpCnt);
    }
    g_touch_first_down = isFirstDown;

    ++g_touch_upload_calls;
    g_touch_upload_ms += TouchNowMs() - t0;
    pthread_mutex_unlock(&g_touch_mutex);
}

// Единицы тачскрина -> пиксели оверлея (та же система координат, что у
// ImGui MousePos). Вынесено из TypeA отдельным шагом: снимок мультитача для
// фрикама (Touch_GetActive) считает те же координаты тем же преобразованием,
// иначе хит-тесты джойстика/обзора разъехались бы с одним пальцем мыши.
static void Touch_ToOverlay(int devX, int devY, float& x, float& y) {
    float sx = ::scale_x > 0.f ? ::scale_x : 1.f;
    float sy = ::scale_y > 0.f ? ::scale_y : 1.f;
    float xt = (float)devX / sx;
    float yt = (float)devY / sy;

    if (other_touch) {
        switch (orientation) {
            case 1:
                x = xt;
                y = yt;
                break;
            case 2:
                y = yt;
                x = screenHeight - xt;
                break;
            case 3:
                x = screenHeight - xt;
                y = screenWidth - yt;
                break;
            default:
                y = xt;
                x = screenHeight - yt;
                break;
        }
    } else {
        switch (orientation) {
            case 1:
                x = yt;
                y = screenHeight - xt;
                break;
            case 2:
                x = screenHeight - xt;
                y = screenWidth - yt;
                break;
            case 3:
                y = xt;
                x = screenWidth - yt;
                break;
            default:
                x = xt;
                y = yt;
                break;
        }
    }
}

static pthread_t s_threads[maxE]{};
static bool s_thread_alive[maxE]{};
static int s_real_touch_fd = -1;
static int s_min_event_num = -1;
static std::atomic<bool> g_touch_phys_down{false};
static std::atomic<bool> g_touch_press_unseen{false};
static std::atomic<float> g_touch_last_x{0.f};
static std::atomic<float> g_touch_last_y{0.f};

void Touch_FrameStep() {
    if (!Touch_initialized) return;
    ImGuiIO &io = ImGui::GetIO();
    if (g_touch_press_unseen.exchange(false, std::memory_order_acq_rel)) {
        io.MousePos = {g_touch_last_x.load(std::memory_order_relaxed),
                       g_touch_last_y.load(std::memory_order_relaxed)};
        io.MouseDown[0] = true;
    } else if (!g_touch_phys_down.load(std::memory_order_acquire)) {
        io.MouseDown[0] = false;
    }
}

static void *TypeA(void *arg) {
    targ tmp = *(targ *) arg;
    int i = tmp.fdNum;
    float S2TX = tmp.S2TX;
    float S2TY = tmp.S2TY;
    const int minX = tmp.minX;
    const int minY = tmp.minY;
    const bool single = tmp.singleTouch != 0;
    int latest = 0;
    bool seenTrackingId = false;
    input_event inputEvent[64]{0};

    if (single) {
        // На ВМ-тачпаде один палец: слот — всегда нулевой, id стабилен.
        Finger[i][0].id = (i * 2 + 1) * maxF;
    }

    while (Touch_initialized) {
        const int fd = origfd[i];
        if (fd <= 0) break;
        struct pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLIN;
        const int pr = poll(&pfd, 1, 50);
        if (!Touch_initialized) break;
        if (pr <= 0 || !(pfd.revents & POLLIN)) {
            continue;
        }
        auto readSize = (int32_t) read(fd, inputEvent, sizeof(inputEvent));
        if (readSize <= 0 || (readSize % sizeof(input_event)) != 0) {
            continue;
        }
        size_t count = size_t(readSize) / sizeof(input_event);
        for (size_t j = 0; j < count; j++) {
            input_event &ie = inputEvent[j];
            if (single) {
                // type-A: координаты в ABS_X/ABS_Y, нажатие — BTN_TOUCH; общий
                // хвост по SYN_REPORT ниже работает как для мультитача.
                if (ie.type == EV_ABS) {
                    if (ie.code == ABS_X) { Finger[i][0].x = (int) ((ie.value - minX) * S2TX); continue; }
                    if (ie.code == ABS_Y) { Finger[i][0].y = (int) ((ie.value - minY) * S2TY); continue; }
                }
                if (ie.type == EV_KEY && ie.code == BTN_TOUCH) {
                    const bool down = ie.value != 0;
                    if (down && !Finger[i][0].isDown) Finger[i][0].blockClassified = false;
                    if (!down) Finger[i][0].blockClassified = false;
                    Finger[i][0].isDown = down;
                    continue;
                }
            }
            if (ie.type == EV_KEY && ie.code == BTN_TOUCH && !single) {
                const bool down = ie.value != 0;
                if (!seenTrackingId) {
                    if (down) {
                        if (!Finger[i][latest].isDown) Finger[i][latest].blockClassified = false;
                        Finger[i][latest].id = (i * 2 + 1) * maxF + latest;
                        Finger[i][latest].isDown = true;
                    } else {
                        for (int s = 0; s < maxF; ++s) {
                            if (Finger[i][s].id == kAimFingerId ||
                                (Finger[i][s].id >= 61000 && Finger[i][s].id <= 61002))
                                continue;
                            Finger[i][s].isDown = false;
                            Finger[i][s].blocked = false;
                            Finger[i][s].blockClassified = false;
                        }
                    }
                }
                continue;
            }
            if (ie.type == EV_ABS) {
                if (ie.code == ABS_MT_SLOT) {
                    latest = ie.value;
                    if (latest < 0) latest = 0;
                    else if (latest >= maxF) latest = maxF - 1;
                    continue;
                }
                if (ie.code == ABS_MT_TRACKING_ID) {
                    seenTrackingId = true;
                    if (ie.value == -1) {
                        Finger[i][latest].isDown = false;
                        Finger[i][latest].blocked = false;
                        Finger[i][latest].blockClassified = false;
                    } else {
                        Finger[i][latest].id = (i * 2 + 1) * maxF + latest;
                        Finger[i][latest].isDown = true;
                        Finger[i][latest].blockClassified = false;
                    }
                    continue;
                }
                if (ie.code == ABS_MT_POSITION_X) {
                    Finger[i][latest].id = (i * 2 + 1) * maxF + latest;
                    Finger[i][latest].x = (int) ((ie.value - minX) * S2TX);
                    if (!seenTrackingId) Finger[i][latest].isDown = true;
                    continue;
                }
                if (ie.code == ABS_MT_POSITION_Y) {
                    Finger[i][latest].id = (i * 2 + 1) * maxF + latest;
                    Finger[i][latest].y = (int) ((ie.value - minY) * S2TY);
                    if (!seenTrackingId) Finger[i][latest].isDown = true;
                    continue;
                }
                if (!single && ie.code == ABS_X) {
                    Finger[i][latest].x = (int) ((ie.value - minX) * S2TX);
                    continue;
                }
                if (!single && ie.code == ABS_Y) {
                    Finger[i][latest].y = (int) ((ie.value - minY) * S2TY);
                    continue;
                }
            }
            if (ie.code == SYN_REPORT) {
                ImGuiIO &io = ImGui::GetIO();
                int bestDev = -1;
                int bestSlot = -1;
                for (int d = 0; d < fdNum; ++d) {
                    for (int s = 0; s < maxF; ++s) {
                        const int fid = Finger[d][s].id;
                        if (fid == kAimFingerId || (fid >= 61000 && fid <= 61002))
                            continue;
                        if (Finger[d][s].isDown) {
                            if (!Finger[d][s].blockClassified) {
                                float sx = 0.f, sy = 0.f;
                                Touch_ToOverlay(Finger[d][s].x, Finger[d][s].y, sx, sy);
                                Finger[d][s].blocked = Touch_IsBlocked(sx, sy);
                                Finger[d][s].blockClassified = true;
                            }
                            if (Finger[d][s].blocked) {
                                if (bestDev < 0 || !Finger[bestDev][bestSlot].blocked ||
                                    (d == i && s == latest)) {
                                    bestDev = d;
                                    bestSlot = s;
                                }
                            } else if (bestDev < 0 ||
                                       (!Finger[bestDev][bestSlot].blocked && d == i && s == latest)) {
                                bestDev = d;
                                bestSlot = s;
                            }
                        } else {
                            Finger[d][s].blocked = false;
                            Finger[d][s].blockClassified = false;
                        }
                    }
                }
                if (bestDev >= 0 && bestSlot >= 0 && Finger[bestDev][bestSlot].isDown) {
                    float x = 0.f, y = 0.f;
                    Touch_ToOverlay(Finger[bestDev][bestSlot].x, Finger[bestDev][bestSlot].y, x, y);
                    g_touch_last_x.store(x, std::memory_order_relaxed);
                    g_touch_last_y.store(y, std::memory_order_relaxed);
                    g_touch_phys_down.store(true, std::memory_order_release);
                    g_touch_press_unseen.store(true, std::memory_order_release);
                    io.MousePos = {x, y};
                    io.MouseDown[0] = true;
                } else {
                    g_touch_phys_down.store(false, std::memory_order_release);
                    if (!g_touch_press_unseen.load(std::memory_order_acquire)) {
                        io.MouseDown[0] = false;
                    }
                }
                if (!Touch_readOnly) {
                    Upload();
                }
                continue;
            }
        }
    }
    return nullptr;
}

static bool Touch_CreateUinput(int real_touch_fd, int minCnt, int screenX, int screenY) {
    char temp[128];
    struct uinput_user_dev ui_dev;
    nowfd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (nowfd <= 0) {
        nowfd = 0;
        return false;
    }

    int string_len = rand() % 10 + 5;
    char string[string_len];
    memset(&ui_dev, 0, sizeof(ui_dev));

    genRandomString(string, string_len);
    strncpy(ui_dev.name, string, UINPUT_MAX_NAME_SIZE);

    ui_dev.id.bustype = 0;
    ui_dev.id.vendor = rand() % 10 + 5;
    ui_dev.id.product = rand() % 10 + 5;
    ui_dev.id.version = rand() % 10 + 5;

    ioctl(nowfd, UI_SET_PROPBIT, INPUT_PROP_DIRECT);

    ioctl(nowfd, UI_SET_EVBIT, EV_ABS);
    ioctl(nowfd, UI_SET_ABSBIT, ABS_X);
    ioctl(nowfd, UI_SET_ABSBIT, ABS_Y);
    ioctl(nowfd, UI_SET_ABSBIT, ABS_MT_POSITION_X);
    ioctl(nowfd, UI_SET_ABSBIT, ABS_MT_POSITION_Y);
    ioctl(nowfd, UI_SET_ABSBIT, ABS_MT_TRACKING_ID);
    ioctl(nowfd, UI_SET_EVBIT, EV_SYN);
    ioctl(nowfd, UI_SET_EVBIT, EV_KEY);
    ioctl(nowfd, UI_SET_KEYBIT, BTN_TOOL_FINGER);
    ioctl(nowfd, UI_SET_KEYBIT, BTN_TOUCH);

    genRandomString(string, string_len);
    ioctl(nowfd, UI_SET_PHYS, string);

    if (real_touch_fd >= 0) {
        struct input_id real_id{};
        char real_name[UINPUT_MAX_NAME_SIZE] = {};
        char real_phys[UINPUT_MAX_NAME_SIZE] = {};
        if (ioctl(real_touch_fd, EVIOCGID, &real_id) == (int)sizeof(real_id))
            ui_dev.id = real_id;
        if (ioctl(real_touch_fd, EVIOCGNAME(sizeof(real_name) - 1), real_name) > 0 &&
            real_name[0])
            strncpy(ui_dev.name, real_name, UINPUT_MAX_NAME_SIZE - 1);
        if (ioctl(real_touch_fd, EVIOCGPHYS(sizeof(real_phys) - 1), real_phys) > 0 &&
            real_phys[0])
            ioctl(nowfd, UI_SET_PHYS, real_phys);
    }

    if (minCnt >= 0) {
        sprintf(temp, "/dev/input/event%d", minCnt);
        int fd = open(temp, O_RDWR);
        if (fd < 0) fd = open(temp, O_RDONLY);
        if (fd >= 0) {
            struct input_id id;
            if (ioctl(fd, EVIOCGID, &id) == (int)sizeof(id)) {
                ui_dev.id.bustype = id.bustype;
                ui_dev.id.vendor = id.vendor;
                ui_dev.id.product = id.product;
                ui_dev.id.version = id.version;
            }
            uint8_t *bits = NULL;
            ssize_t bits_size = 0;
            int res, j, k;
            while (1) {
                res = ioctl(fd, EVIOCGBIT(EV_KEY, bits_size), bits);
                if (res < bits_size)
                    break;
                bits_size = res + 16;
                bits = (uint8_t *) realloc(bits, bits_size * 2);
            }
            for (j = 0; j < res; j++) {
                for (k = 0; k < 8; k++)
                    if (bits[j] & 1 << k) {
                        if (j * 8 + k == BTN_TOUCH || j * 8 + k == BTN_TOOL_FINGER)
                            continue;
                        ioctl(nowfd, UI_SET_KEYBIT, j * 8 + k);
                    }
            }
            free(bits);
            close(fd);
        }
    }
    ui_dev.absmin[ABS_MT_POSITION_X] = 0;
    ui_dev.absmax[ABS_MT_POSITION_X] = screenX;
    ui_dev.absmin[ABS_MT_POSITION_Y] = 0;
    ui_dev.absmax[ABS_MT_POSITION_Y] = screenY;
    ui_dev.absmin[ABS_X] = 0;
    ui_dev.absmax[ABS_X] = screenX;
    ui_dev.absmin[ABS_Y] = 0;
    ui_dev.absmax[ABS_Y] = screenY;
    ui_dev.absmin[ABS_MT_TRACKING_ID] = 0;
    ui_dev.absmax[ABS_MT_TRACKING_ID] = 65535;
    write(nowfd, &ui_dev, sizeof(ui_dev));

    if (ioctl(nowfd, UI_DEV_CREATE)) {
        close(nowfd);
        nowfd = 0;
        return false;
    }
    return true;
}

bool Touch_Init(int w, int h, uint32_t orientation_, bool readOnly) {
    char temp[128];
    DIR *dir = opendir("/dev/input/");
    if (!dir) return false;
    dirent *ptr = NULL;
    std::vector<int> evNums;
    while ((ptr = readdir(dir)) != NULL) {
        int ev = -1;
        if (sscanf(ptr->d_name, "event%d", &ev) == 1 && ev >= 0) {
            evNums.push_back(ev);
        }
    }
    closedir(dir);
    std::sort(evNums.begin(), evNums.end());

    struct CandDev {
        int fd;
        int evNum;
        int kind;
        input_absinfo ax;
        input_absinfo ay;
    };
    std::vector<CandDev> cands;
    bool haveMultiTouch = false;
    for (int ev : evNums) {
        sprintf(temp, "/dev/input/event%d", ev);
        int fd = open(temp, O_RDWR);
        if (fd < 0) fd = open(temp, O_RDONLY);
        if (fd < 0) continue;
        const int kind = checkDeviceIsTouch(fd);
        if (kind != 0) {
            input_absinfo ax{}, ay{};
            int tmp1 = ioctl(fd, EVIOCGABS(kind == 1 ? ABS_MT_POSITION_X : ABS_X), &ax);
            int tmp2 = ioctl(fd, EVIOCGABS(kind == 1 ? ABS_MT_POSITION_Y : ABS_Y), &ay);
            if ((tmp1 != 0 || ax.maximum <= ax.minimum) && kind == 1)
                tmp1 = ioctl(fd, EVIOCGABS(ABS_X), &ax);
            if ((tmp2 != 0 || ay.maximum <= ay.minimum) && kind == 1)
                tmp2 = ioctl(fd, EVIOCGABS(ABS_Y), &ay);
            if (tmp1 == 0 && tmp2 == 0 &&
                ax.maximum > ax.minimum && ay.maximum > ay.minimum) {
                if (kind == 1) haveMultiTouch = true;
                cands.push_back({fd, ev, kind, ax, ay});
                continue;
            }
        }
        close(fd);
    }

    struct input_absinfo absX[maxE]{}, absY[maxE]{};
    static int s_single[maxE]{};
    int screenX = 0, screenY = 0, minCnt = 999999;
    int minKind = 0;
    int real_touch_fd = -1;
    fdNum = 0;
    for (const CandDev& c : cands) {
        if (haveMultiTouch && c.kind != 1) {
            close(c.fd);
            continue;
        }
        if (fdNum >= maxE) {
            close(c.fd);
            continue;
        }
        origfd[fdNum] = c.fd;
        absX[fdNum] = c.ax;
        absY[fdNum] = c.ay;
        s_single[fdNum] = (c.kind == 2) ? 1 : 0;
        if (!readOnly) {
            ioctl(c.fd, EVIOCGRAB, GRAB);
        }
        const int spanX = c.ax.maximum - c.ax.minimum;
        const int spanY = c.ay.maximum - c.ay.minimum;
        if (c.evNum < minCnt && spanX > 0 && spanY > 0) {
            screenX = spanX;
            screenY = spanY;
            minCnt = c.evNum;
            minKind = c.kind;
            real_touch_fd = c.fd;
        }
        fdNum++;
    }

    if (fdNum <= 0 || screenX <= 0 || screenY <= 0) {
        puts("Failed init touch!");
        return false;
    }
    printf("touch: event%d %s\n", minCnt, minKind == 2 ? "single-touch (vm pad)" : "multitouch");

    s_real_touch_fd = real_touch_fd;
    s_min_event_num = minCnt;

    if (!readOnly) {
        if (!Touch_CreateUinput(real_touch_fd, minCnt, screenX, screenY)) {
            for (int k = 0; k < fdNum; ++k) {
                ioctl(origfd[k], EVIOCGRAB, UNGRAB);
                close(origfd[k]);
                origfd[k] = 0;
            }
            fdNum = 0;
            return false;
        }
    }
    Touch_initialized = true;
    Touch_readOnly = readOnly;

    for (int i = 0; i < fdNum; i++) {
        const int spanX = absX[i].maximum - absX[i].minimum;
        const int spanY = absY[i].maximum - absY[i].minimum;
        targF[i].fdNum = i;
        targF[i].S2TX = (spanX > 0) ? ((float) screenX / (float) spanX) : 1.f;
        targF[i].S2TY = (spanY > 0) ? ((float) screenY / (float) spanY) : 1.f;
        targF[i].minX = absX[i].minimum;
        targF[i].minY = absY[i].minimum;
        targF[i].singleTouch = s_single[i];
        s_thread_alive[i] = (pthread_create(&s_threads[i], NULL, TypeA, &targF[i]) == 0);
    }

    ::screenWidth = w;
    ::screenHeight = h;
    ::orientation = orientation_;
    devMaxX = screenX;
    devMaxY = screenY;
    Touch_UpdateScale();

    system("chmod 000 -R /proc/bus/input/*");
    return true;
}

bool Touch_IsInitialized() {
    return Touch_initialized && fdNum > 0;
}

bool Touch_TryUpgradeInject() {
    if (!Touch_initialized || !Touch_readOnly || fdNum <= 0)
        return Touch_initialized && !Touch_readOnly;
    if (devMaxX <= 0 || devMaxY <= 0) return false;
    if (!Touch_CreateUinput(s_real_touch_fd, s_min_event_num, devMaxX, devMaxY))
        return false;
    for (int k = 0; k < fdNum; ++k) {
        if (origfd[k] > 0) ioctl(origfd[k], EVIOCGRAB, GRAB);
    }
    Touch_readOnly = false;
    return true;
}
void UpdateScreenData(int w, int h, uint32_t orientation_) {
    ::screenWidth = w;
    ::screenHeight = h,
    ::orientation = orientation_;
    Touch_UpdateScale();
}

// Есть ли у устройства клавиша keyCode в EV_KEY (нам важен BTN_TOUCH у
// однотачевых — без неё «ABS_X/ABS_Y» может оказаться джойстиком).
static bool checkDeviceHasKey(int fd, int keyCode) {
    uint8_t *bits = NULL;
    ssize_t bits_size = 0;
    int res, j, k;
    bool found = false;
    while (true) {
        res = ioctl(fd, EVIOCGBIT(EV_KEY, bits_size), bits);
        if (res < bits_size)
            break;
        bits_size = res + 16;
        bits = (uint8_t *) realloc(bits, bits_size * 2);
    }
    for (j = 0; j < res && !found; j++)
        for (k = 0; k < 8 && !found; k++)
            if (bits[j] & 1 << k && j * 8 + k == keyCode)
                found = true;
    free(bits);
    return found;
}

static bool checkDeviceHasProp(int fd, int propCode) {
    unsigned long bits[(INPUT_PROP_MAX + 8 * sizeof(unsigned long)) /
                       (8 * sizeof(unsigned long))]{};
    if (ioctl(fd, EVIOCGPROP(sizeof(bits)), bits) < 0) return false;
    const int width = 8 * (int)sizeof(unsigned long);
    return (bits[propCode / width] & (1UL << (propCode % width))) != 0;
}

static int checkDeviceIsTouch(int fd) {
    uint8_t *bits = NULL;
    ssize_t bits_size = 0;
    int res, j, k;
    touchfilter::Caps caps{};
    struct input_absinfo abs{};
    while (true) {
        res = ioctl(fd, EVIOCGBIT(EV_ABS, bits_size), bits);
        if (res < bits_size)
            break;
        bits_size = res + 16;
        bits = (uint8_t *) realloc(bits, bits_size * 2);
    }
    for (j = 0; j < res; j++) {
        for (k = 0; k < 8; k++)
            if (bits[j] & 1 << k && ioctl(fd, EVIOCGABS(j * 8 + k), &abs) == 0) {
                if (j * 8 + k == ABS_MT_POSITION_X) caps.mt_x = true;
                if (j * 8 + k == ABS_MT_POSITION_Y) caps.mt_y = true;
                if (j * 8 + k == ABS_X) caps.abs_x = true;
                if (j * 8 + k == ABS_Y) caps.abs_y = true;
            }
    }
    free(bits);
    caps.btn_touch = checkDeviceHasKey(fd, BTN_TOUCH);
    caps.tool_pen = checkDeviceHasKey(fd, BTN_TOOL_PEN);
    caps.direct = checkDeviceHasProp(fd, INPUT_PROP_DIRECT);
    caps.pointer = checkDeviceHasProp(fd, INPUT_PROP_POINTER);
    return (int)touchfilter::classify(caps);
}

void Touch_Close() {
    if (Touch_initialized) {
        Touch_initialized = false;
        for (int i = 0; i < maxE; ++i) {
            if (s_thread_alive[i]) {
                pthread_join(s_threads[i], nullptr);
                s_thread_alive[i] = false;
            }
            if (origfd[i] > 0) {
                if (!Touch_readOnly)
                    ioctl(origfd[i], EVIOCGRAB, UNGRAB);
                close(origfd[i]);
                origfd[i] = 0;
            }
        }
        if (nowfd > 0) {
            ioctl(nowfd, UI_DEV_DESTROY);
            close(nowfd);
            nowfd = 0;
        }
        s_real_touch_fd = -1;
        s_min_event_num = -1;
        fdNum = 0;
        g_touch_phys_down.store(false, std::memory_order_release);
        g_touch_press_unseen.store(false, std::memory_order_release);
        memset(Finger, 0, sizeof(Finger));
        memset(input.event, 0, sizeof(input.event));
    }
}

float Touch_DeviceUnitsPerPixel() {
    if (!Touch_initialized || devMaxX <= 0 || devMaxY <= 0) return 1.0f;
    float s = ::scale_x < ::scale_y ? ::scale_x : ::scale_y;
    return (s > 0.01f && s < 100.0f) ? s : 1.0f;
}

int Touch_GetActive(float* xs, float* ys, int* ids, int maxCount) {
    if (!xs || !ys || !ids || maxCount <= 0) return 0;
    if (!Touch_initialized) return 0;
    pthread_mutex_lock(&g_touch_mutex);
    int n = 0;
    for (int i = 0; i < fdNum && n < maxCount; ++i) {
        for (int j = 0; j < maxF && n < maxCount; ++j) {
            const touchObj& f = Finger[i][j];
            if (!f.isDown) continue;
            // Синтетика аима/фарма — не пальцы игрока: в снимок не берём,
            // иначе injected-тач уводил бы джойстик и камеру фрикама.
            if (f.id == kAimFingerId) continue;
            if (f.id >= 61000 && f.id <= 61002) continue;
            float x = 0.f, y = 0.f;
            Touch_ToOverlay(f.x, f.y, x, y);
            if (!std::isfinite(x) || !std::isfinite(y)) continue;
            xs[n] = x; ys[n] = y; ids[n] = f.id;
            ++n;
        }
    }
    pthread_mutex_unlock(&g_touch_mutex);
    return n;
}

void Touch_Down(float xt, float yt) {
    if (!Touch_initialized || Touch_readOnly) return;
    // Keep sub-pixel precision all the way to the device grid: the touch
    // controller usually has finer resolution than the display, and the game
    // receives float coordinates. Truncating to whole screen pixels here would
    // make the smallest possible camera step larger than a head at range.
    float x = 0.0f, y = 0.0f;
    switch (orientation) {
        case 1: {
            x = ::screenHeight - yt;
            y = xt;
            break;
        }
        case 2: {
            x = ::screenHeight - xt;
            y = ::screenWidth - yt;
            break;
        }
        case 3: {
            x = yt;
            y = ::screenWidth - xt;
            break;
        }
        default: {
            x = xt;
            y = yt;
            break;
        }
    }
    if (x < 0.0f) x = 0.0f;
    if (y < 0.0f) y = 0.0f;
    touchObj &touch = Finger[0][kAimFingerSlot];
    touch.id = kAimFingerId;
    touch.x = (int) lroundf(x * ::scale_x);
    touch.y = (int) lroundf(y * ::scale_y);
    if (devMaxX > 0 && touch.x > devMaxX) touch.x = devMaxX;
    if (devMaxY > 0 && touch.y > devMaxY) touch.y = devMaxY;
    touch.isDown = true;
    Upload();
}
void Touch_Move(float x, float y) {
    Touch_Down(x, y);
}

void Touch_Up() {
    if (!Touch_initialized || Touch_readOnly) return;
    touchObj &touch = Finger[0][kAimFingerSlot];
    touch.isDown = false;
    Upload();
}

// ---- Extra synthetic fingers (auto-farm) ------------------------------------
// Same coordinate pipeline as Touch_Down(), but in their own slots (6..8) so
// they coexist with the aimbot finger in slot 9 and with real fingers.
void Touch_Down_N(int finger, float xt, float yt) {
    if (!Touch_initialized || Touch_readOnly) return;
    if (finger < 0 || finger > 2) return;
    float x = 0.0f, y = 0.0f;
    switch (orientation) {
        case 1: { x = ::screenHeight - yt; y = xt; break; }
        case 2: { x = ::screenHeight - xt; y = ::screenWidth - yt; break; }
        case 3: { x = yt; y = ::screenWidth - xt; break; }
        default: { x = xt; y = yt; break; }
    }
    if (x < 0.0f) x = 0.0f;
    if (y < 0.0f) y = 0.0f;
    touchObj &touch = Finger[0][6 + finger];
    touch.id = 61000 + finger;
    touch.x = (int) lroundf(x * ::scale_x);
    touch.y = (int) lroundf(y * ::scale_y);
    if (devMaxX > 0 && touch.x > devMaxX) touch.x = devMaxX;
    if (devMaxY > 0 && touch.y > devMaxY) touch.y = devMaxY;
    touch.isDown = true;
    Upload();
}

void Touch_Up_N(int finger) {
    if (!Touch_initialized || Touch_readOnly) return;
    if (finger < 0 || finger > 2) return;
    touchObj &touch = Finger[0][6 + finger];
    touch.isDown = false;
    Upload();
}

bool Touch_CanInject() {
    return Touch_initialized && !Touch_readOnly;
}
