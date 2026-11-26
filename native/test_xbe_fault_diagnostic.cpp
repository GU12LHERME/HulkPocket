#include "xbe_fault_diagnostic.h"
#include "xbox_guest_address_space.h"
#include <cassert>
#include <string>
#include <sys/wait.h>
using namespace hulkpocket;
int main() {
    XboxGuestAddressSpace mem;
    int fd[2];assert(pipe(fd)==0);
    auto pid=fork();assert(pid>=0);
    if(pid==0){
        close(fd[0]);assert(dup2(fd[1],STDERR_FILENO)>=0);close(fd[1]);
        GuestFaultStack stack;
        InstallGuestFaultHandler(mem.Base());
        fault_ordinal=277;fault_thread=0x100;
        volatile auto* ptr=mem.Base()+0x08003000;
        *ptr=1;
        _exit(1);
    }
    close(fd[1]);char buf[512];std::string log;
    ssize_t n;
    while((n=read(fd[0],buf,sizeof(buf)))>0)log.append(buf,static_cast<size_t>(n));
    close(fd[0]);int status=0;assert(waitpid(pid,&status,0)==pid);
    assert(WIFEXITED(status)&&WEXITSTATUS(status)==128+SIGSEGV);
    assert(log.find("guest_address=0x0000000008003000")!=std::string::npos);
    assert(log.find("last_kernel=0x0000000000000115")!=std::string::npos);
}
