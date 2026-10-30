#include "magic_farm.h"
#include "magic_farm_loader.h"
#include "farm.h"
#include "farm_joy.h"
#include "game.h"
#include "process.h"
#include "app_state.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <ctime>
#include <algorithm>

namespace magicfarm {
constexpr std::uint64_t kLoadRetryBaseNs = 2000000000ULL;
constexpr std::uint64_t kLoadRetryMaxNs = 30000000000ULL;
namespace {
std::atomic<State> ui{State::Idle};
std::uint64_t now_ns() {
    timespec t{};clock_gettime(CLOCK_MONOTONIC,&t);
    return std::uint64_t(t.tv_sec)*1000000000ULL+std::uint64_t(t.tv_nsec);
}
class Client {
    std::mutex mutex_;
    std::condition_variable wake_;
    std::thread worker_;
    bool stop_{},pending_{};
    int pid_{};
    std::uintptr_t base_{},context_{};
    std::uint64_t generation_{};
    std::uint64_t retry_after_{};
    unsigned load_failures_{};
    State loading_{State::Idle};
    void run() {
        std::unique_lock<std::mutex> lock(mutex_);
        for (;;) {
            wake_.wait(lock,[&]{return stop_ || pending_;});
            if (stop_) return;
            const auto pid=pid_;const auto base=base_;const auto generation=generation_;
            pending_=false;
            lock.unlock();const auto result=load_module(pid,base);lock.lock();
            if (generation_==generation && !stop_) {
                context_=result.shared;loading_=result.state;
                if (!result.shared && (result.state==State::LoadFailed || result.state==State::NoAccess)) {
                    const unsigned shift=std::min(load_failures_,4u);
                    retry_after_=now_ns()+std::min(kLoadRetryMaxNs,kLoadRetryBaseNs<<shift);
                    ++load_failures_;
                } else {
                    retry_after_=0;load_failures_=0;
                }
            }
        }
    }
    bool snapshot(std::uintptr_t& context,int& pid) {
        std::lock_guard<std::mutex> lock(mutex_);
        context=context_;pid=pid_;return context && pid>0;
    }
public:
    ~Client() { shutdown(); }
    void ensure(int pid,std::uintptr_t base) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_) return;
        const bool same=pid_==pid && base_==base;
        if (same) {
            if (context_ || loading_==State::Loading || now_ns()<retry_after_) return;
        } else {
            retry_after_=0;load_failures_=0;
        }
        pid_=pid;base_=base;context_=0;++generation_;pending_=true;loading_=State::Loading;
        if (!worker_.joinable()) {
            try {worker_=std::thread(&Client::run,this);}
            catch (...) {
                loading_=State::LoadFailed;pending_=false;
                retry_after_=now_ns()+kLoadRetryBaseNs;++load_failures_;
                return;
            }
        }
        wake_.notify_one();
    }
    State read(Shared& remote) {
        std::uintptr_t context=0;int pid=0;
        if (!snapshot(context,pid)) {
            std::lock_guard<std::mutex> lock(mutex_);return loading_;
        }
        if (!esp_silent_target(pid) || !esp_silent_read(context,&remote,sizeof(remote)) ||
            remote.magic!=kMagic || remote.abi!=kAbi || remote.size!=sizeof(Shared) ||
            remote.state>static_cast<unsigned>(State::Unavailable)) return State::LoadFailed;
        return static_cast<State>(remote.state);
    }
    bool publish(const Command& command) {
        std::uintptr_t context=0;int pid=0;Shared current{};
        if (!snapshot(context,pid) || !esp_silent_target(pid) ||
            !esp_silent_read(context,&current,sizeof(current)) || current.magic!=kMagic ||
            current.abi!=kAbi || current.size!=sizeof(Shared)) return false;
        struct Writer {
            bool write(std::uintptr_t a,const void* p,std::size_t n) {return esp_silent_write(a,p,n);}
        } writer;
        return publish_command(writer,context,current.sequence,command);
    }
    void clear() { (void)publish(Command{}); }
    void reset() {
        clear();std::lock_guard<std::mutex> lock(mutex_);
        ++generation_;pid_=0;base_=context_=0;pending_=false;loading_=State::Idle;
        retry_after_=0;load_failures_=0;
    }
    void shutdown() {
        clear();{
            std::lock_guard<std::mutex> lock(mutex_);stop_=true;pending_=false;++generation_;
        }
        wake_.notify_one();if (worker_.joinable()) worker_.join();
    }
};
Client& client() {static Client c;return c;}
bool active{};
std::uint64_t node_id{};
float previous_hp{-1},no_progress{},best_distance{1e9f},stuck{},evade{};
float ack_wait{};
unsigned attempts{};
bool requested_hit{};
bool inactive_handler{};
int session_pid{};
void reset_node() {
    node_id=0;previous_hp=-1;no_progress=stuck=evade=0;best_distance=1e9f;
    attempts=0;requested_hit=false;ack_wait=0;
}
void release() {farmjoy::release();g_farmPhase=0;}
}

State status() {return ui.load();}
const char* status_text() {
    switch (status()) {
    case State::Idle:return "Magic Farm: ожидание";
    case State::Loading:return "Magic Farm: подключение";
    case State::Ready:return "Magic Farm: готов";
    case State::Moving:return "Magic Farm: подход к ресурсу";
    case State::Sent:return "Magic Farm: запрос добычи отправлен";
    case State::OutOfReach:return "Magic Farm: вне дальности инструмента";
    case State::Blocked:return "Magic Farm: ресурс перекрыт";
    case State::NoTool:return "Magic Farm: нужен подходящий инструмент";
    case State::NoTarget:return "Magic Farm: поиск ресурсов";
    case State::WrongBuild:return "Magic Farm: нужен релиз 11991";
    case State::NoAccess:return "Magic Farm: нет доступа к процессу игры";
    case State::LoadFailed:return "Magic Farm: модуль не подключён · /data/local/benzware/magic.log";
    case State::HookFailed:return "Magic Farm: обработчик не подключён";
    case State::NoProgress:return "Magic Farm: сервер не подтвердил добычу";
    case State::Stuck:return "Magic Farm: цель недоступна";
    case State::Exception:return "Magic Farm: ошибка обработки в игре";
    default:return "Magic Farm: ожидание актуальных данных";
    }
}
void Stop() {
    if (active) {client().reset();release();active=false;}
    reset_node();inactive_handler=false;g_farmActive=false;g_farmPaused=false;g_farmPhase=0;ui=State::Idle;
}
void Shutdown() {Stop();client().shutdown();}

void Update(float dt) {
    dt=finite(dt)?std::max(0.f,std::min(dt,0.1f)):0.016f;
    farmjoy::tick(dt);
    if (g_target_pid!=session_pid) {Stop();session_pid=g_target_pid;}
    unsigned mask=0;
    if (g_state.farm_on) {
        if (g_state.farm_wood) mask|=1;
        if (g_state.farm_stone) mask|=2;
        if (g_state.farm_metal) mask|=4;
        if (g_state.farm_sulfur) mask|=8;
    }
    esp_farm_set_resources(mask);esp_farm_set_range(g_state.farm_range);
    if (!mask || !g_esp_attached) {Stop();return;}
    active=true;
    if (inactive_handler) {release();g_farmPaused=true;ui=State::HookFailed;return;}
    // Calibration owns the physical controls; normal camera swipes never pause us.
    if (g_calibMode || g_state.freecam_on) {
        client().clear();release();g_farmPaused=true;ui=State::Idle;return;
    }
    FarmTarget target{};
    const bool found=esp_farm_get_magic_target(target,node_id) && target.valid;
    esp_farm_debug(g_farmNodes,g_farmReason);
    if (!found || !target.network_id) {
        client().clear();release();g_farmActive=false;reset_node();ui=g_farmReason==6?State::NoTool:State::NoTarget;return;
    }
    g_farmActive=true;g_farmPaused=false;
    g_farmTgtDist=target.aim_dist;g_farmReach=target.melee_reach;
    g_farmTgtKind=target.kind;g_farmSpot=target.has_spot?target.spot_source:0;
    g_farmStreak=target.streak;g_farmSpotLife=target.spot_life;
    g_farmAttackPeriod=target.attack_period;g_farmXp=target.node_experience;
    g_farmToolHave=target.tool_purposes;
    g_farmHpPct=target.node_health>=0 && target.node_health_max>0?
        100.f*target.node_health/target.node_health_max:-1;
    if (!target.magic_supported || !target.game_base) {
        client().clear();release();g_farmPaused=true;ui=State::WrongBuild;return;
    }
    if (!finite(target.melee_reach) || target.melee_reach<0.2f ||
        !finite(target.attack_period) || target.attack_period<0.05f) {
        client().clear();release();g_farmPaused=true;ui=State::NoTool;return;
    }
    client().ensure(g_target_pid,target.game_base);
    Shared remote{};const State native=client().read(remote);
    ui=native;
    if (!ready(native)) {release();g_farmPaused=true;return;}
    if (node_id!=target.id) {reset_node();node_id=target.id;previous_hp=target.node_health;}
    if (remote.last_net_id!=target.network_id) ack_wait+=dt;
    else ack_wait=0;
    if (ack_wait>3.f) {inactive_handler=true;client().clear();release();g_farmPaused=true;ui=State::HookFailed;return;}
    if (target.node_health>=0 && previous_hp>=0 && target.node_health<previous_hp-0.001f) {
        no_progress=0;attempts=0;
    }
    previous_hp=target.node_health;
    if (native==State::Sent && remote.last_net_id==target.network_id) requested_hit=true;
    if (requested_hit && target.node_health>=0) no_progress+=dt;
    if (no_progress>20.f) {
        client().clear();esp_farm_blacklist(target.id,20.f);release();reset_node();ui=State::NoProgress;return;
    }
    Command c{};c.enabled=1;c.net_id=target.network_id;
    c.deadline_ns=now_ns()+kLeaseNs-1000000ULL;
    c.point={target.aim_x,target.aim_y,target.aim_z};
    if (!client().publish(c)) {release();g_farmPaused=true;ui=State::LoadFailed;return;}
    g_farmBlocked=native==State::Blocked;
    const bool own_status=remote.last_net_id==target.network_id;
    bool walk=target.walk_dist>0.30f;
    // Without a marker, stop as soon as a hit really reaches the node. With
    // a marker, walk to its side of the node to preserve the critical streak.
    if (!target.has_spot && own_status && native==State::Sent) walk=false;
    if (own_status && native==State::OutOfReach) walk=true;
    if (native==State::NoTool) {release();return;}
    farmjoy::observe(target.walk_dist,walk && evade<=0);
    if (walk) {
        if (target.walk_dist<best_distance-0.08f) {best_distance=target.walk_dist;stuck=0;}
        else stuck+=dt;
        if (stuck>2.0f && evade<=0) {
            if (++attempts>4) {
                client().clear();esp_farm_blacklist(target.id,20.f);release();reset_node();ui=State::Stuck;return;
            }
            evade=0.8f;stuck=0;
        }
        float x=0,y=0;
        const float yaw=evade>0?target.walk_yaw+(attempts&1?80.f:-80.f):
            (target.walk_dist<0.3f?target.magic_point_yaw:target.walk_yaw);
        movement(yaw,target.walk_dist>2.f?1.f:0.65f,x,y);
        evade=std::max(0.f,evade-dt);
        if (!farmjoy::command(x,y)) {ui=State::NoAccess;client().clear();release();return;}
        (void)farmjoy::sprint(target.walk_dist>5.f);
        g_farmPhase=2;ui=State::Moving;
    } else {
        farmjoy::release();g_farmPhase=3;stuck=0;best_distance=target.walk_dist;
    }
}
} // namespace magicfarm
