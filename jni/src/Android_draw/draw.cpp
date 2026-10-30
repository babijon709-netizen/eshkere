#include "Android_draw/draw.h"
#include "ImGui/Font/Font.h"
#include "overlay_geometry.h"
#include "scoped_text.h"
#include "game.h"                // esp_game_tick_now / esp_game_frame_hz (темп кадра)
#include <dirent.h>
#include <sys/system_properties.h>
#include <time.h>
#include <cmath>
#include <cstdlib>
#include <cstring>

EGLDisplay display = EGL_NO_DISPLAY;
EGLConfig  config;
EGLSurface surface = EGL_NO_SURFACE;
EGLContext context = EGL_NO_CONTEXT;

ANativeWindow *native_window;

static constexpr char kOverlayName[] = {0x53,0x75,0x72,0x66,0x61,0x63,0x65,0x00};
static uint32_t g_surface_width = 0, g_surface_height = 0;
static overlay_capture::Mode g_capture_mode;

int native_window_screen_x = 0;
int native_window_screen_y = 0;
anwc::ANativeWindowCreator::DisplayInfo displayInfo{0};
uint32_t orientation  = 0;
bool     g_Initialized = false;
ImGuiWindow *g_window  = nullptr;
// Темп оверлея: 60 кадров/с, а не пик дисплея.
//
// Оверлей — это своё окно (свой ANativeWindow «Surface») и свой EGL-контекст,
// vsync у него выключен (eglSwapInterval(display, 0)), поэтому без ограничения
// цикл крутился на пиковой герцовке панели: на устройстве вышло ~118 кадров/с
// при 8.5 мс на кадр, то есть ПОСТОЯННО занятое ядро и вдвое больше
// полноэкранных отрисовок, чем делает сама игра (а игра у пользователя capped
// на 60 ФПС). Показывать оверлею чаще, чем обновляется игра, всё равно нечего —
// каждый наш кадр читает память игры (syscall'ы на каждую позицию/кость) и
// рисует всю ESP-сцену заново. Отсюда и лаги: чем меньше работы на кадр (фарм
// выключен), тем больше витков в секунду и тем сильнее мы отъедаем ядро, GPU и
// композитор у игры — пользователь это и видел как «с выключенным фармом лагает
// ещё сильнее». Плюс 120 Гц на панели = вдвое больше тепла, а троттлинг на
// телефоне добивал остаток: в логе первые 90 с шли ровно, потом кадр вырос до
// 40-100 мс.
//
// Поэтому: темп больше не выпрашивает высокую герцовку панели и не берётся из
// константы, а снимается с самой игры (esp_game_frame_hz по тику кадра игры) —
// игре он не нужен, а композитор из-за него не работает вдвое чаще. Запасной
// темп — 60 Гц, он же работает, пока тик игры не читается.
// g_peak_hz остаётся только для диагностики (пишется в шапку лога).
//
// Частота игры не константа: 90/120 на панелях с высокой герцовкой, до 45 и ниже
// при троттлинге. Пока оверлей идёт своими 60 Гц, два таймера с близкими
// частотами дают биения — фаза медленно едет, камера снимается каждый раз в
// другой точке кадра игры, и визуалы «дышат» на панораме. Поэтому у игры берётся
// не только темп, но и фаза: см. OverlayWaitForGameTick.
static constexpr float kOverlayFallbackPaceHz = 60.f;
static float g_peak_hz = 60.f;
static float g_overlay_fps = 60.f;
static double g_prev_swap = 0.0;
static double g_pace_sleep_ms = 0.0;  // сколько последний кадр проспал до своего темпа
static double g_pace_tick_ms = 0.0;   // из них — ожидание начала кадра игры
static int g_refresh_tick = 0;
// Часы монотонного времени: нужны и темпу кадра (drawEnd), и опросу дисплея.
static double NowSeconds() {
    struct timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}


static float SnapHz(float h) {
    const float c[] = {60.f, 90.f, 120.f, 144.f, 165.f, 180.f, 240.f};
    float best = 60.f, bd = 1e9f;
    for (float x : c) {
        float d = fabsf(h - x);
        if (d < bd) { bd = d; best = x; }
    }
    if (bd <= 8.f) return best;
    if (h >= 50.f && h <= 240.f) return h;
    return 60.f;
}

static void ConsiderHz(float& best, float h) {
    if (h >= 50.f && h <= 240.f && h > best) best = h;
}

static void ScanModesFile(float& best, const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        float hz = 0.f;
        const char* p = line;
        while (*p) {
            if ((*p == '-' || *p == 'p' || *p == 'P' || *p == 'R' || *p == '@' || *p == ' ') && p[1] >= '0' && p[1] <= '9') {
                hz = strtof(p + 1, nullptr);
                ConsiderHz(best, hz);
            }
            ++p;
        }
    }
    fclose(f);
}

static float DetectPeakRefresh() {
    float best = 0.f;
    const char* props[] = {
        "persist.sys.sf.peak_refresh_rate",
        "ro.surface_flinger.max_frame_rate",
        "ro.vendor.display.default_fps",
        "persist.vendor.display.refresh_rate",
        "ro.vendor.display.refresh_rate",
        "vendor.display.refresh_rate",
        "persist.sys.display.refresh_rate",
        "ro.sf.lcd_fps"
    };
    char buf[128];
    for (const char* k : props) {
        memset(buf, 0, sizeof(buf));
        if (__system_property_get(k, buf) > 0)
            ConsiderHz(best, strtof(buf, nullptr));
    }
    DIR* d = opendir("/sys/class/drm");
    if (d) {
        while (dirent* e = readdir(d)) {
            if (e->d_name[0] == '.') continue;
            char path[320];
            snprintf(path, sizeof(path), "/sys/class/drm/%s/modes", e->d_name);
            ScanModesFile(best, path);
        }
        closedir(d);
    }
    ScanModesFile(best, "/sys/class/graphics/fb0/modes");
    ScanModesFile(best, "/sys/class/lcd/panel/panel_refresh_rate");
    if (best < 50.f) best = 60.f;
    return SnapHz(best);
}

// Tempo, по которому идёт оверлей: 60 Гц, и это ЖЁСТКИЙ потолок.
//
// Раньше темп равнялся частоте кадра игры (тик Move.Tick), то есть на панели
// 90/120 Гц цикл крутился 90/120 раз в секунду. Каждый такой виток — это полный
// съём мира (позиции, кости, маркеры: тысячи syscall'ов в чужую память), полная
// перерисовка ESP и ещё один кадр в очередь композитора. Игра при этом рисует
// свои кадры тем же GPU и тем же окном вывода: на 120 Гц оверлей забирал у неё
// вдвое больше, а очередь буферов (setBufferCount 4) упиралась в
// eglSwapBuffers — кадр ждал композитор, и всё это время на экране висел
// предыдущий кадр оверлея. С устройства это и читается как «ESP залип на
// несколько секунд», «фрикам/иксрей лагают»: и то, и другое живёт в этом же
// цикле и обновляется только вместе с кадром оверлея.
//
// Почему именно 60, а не «частота игры»: снимать чаще, чем игра перерисовывает
// камеру, незачем (между двумя кадрами игры позиции не меняются), а пол/потолок
// 60 ещё и снимает старую беду с оценкой частоты игры — она мерится по переходам
// тика в наших же отсчётах, поэтому длинная пауза занижала её, оверлей
// замедлялся под неё и уже не поднимался («визуалы ползли на 24-30 к/с»).
// Фаза кадра при этом остаётся привязанной к тику игры (OverlayWaitForGameTick).
static float OverlayPaceHz() {
    return kOverlayFallbackPaceHz;
}

static void ApplyOverlayRefresh(ANativeWindow* w, float hz) {
    if (!w) return;
    using SetFR2 = int32_t(*)(ANativeWindow*, float, int8_t, int8_t);
    using SetFR  = int32_t(*)(ANativeWindow*, float, int8_t);
    using SetBC  = int32_t(*)(ANativeWindow*, size_t);
    static SetFR2 setfr2 = (SetFR2)dlsym(RTLD_DEFAULT, BW_TEXT("ANativeWindow_setFrameRateWithChangeStrategy").c_str());
    static SetFR  setfr  = (SetFR)dlsym(RTLD_DEFAULT, BW_TEXT("ANativeWindow_setFrameRate").c_str());
    static SetBC  setbc  = (SetBC)dlsym(RTLD_DEFAULT, BW_TEXT("ANativeWindow_setBufferCount").c_str());
    static bool buffers_set = false;
    static float applied_hz = 0.f;
    static ANativeWindow* configured_window = nullptr;
    // Гистерезис подсказки частоты (см. ниже): цель должна продержаться
    // kHintDwellSec, а между двумя отправками проходит kHintMinGapSec.
    static constexpr double kHintDwellSec  = 1.5;
    static constexpr double kHintMinGapSec = 6.0;
    static float  pending_hz = 0.f;
    static double pending_since = 0.0;
    static double last_apply_at = -1e9;
    if (configured_window != w) {
        configured_window = w;
        buffers_set = false;
        applied_hz = 0.f;
        pending_hz = 0.f;
        pending_since = 0.0;
        last_apply_at = -1e9;   // новая поверхность — своя история подсказок
    }
    if (setbc && !buffers_set) { setbc(w, 4); buffers_set = true; }
    // hz <= 61 — сознательно: оверлей больше не переводит панель в 120 Гц ради
    // своих кадров (игра всё равно рисует 60, а композитор и батарея платят за
    // удвоенную герцовку).
    //
    // Подсказка частоты уходит композитору только когда сменилось ОКРУГЛЁННОЕ
    // значение — иначе SurfaceFlinger каждый раз пересчитывал выбор режима
    // панели, и на части устройств это давало переключение режима и секундное
    // залипание всей картинки раз в несколько секунд.
    //
    // Одного округления оказалось мало: оценка частоты игры гуляет вокруг порога
    // 61 Гц, и КАЖДЫЙ её заход за порог заново отправлял подсказку (предыдущая
    // ветка сбрасывала applied_hz в 0), то есть на ровном месте получалась та же
    // серия переключений режима — «ESP залип на пару секунд». Теперь цель должна
    // продержаться kHintDwellSec, между отправками выдерживается kHintMinGapSec,
    // а уход оценки ниже порога подсказку не сбрасывает и не переотправляет.
    // eglSwapInterval выставлен один раз при создании поверхности (init_egl) —
    // повторный вызов драйверу не нужен и на некоторых GPU дорогой.
    if (hz <= 61.f) return;
    const float target = SnapHz(hz);
    if (target == applied_hz) { pending_since = 0.0; return; }
    const double now = NowSeconds();
    if (pending_hz != target) { pending_hz = target; pending_since = now; return; }
    if (now - pending_since < kHintDwellSec) return;
    if (now - last_apply_at < kHintMinGapSec) return;
    applied_hz = target;
    pending_since = 0.0;
    last_apply_at = now;
    if (setfr2) setfr2(w, target, 0, 1);
    else if (setfr) setfr(w, target, 0);
}

float overlay_fps() {
    return g_overlay_fps;
}

// Сколько миллисекунд последний кадр проспал, дожидаясь своего темпа (60 Гц).
// В логе это разделяет «кадр тормозит из-за работы» и «кадр просто ждёт темп».
double overlay_pace_sleep_ms() {
    return g_pace_sleep_ms;
}

// Сколько из этого сна ушло на ожидание перехода тика игры (см.
// OverlayWaitForGameTick): ненулевое значение = фаза кадра поймана, нулевое =
// кадр шёл по таймеру (тик недоступен или мы уже опаздывали).
double overlay_pace_tick_ms() {
    return g_pace_tick_ms;
}

// Пиковая герцовка панели (диагностика): в шапку лога пишется вместе с темпом
// оверлея, чтобы 60 у оверлея не путали с ФПС игры.
float overlay_peak_hz() {
    return g_peak_hz;
}

// Tempo, по которому реально идёт оверлей: частота кадра игры (или запасные
// 60 Гц, пока тик игры не читается) — см. drawEnd.
float overlay_pace_hz() {
    return OverlayPaceHz();
}

bool initGUI_draw(uint32_t _screen_x, uint32_t _screen_y, bool log) {
    orientation = displayInfo.orientation;
    if (!init_egl(_screen_x, _screen_y, log)) return false;
    if (!ImGui_init()) return false;
    return true;
}

bool init_egl(uint32_t _screen_x, uint32_t _screen_y, bool log) {
    // Preserve the original, capture-visible behavior for startup and old configs.
    g_capture_mode.reset();
    g_surface_width = _screen_x;
    g_surface_height = _screen_y;
    ::native_window = anwc::ANativeWindowCreator::Create(kOverlayName, _screen_x, _screen_y, false);
    if (::native_window == nullptr) return false;
    ANativeWindow_acquire(native_window);
    // Explicitly clear both exclusion flags where the transaction API exists.
    // Older versions already created the layer without the legacy hide marker.
    (void)anwc::ANativeWindowCreator::SetCaptureVisible(native_window, true);

    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) return false;
    if (eglInitialize(display, 0, 0) != EGL_TRUE) return false;

    EGLint num_config = 0;
    const EGLint attribList[] = {
        EGL_SURFACE_TYPE,      EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE,   EGL_OPENGL_ES2_BIT,
        EGL_BLUE_SIZE,  5,
        EGL_GREEN_SIZE, 6,
        EGL_RED_SIZE,   5,
        EGL_BUFFER_SIZE, 32,
        EGL_DEPTH_SIZE,  16,
        EGL_STENCIL_SIZE, 8,
        EGL_NONE
    };
    const EGLint attrib_list[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };

    if (eglChooseConfig(display, attribList, &config, 1, &num_config) != EGL_TRUE) return false;

    EGLint egl_format;
    eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &egl_format);
    ANativeWindow_setBuffersGeometry(native_window, 0, 0, egl_format);

    context = eglCreateContext(display, config, EGL_NO_CONTEXT, attrib_list);
    if (context == EGL_NO_CONTEXT) return false;

    surface = eglCreateWindowSurface(display, config, native_window, nullptr);
    if (surface == EGL_NO_SURFACE) return false;

    if (!eglMakeCurrent(display, surface, surface, context)) return false;

    eglSwapInterval(display, 0);
    g_peak_hz = DetectPeakRefresh();          // только для шапки лога/диагностики
    ApplyOverlayRefresh(::native_window, OverlayPaceHz());

    return true;
}

// Опрос дисплея — это IPC в SurfaceFlinger (getDisplayState/getDisplayInfo), а не
// чтение локального поля. drawBegin зовёт его КАЖДЫЙ кадр, то есть на 120 Гц это
// 120 binder-транзакций в секунду в тот же процесс, который в это время
// композит кадры игры. Загруженный SF отвечает не мгновенно, и ожидание ответа
// стоит ровно в цикле оверлея: кадр (а с ним ESP, фрикам и иксрей) встаёт на
// всё время ответа — с устройства это и видно как «ESP залип на несколько
// секунд в случайный момент». Частота/ориентация дисплея меняются раз в
// секунды, а не раз в кадр: опрашиваем 10 раз в секунду, поворот подхватывается
// максимум через 100 мс (6 кадров при 60 Гц).
void screen_config() {
    static double next_poll = 0.0;
    const double now = NowSeconds();
    if (now < next_poll) return;
    const anwc::ANativeWindowCreator::DisplayInfo info =
        anwc::ANativeWindowCreator::GetDisplayInfo();
    // Пустой ответ (SF не отдал display state) оставляем прежней геометрией и
    // переспрашиваем следующим кадром: раньше пустой ответ затирал её нулями.
    if (info.width <= 0 || info.height <= 0) return;
    displayInfo = info;
    next_poll = now + 0.1;
}

bool ImGui_init() {
    if (g_Initialized) return true;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplAndroid_Init(native_window);
    ImGui_ImplOpenGL3_Init("#version 300 es");

    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.DisplaySize = ImVec2((float)native_window_screen_x, (float)native_window_screen_y);

    ImFontConfig font_cfg;
    font_cfg.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF(
        (void*)RobotoFont,
        (int)RobotoFont_size,
        28.0f,
        &font_cfg
    );

    ImGui::GetStyle().ScaleAllSizes(1.5f);
    ImGui::GetStyle().WindowRounding = 14.0f;

    g_Initialized = true;
    return true;
}

// Android 8–11 store the screenshot-exclusion marker only at layer creation.
// If a newer device lacks the private transaction symbols, use the same path.
// Prepare the new window/surface before dropping the old one, and keep the EGL
// context (and all ImGui/GL resources) across the switch.
static bool RecreateOverlayForCapture(bool visible) {
    if (!visible && !anwc::ANativeWindowCreator::CanCreateCaptureExcluded()) return false;
    EGLint format = 0;
    if (eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &format) != EGL_TRUE) return false;
    ANativeWindow* next = anwc::ANativeWindowCreator::Create(
        kOverlayName, g_surface_width, g_surface_height, !visible);
    if (!next) return false;
    ANativeWindow_acquire(next);
    auto discardWindow = [](ANativeWindow* window) {
        ANativeWindow_release(window);
        anwc::ANativeWindowCreator::Destroy(window);
    };
    if (ANativeWindow_setBuffersGeometry(next, 0, 0, format) != 0) {
        discardWindow(next);
        return false;
    }
    EGLSurface nextSurface = eglCreateWindowSurface(display, config, next, nullptr);
    if (nextSurface == EGL_NO_SURFACE) {
        discardWindow(next);
        return false;
    }
    if (eglMakeCurrent(display, nextSurface, nextSurface, context) != EGL_TRUE) {
        eglDestroySurface(display, nextSurface);
        discardWindow(next);
        return false;
    }

    ANativeWindow* oldWindow = native_window;
    EGLSurface oldSurface = surface;
    native_window = next;
    surface = nextSurface;
    ImGui_ImplAndroid_SetWindow(next);
    eglSwapInterval(display, 0);
    ApplyOverlayRefresh(next, OverlayPaceHz());
    eglDestroySurface(display, oldSurface);
    discardWindow(oldWindow);
    return true;
}

bool overlay_capture_visible() { return g_capture_mode.visible(); }

bool overlay_set_capture_visible(bool visible) {
    if (!g_Initialized || !native_window || display == EGL_NO_DISPLAY ||
        context == EGL_NO_CONTEXT || surface == EGL_NO_SURFACE) return false;
    return g_capture_mode.set(visible,
        [](bool target) {
            return anwc::ANativeWindowCreator::SetCaptureVisible(native_window, target) ==
                   anwc::ANativeWindowCreator::CaptureChange::Applied;
        },
        [](bool target) { return RecreateOverlayForCapture(target); });
}

void drawBegin() {
    screen_config();

    if ((++g_refresh_tick % 180) == 0)
        ApplyOverlayRefresh(::native_window, OverlayPaceHz());

    if (::orientation != displayInfo.orientation) {
        ::orientation = displayInfo.orientation;
        UpdateScreenData(displayInfo.width, displayInfo.height, displayInfo.orientation);
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame();
    Touch_FrameStep();
    {
        // ImGui canvas обязан совпадать с квадратным Surface из main.cpp.
        // Так поворот устройства не меняет систему координат EGL и portrait
        // больше не обрезает интерфейс до landscape-высоты. ESP продолжает
        // проецировать игру в видимую landscape-область (max x min), а меню
        // использует фактические DisplayInfo width/height для границ экрана.
        const OverlayGeometry overlay = MakeOverlayGeometry(
            displayInfo.width, displayInfo.height,
            native_window_screen_x, native_window_screen_y);
        const float side = static_cast<float>(overlay.surface_side);
        ImGui::GetIO().DisplaySize = ImVec2(side, side);
    }
    ImGui::NewFrame();
}

static double MonotonicSeconds() {
    struct timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1000000000.0;
}

// Ожидание НАЧАЛА следующего кадра игры (тик Move.Tick локального игрока).
//
// Сон по своему таймеру держит средний темп, но не фазу: пока наш кадр и кадр
// игры идут каждый своим таймером, точка съёма камеры медленно едет по кадру
// игры (биения), и визуалы «дышат» на панораме. Ожидание перехода тика ставит
// следующий съём памяти игры сразу за началом её кадра: фаза фиксируется, а
// остаётся постоянный (незаметный) сдвиг меньше кадра.
//
// Ожидание ограничено 60% периода: если тик не пришёл (мы уже опоздали или он
// замолчал — смерть, рагдолл, пауза в меню), кадр дорисовывается таймерным
// сном, и цикл идёт с обычной частотой, а не «ждёт сигнал, которого не будет».
// Тик, замолчавший на четыре промаха подряд, на секунду отключает привязку.
static bool OverlayWaitForGameTick(double start, double target, double& waited, bool allow_wait) {
    static uint32_t s_tick = 0;
    static bool     s_tick_valid = false;
    static int      s_misses = 0;
    static double   s_off_until = 0.0;
    waited = 0.0;
    if (start < s_off_until) return false;
    uint32_t tick = 0;
    if (!esp_game_tick_now(tick)) {
        s_tick_valid = false;
        return false;
    }
    if (!s_tick_valid) {
        s_tick_valid = true;
        s_tick = tick;
        return false;   // первый кадр: сравнивать не с чем
    }
    if (tick != s_tick) {
        // Игра уже начала новый кадр с прошлого раза — ждать нечего.
        s_tick = tick;
        return false;
    }
    // Тик прочитан (оценка частоты игры питается именно этим чтением), но ждать
    // его не нужно: игра медленнее оверлея, привязка к её фазе не имеет смысла.
    if (!allow_wait) return false;
    const double limit = start + target * 0.6;
    for (;;) {
        struct timespec req{};
        req.tv_nsec = 200000;   // 0.2 мс: тик приходит раз в кадр игры
        clock_nanosleep(CLOCK_MONOTONIC, 0, &req, nullptr);
        const double at = MonotonicSeconds();
        uint32_t again = 0;
        if (!esp_game_tick_now(again)) {
            s_tick_valid = false;
            s_misses = 0;
            return false;
        }
        if (again != s_tick) {
            s_tick = again;
            waited = at - start;
            s_misses = 0;
            return true;
        }
        if (at >= limit) break;
    }
    waited = MonotonicSeconds() - start;
    if (++s_misses >= 4) {
        s_misses = 0;
        s_off_until = MonotonicSeconds() + 1.0;
    }
    return false;
}

void drawEnd() {
    ImGui::Render();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    eglSwapBuffers(display, surface);

    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double now = (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
    // Темп — кадр игры (см. комментарий к kOverlayFallbackPaceHz): esp_game_frame_hz
    // считает его по переходу тика Move.Tick, а пока тик не читается — 60 Гц.
    const float game_hz = esp_game_frame_hz();
    const float pace_hz = OverlayPaceHz();
    const double target = 1.0 / (double)pace_hz;

    // Сначала — привязка к фазе игры: ждём перехода тика (ограниченно, см.
    // OverlayWaitForGameTick). Если тик пришёл, наш следующий съём памяти игры
    // идёт сразу за началом её кадра, и фаза зафиксирована. Не пришёл — работаем
    // по таймеру ниже, как раньше: ожидание ограничено частью периода, поэтому
    // кадр от него не растягивается.
    double tick_wait = 0.0;
    // Привязка к фазе осмысленна, только когда игра идёт не медленнее оверлея:
    // иначе тик за 60% периода не приходит почти никогда, и каждый кадр впустую
    // крутил опрос памяти по 0.2 мс (десятки syscall'ов), а серия промахов
    // выключала привязку на секунду и включала снова — темп кадра гулял по кругу.
    const bool tick_aligned = OverlayWaitForGameTick(
        now, target, tick_wait, game_hz >= kOverlayFallbackPaceHz - 5.0F);
    now += tick_wait;

    double slept = 0.0;
    if (!tick_aligned && g_prev_swap > 0.0) {
        double elapsed = now - g_prev_swap;
        if (elapsed > 0.0 && elapsed < target - 0.00035) {
            double sl = target - elapsed;
            struct timespec req{};
            req.tv_sec = (time_t)sl;
            req.tv_nsec = (long)((sl - (double)req.tv_sec) * 1000000000.0);
            clock_nanosleep(CLOCK_MONOTONIC, 0, &req, nullptr);
            clock_gettime(CLOCK_MONOTONIC, &ts);
            const double after = (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
            slept = after - now;
            now = after;
        }
    }
    g_pace_tick_ms = tick_wait * 1000.0;
    g_pace_sleep_ms = slept * 1000.0;
    if (g_prev_swap > 0.0) {
        float d = (float)(now - g_prev_swap);
        if (d > 0.0002f && d < 0.25f)
            g_overlay_fps += (1.f / d - g_overlay_fps) * 0.12f;
    }
    g_prev_swap = now;
}

void shutdown() {
    if (!g_Initialized) return;

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplAndroid_Shutdown();
    ImGui::DestroyContext();

    if (display != EGL_NO_DISPLAY) {
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (context != EGL_NO_CONTEXT) eglDestroyContext(display, context);
        if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
        eglTerminate(display);
    }
    display = EGL_NO_DISPLAY;
    context = EGL_NO_CONTEXT;
    surface = EGL_NO_SURFACE;

    ANativeWindow_release(native_window);
    anwc::ANativeWindowCreator::Destroy(native_window);
    native_window = nullptr;
    g_capture_mode.reset();
    g_Initialized = false;
}
