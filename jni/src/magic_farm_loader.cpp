// Android arm64 root loader. No process hiding or security-policy changes.
#include "magic_farm_loader.h"
#include <android/dlext.h>
#include <dlfcn.h>
#include <elf.h>
#include <sys/ptrace.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <csignal>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <algorithm>
#include <sys/mman.h>

namespace magicfarm {
namespace {
alignas(16) const unsigned char module_bytes[] = {
#include "magic_farm_blob.inc"
};
std::uint64_t milliseconds() {
    timespec t{};clock_gettime(CLOCK_MONOTONIC,&t);
    return std::uint64_t(t.tv_sec)*1000+std::uint64_t(t.tv_nsec)/1000000;
}
int unity_thread(int pid) {
    char path[96];std::snprintf(path,sizeof(path),"/proc/%d/task",pid);
    DIR* dir=opendir(path);if (!dir) return -1;
    int found=-1;
    while (auto* item=readdir(dir)) {
        char* end=nullptr;long tid=std::strtol(item->d_name,&end,10);
        if (!end || *end || tid<=0 || tid>0x7fffffff) continue;
        std::snprintf(path,sizeof(path),"/proc/%d/task/%ld/comm",pid,tid);
        FILE* f=std::fopen(path,"r");if (!f) continue;
        char name[64]{};const bool ok=std::fgets(name,sizeof(name),f)!=nullptr;std::fclose(f);
        if (ok && std::strcmp(name,"UnityMain\n")==0) { found=static_cast<int>(tid);break; }
    }
    closedir(dir);return found;
}
std::uintptr_t remote_function(int pid,void* local) {
    Dl_info info{};if (!dladdr(local,&info) || !info.dli_fname || !info.dli_fbase) return 0;
    const char* leaf=std::strrchr(info.dli_fname,'/');leaf=leaf?leaf+1:info.dli_fname;
    char path[64];std::snprintf(path,sizeof(path),"/proc/%d/maps",pid);
    FILE* f=std::fopen(path,"r");if (!f) return 0;
    char* line=nullptr;size_t cap=0;std::uintptr_t base=0;
    while (getline(&line,&cap,f)>=0) {
        unsigned long start=0,end=0,offset=0;char name[512]{};
        if (std::sscanf(line,"%lx-%lx %*4s %lx %*s %*s %511s",&start,&end,&offset,name)!=4 || offset) continue;
        const char* remote_leaf=std::strrchr(name,'/');remote_leaf=remote_leaf?remote_leaf+1:name;
        if (std::strcmp(leaf,remote_leaf)==0) {base=start;break;}
    }
    std::free(line);std::fclose(f);
    return base?base+(reinterpret_cast<std::uintptr_t>(local)-reinterpret_cast<std::uintptr_t>(info.dli_fbase)):0;
}
struct Registers { std::uint64_t x[31],sp,pc,pstate; };
class Trace {
    int tid_;
    Registers saved_{};
    alignas(16) unsigned char fp_[528]{};
    bool attached_{},saved_ok_{},fp_ok_{},stopped_{},usable_{true};
    std::uintptr_t stack_{};
    bool regset(int request,int type,void* data,std::size_t length) {
        iovec io{data,length};
        return ptrace(request,tid_,reinterpret_cast<void*>(type),&io)==0 && io.iov_len==length;
    }
    bool wait_stop(unsigned timeout) {
        const auto deadline=milliseconds()+timeout;
        while (milliseconds()<deadline) {
            int s=0;const int result=waitpid(tid_,&s,__WALL|WNOHANG);
            if (result==tid_) {stopped_=WIFSTOPPED(s);return stopped_;}
            if (result<0 && errno!=EINTR) return false;
            usleep(1000);
        }
        return false;
    }
public:
    explicit Trace(int tid):tid_(tid) {}
    ~Trace() {
        if (!attached_) return;
        if (!stopped_) { (void)ptrace(PTRACE_INTERRUPT,tid_,nullptr,nullptr);(void)wait_stop(1000); }
        if (stopped_) {
            if (fp_ok_) (void)regset(PTRACE_SETREGSET,NT_PRFPREG,fp_,sizeof(fp_));
            if (saved_ok_) (void)regset(PTRACE_SETREGSET,NT_PRSTATUS,&saved_,sizeof(saved_));
            (void)ptrace(PTRACE_DETACH,tid_,nullptr,nullptr);
        }
    }
    bool attach() {
        // Do not hijack an interrupted syscall: its restart machinery can
        // rewrite PC/x0 on resume. Sample until UnityMain is in user code.
        constexpr int arm_system_call=0x404; // NT_ARM_SYSTEM_CALL, AArch64 UAPI
        for (unsigned attempt=0;attempt<60;++attempt) {
            if (ptrace(PTRACE_SEIZE,tid_,nullptr,nullptr)) return false;
            attached_=true;
            if (ptrace(PTRACE_INTERRUPT,tid_,nullptr,nullptr) || !wait_stop(1000)) return false;
            int syscall_number=0;
            if (!regset(PTRACE_GETREGSET,arm_system_call,&syscall_number,sizeof(syscall_number))) return false;
            if (syscall_number!=-1) {
                if (ptrace(PTRACE_DETACH,tid_,nullptr,nullptr)) return false;
                attached_=stopped_=false;usleep(2000);continue;
            }
            saved_ok_=regset(PTRACE_GETREGSET,NT_PRSTATUS,&saved_,sizeof(saved_));
            fp_ok_=regset(PTRACE_GETREGSET,NT_PRFPREG,fp_,sizeof(fp_));
            return saved_ok_ && fp_ok_;
        }
        return false;
    }
    void stack(std::uintptr_t top) { stack_=top; }
    bool write(std::uintptr_t address,const void* data,std::size_t size) {
        const auto* bytes=static_cast<const unsigned char*>(data);
        for (std::size_t done=0;done<size;done+=sizeof(long)) {
            long value=0;const auto part=std::min(sizeof(long),size-done);
            if (part!=sizeof(long)) {
                errno=0;value=ptrace(PTRACE_PEEKDATA,tid_,reinterpret_cast<void*>(address+done),nullptr);
                if (errno) return false;
            }
            std::memcpy(&value,bytes+done,part);
            if (ptrace(PTRACE_POKEDATA,tid_,reinterpret_cast<void*>(address+done),reinterpret_cast<void*>(value))) return false;
        }
        return true;
    }
    bool call(std::uintptr_t function,const std::vector<std::uint64_t>& args,std::uint64_t& result) {
        if (!usable_ || !function || !stopped_ || args.size()>8) return false;
        Registers regs=saved_;
        for (unsigned i=0;i<8;++i) regs.x[i]=i<args.size()?args[i]:0;
        regs.sp=(stack_?stack_:saved_.sp-1024)&~std::uint64_t(15);
        regs.pc=function;regs.x[30]=0;
        regs.pstate&=~((3ULL<<10)|(1ULL<<21));
        if (!regset(PTRACE_SETREGSET,NT_PRSTATUS,&regs,sizeof(regs)) ||
            ptrace(PTRACE_CONT,tid_,nullptr,nullptr)) {usable_=false;return false;}
        stopped_=false;
        if (!wait_stop(2500) || !regset(PTRACE_GETREGSET,NT_PRSTATUS,&regs,sizeof(regs))) {usable_=false;return false;}
        if (regs.pc!=0) {usable_=false;return false;}
        result=regs.x[0];return true;
    }
};
} // namespace

Loaded load_module(int pid,std::uintptr_t game_base) {
    if (pid<=0 || !game_base || geteuid()!=0) return {0,State::NoAccess};
    const int tid=unity_thread(pid);
    if (tid<0) return {0,State::NoAccess};
    const auto mmap_fn=remote_function(pid,reinterpret_cast<void*>(&mmap));
    const auto munmap_fn=remote_function(pid,reinterpret_cast<void*>(&munmap));
    const auto syscall_fn=remote_function(pid,reinterpret_cast<void*>(&syscall));
    const auto close_fn=remote_function(pid,reinterpret_cast<void*>(&close));
    const auto load_fn=remote_function(pid,reinterpret_cast<void*>(&android_dlopen_ext));
    const auto symbol_fn=remote_function(pid,reinterpret_cast<void*>(&dlsym));
    if (!mmap_fn || !munmap_fn || !syscall_fn || !close_fn || !load_fn || !symbol_fn)
        return {0,State::LoadFailed};
    Trace trace(tid);
    if (!trace.attach()) return {0,State::NoAccess};
    std::uint64_t scratch=0,remote_fd=~0ULL,handle=0,attach_fn=0,context=0,ignored=0;
    constexpr std::size_t scratch_size=256*1024;
    if (!trace.call(mmap_fn,{0,scratch_size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,~0ULL,0},scratch) ||
        scratch<0x10000 || scratch>0x800000000000ULL) return {0,State::LoadFailed};
    trace.stack(scratch+scratch_size-16);
    bool ok=false;
    do {
        constexpr char fd_name[]="benzware_magic";
        if (!trace.write(scratch,fd_name,sizeof(fd_name)) ||
            !trace.call(syscall_fn,{__NR_memfd_create,scratch,1},remote_fd) || remote_fd>0x7fffffff) break;
        char fd_path[96];std::snprintf(fd_path,sizeof(fd_path),"/proc/%d/fd/%llu",pid,static_cast<unsigned long long>(remote_fd));
        const int fd=open(fd_path,O_WRONLY|O_CLOEXEC);
        if (fd<0) break;
        std::size_t done=0;
        while (done<sizeof(module_bytes)) {
            const auto n=::write(fd,module_bytes+done,sizeof(module_bytes)-done);
            if (n<0 && errno==EINTR) continue;
            if (n<=0) break;
            done+=static_cast<std::size_t>(n);
        }
        ::close(fd);
        if (done!=sizeof(module_bytes)) break;
        std::snprintf(fd_path,sizeof(fd_path),"/proc/self/fd/%llu",static_cast<unsigned long long>(remote_fd));
        android_dlextinfo info{};info.flags=ANDROID_DLEXT_USE_LIBRARY_FD;info.library_fd=static_cast<int>(remote_fd);
        if (!trace.write(scratch+256,fd_path,std::strlen(fd_path)+1) ||
            !trace.write(scratch+512,&info,sizeof(info)) ||
            !trace.call(load_fn,{scratch+256,RTLD_NOW|RTLD_LOCAL,scratch+512},handle) || !handle) break;
        constexpr char symbol[]="benzware_magic_attach";
        if (!trace.write(scratch+768,symbol,sizeof(symbol)) ||
            !trace.call(symbol_fn,{handle,scratch+768},attach_fn) || !attach_fn ||
            !trace.call(attach_fn,{game_base},context) || context<0x10000 || context>0x800000000000ULL) break;
        ok=true;
    } while (false);
    if (remote_fd<=0x7fffffff) (void)trace.call(close_fn,{remote_fd},ignored);
    // Restore the original stack before unmapping the scratch stack.
    trace.stack(0);(void)trace.call(munmap_fn,{scratch,scratch_size},ignored);
    return ok?Loaded{static_cast<std::uintptr_t>(context),State::Ready}:Loaded{0,State::LoadFailed};
}
} // namespace magicfarm
