#include "datapump/host/sandbox.hpp"
#include "sandbox_policy.hpp"
#include <cerrno>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <linux/audit.h>
#include <linux/seccomp.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
unsigned evaluate(const std::vector<sock_filter>& code,unsigned architecture,unsigned syscall) {
    unsigned accumulator=0;
    for(std::size_t pc=0;pc<code.size();++pc) {
        const auto& op=code[pc];
        if(op.code==(BPF_LD|BPF_W|BPF_ABS)) {
            if(op.k==offsetof(seccomp_data,nr))accumulator=syscall;
            else if(op.k==offsetof(seccomp_data,arch))accumulator=architecture;
            else throw std::runtime_error("unexpected policy field");
        } else if(op.code==(BPF_JMP|BPF_JEQ|BPF_K))pc+=accumulator==op.k?op.jt:op.jf;
        else if(op.code==(BPF_JMP|BPF_JSET|BPF_K))pc+=(accumulator&op.k)?op.jt:op.jf;
        else if(op.code==(BPF_RET|BPF_K))return op.k;
        else throw std::runtime_error("unexpected policy instruction");
    }
    throw std::runtime_error("policy did not terminate");
}
}
int main(){try {
#if defined(__x86_64__)
    constexpr unsigned architecture=AUDIT_ARCH_X86_64;
#elif defined(__aarch64__)
    constexpr unsigned architecture=AUDIT_ARCH_AARCH64;
#elif defined(__arm__)
    constexpr unsigned architecture=AUDIT_ARCH_ARM;
#else
    constexpr unsigned architecture=AUDIT_ARCH_I386;
#endif
    const auto filter=datapump::host::no_socket_filter();
    const auto deny=[&](unsigned syscall){require(evaluate(filter,architecture,syscall)==(SECCOMP_RET_ERRNO|EPERM),"socket operation not denied");};
    // Check the emitted executable filter, not source spelling, without ever
    // creating even a local socket or invoking any socket system call.
#ifdef SYS_socket
    for(unsigned syscall:{SYS_socket,SYS_socketpair,SYS_bind,SYS_connect,SYS_listen,SYS_accept,SYS_accept4,
            SYS_sendto,SYS_sendmsg,SYS_sendmmsg,SYS_recvfrom,SYS_recvmsg,SYS_recvmmsg,SYS_shutdown,
            SYS_getsockname,SYS_getpeername,SYS_getsockopt,SYS_setsockopt})deny(syscall);
#endif
#ifdef SYS_socketcall
    deny(SYS_socketcall);
#endif
#ifdef SYS_io_uring_setup
    deny(SYS_io_uring_setup);deny(SYS_io_uring_enter);deny(SYS_io_uring_register);
#endif
#ifdef SYS_pidfd_getfd
    deny(SYS_pidfd_getfd);
#endif
#ifdef SYS_ptrace
    deny(SYS_ptrace);
#endif
#ifdef SYS_bpf
    deny(SYS_bpf);
#endif
#if defined(__x86_64__)
    for(unsigned syscall=0;syscall<1024;++syscall)deny(syscall|0x40000000U);
#endif
    require(evaluate(filter,architecture^1U,SYS_read)==SECCOMP_RET_KILL_PROCESS,"foreign syscall architecture accepted");
    for(unsigned syscall:{SYS_read,SYS_write,SYS_close,SYS_futex,SYS_exit,SYS_exit_group})require(evaluate(filter,architecture,syscall)==SECCOMP_RET_ALLOW,"required local operation denied");
    datapump::host::install_no_socket_boundary();require(datapump::host::no_socket_boundary_active(),"kernel filter inactive");
    const auto child=fork();require(child>=0,"cannot verify inherited sandbox");if(!child)_exit(datapump::host::no_socket_boundary_active()?0:1);
    int status=0;require(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0,"child did not inherit sandbox");
    std::cout<<"No-socket filter policy and kernel inheritance verified without socket creation\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
