#include "hulk_xbe_preflight.h"
#ifdef HULK_ENABLE_ENTRY_DIAGNOSTIC
#include "xbe_entry_session.h"
#include "xbe_kernel_session.h"
#include "hulk_kernel_missing_data.h"
#include "hulk_kernel_launch_data.h"
#include "hulk_kernel_profile.h"
#include "hulk_kernel_objects.h"
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <dlfcn.h>
#include "xbe_fault_diagnostic.h"
#endif
#include "xbe_bootstrap.h"
#include "xbe_kernel_imports.h"
#include "xbe_image_memory_loader.h"
#include "xbox_guest_address_space.h"
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

// Runs locally: no XBE data is copied to GitHub or printed.
// Supplying an ELF module calls dlopen; only use modules built yourself
// from your own game and trusted source code.
int main(int argc,char** argv) {
    std::vector<std::string> paths;
    std::string disc_root;
    bool imports_requested=false, bootstrap_requested=false, execute_requested=false;
    for (int i=1; i<argc; ++i) {
        const std::string arg(argv[i]);
        if (arg=="--disc-root" && i+1<argc) disc_root=argv[++i];
        else if (arg=="--imports") imports_requested=true;
        else if (arg=="--execute-diagnostic") execute_requested=true;
        else if (arg=="--bootstrap") bootstrap_requested=true;
        else if (arg.rfind("--",0)==0) {
            std::cerr << "unknown diagnostic option: " << arg << '\n';
            return 2;
        } else {
            paths.push_back(arg);
        }
    }
    if(paths.size()!=1 && paths.size()!=2) {
        std::cerr << "usage: hulk_xbe_local_diagnostic <original.xbe> [trusted-recompiled-arm64.so] [--imports] [--bootstrap]\n";
        return 2;
    }
    if(!disc_root.empty() && !execute_requested) { std::cerr << "--disc-root requires execution diagnostic\n"; return 2; }
    if(execute_requested && (paths.size()!=2 || bootstrap_requested)) {
        std::cerr << "Execution requires a module and cannot be combined with --bootstrap\n";
        return 2;
    }
#ifndef HULK_ENABLE_ENTRY_DIAGNOSTIC
    if(execute_requested) {
        std::cerr << "Rebuild with HULK_ENABLE_ENTRY_DIAGNOSTIC and SDK headers\n";
        return 2;
    }
#endif
    try {
        std::ifstream file(paths[0],std::ios::binary|std::ios::ate);
        if(!file) throw std::runtime_error("cannot open local XBE");
        const auto length=file.tellg();
        if(length < 0 || static_cast<std::uint64_t>(length)>512u*1024u*1024u)
            throw std::runtime_error("XBE file exceeds local validation size cap");
        const auto bytes=static_cast<std::size_t>(length);
        file.seekg(0);
        std::vector<std::uint8_t> data(bytes);
        if(bytes && !file.read(reinterpret_cast<char*>(data.data()),
                               static_cast<std::streamsize>(bytes)))
            throw std::runtime_error("cannot read complete XBE");
        hulkpocket::XboxGuestAddressSpace guest;
        const auto loaded=hulkpocket::LoadXbeSections(data,guest);
        std::cout << "XBE parsed and mapped: title=0x"
                  << std::uppercase << std::hex << loaded.title_id
                  << ", entry=0x" << loaded.entry
                  << ", kernel_thunk=0x" << loaded.kernel_thunk
                  << std::dec << ", sections=" << loaded.sections << '\n';
        std::cout << "Startup requirements (uninitialized): tls=0x"
                  << std::uppercase << std::hex << loaded.tls_va
                  << ", stack_commit=0x" << loaded.stack_commit
                  << ", heap_reserve=0x" << loaded.heap_reserve
                  << ", heap_commit=0x" << loaded.heap_commit
                  << ", init_flags=0x" << loaded.init_flags
                  << std::dec << '\n';
        if(imports_requested) {
            const auto imports=hulkpocket::ScanXbeKernelImports(loaded,guest);
            if(!imports.complete) {
                std::cerr << "Kernel import inventory unavailable: " << imports.error << '\n';
                return 4;
            }
            std::cout << "Kernel imports: " << imports.entries
                      << " entries, " << imports.ordinals.size()
                      << " unique kernel ordinals\n";
            std::cout << "Ordinals:";
            for(auto ordinal:imports.ordinals) std::cout << ' ' << ordinal;
            std::cout << '\n';
        }
        if(bootstrap_requested) {
            // Diagnostic-only arena, not a production Xbox allocator.
            const auto state=hulkpocket::PrepareXbeBootstrap(loaded,guest,0x04000000,0x06000000);
            std::cout << "Bootstrap memory prepared: entry=0x" << std::hex
                      << state.entry << ", esp=0x" << state.initial_esp
                      << ", tls_data=0x" << state.tls_data << std::dec
                      << ", tls_bytes=" << state.tls_size
                      << ", tls_callbacks_present=" << (state.tls.callbacks!=0) << '\n';
            std::cout << "CPU dispatch, TLS index and callbacks remain uninitialized.\n";
        }
        if(paths.size() == 2) {
            hulkpocket::PortableXbeImage native(paths[1]);
            const auto result=hulkpocket::CheckHulkXbePreflight(loaded,native);
            std::cout << "Module match: " << (result.ReadyForGuestCall() ? "PASS" : "REJECT")
                      << ", candidate_region=" << result.region_candidate
                      << ", mapped_functions=" << native.MappingCount() << '\n';
            if(!result.ReadyForGuestCall()) {
                std::cerr << "Reason: " << result.failure << '\n';
                return 3;
            }
#ifdef HULK_ENABLE_ENTRY_DIAGNOSTIC
            if(execute_requested) {
                using namespace hulkpocket;
                InitializeXboxKernelPhysicalRegion(guest);
                std::cout << "Xbox kernel physical RAM: 0x80000000-0x800FFFFF (zero-initialized, no firmware image)" << '\n';
                HulkKernelDataRegistry registry;
                auto imports=ScanXbeKernelImports(loaded,guest);
                if(!imports.complete) throw std::runtime_error(imports.error);
                guest.Commit(0x07200000,guest.PageSize());
                HulkKernelClock clock(guest,registry,0x07200000,10000);
                RegisterLaunchData(guest,registry,0x07200100,0,"\\Device\\CdRom0\\default.xbe");
                RegisterKernelProfile(guest,registry,0x07200200,kRetailDiagnosticProfile);
                std::cout << "Xbox retail diagnostic profile: kernel 1.0.5838.1, hardware flags 0x20" << '\n';
                RegisterDispatcherObjectTypes(guest,registry,0x07200400);
                RegisterDiagnosticIdeQueue(guest,registry,0x07200600);
                std::cout << "Diagnostic dispatcher object types and empty IDE queue registered" << '\n';
                auto guards=PlanMissingKernelDataGuards(registry,imports,guest,0x0F000000,0x10000000);
                for(auto g:guards) std::cout << "Unsupported data ordinal " << g.ordinal
                    << " guard=0x" << std::hex << g.address << std::dec << '\n';
                auto boot=PrepareXbeBootstrap(loaded,guest,0x04000000,0x06000000);
                InitializeDiagnosticTlsIndex(boot,guest);
                guest.Commit(0x07000000,guest.PageSize());
                auto cpu=PrepareInitialCpu(boot,guest,0x07000000,0x10,0);
                InitializeKernelThread(guest,0x07000000,0x07000400,
                    boot.memory.stack_top,boot.memory.stack_base,cpu.esp,boot.tls_data,0x10);
                // Explicit diagnostic binding: unsupported data stays inaccessible.
                for(auto ordinal:imports.ordinals)
                    if(HulkKernelExportKind(ordinal)==KernelExportKind::Unknown)
                        throw std::runtime_error("Unknown kernel ordinal "+std::to_string(ordinal));
                for(size_t i=0;i<imports.entries;++i) {
                    auto slot=loaded.kernel_thunk+static_cast<uint32_t>(i*4);
                    auto ordinal=guest.Read32(slot)&0xffffu;
                    uint32_t target=0;
                    if(HulkKernelExportKind(ordinal)==KernelExportKind::Function)
                        target=0xFE000000u+ordinal*4u;
                    else {
                        target=registry.Resolve(ordinal).guest_data;
                        if(!target) for(auto g:guards) if(g.ordinal==ordinal) target=g.address;
                    }
                    if(!target) throw std::runtime_error("Missing diagnostic binding");
                    if(HulkKernelExportKind(ordinal)==KernelExportKind::Data)
                        std::cout << "Data binding ordinal=" << ordinal << " slot=0x" << std::hex << slot
                                  << " target=0x" << target << std::dec << '\n';
                    guest.Write32(slot,target);
                }
                auto entry=native.ResolveKernelEntry();
                std::cout << "Starting isolated guest entry diagnostic" << std::endl;
                std::cerr.flush();
                auto pid=fork();
                if(pid<0) throw std::runtime_error("fork failed");
                if(pid==0) {
                    alarm(15);
                    try {
                        Dl_info module_info{};
                        dladdr(reinterpret_cast<void*>(entry),&module_info);
                        InstallGuestFaultHandler(guest.Base(),reinterpret_cast<uintptr_t>(module_info.dli_fbase));
                        XbeKernelSession session(guest,0x09000000,0x01000000,boot.tls,&clock,disc_root,&registry);
                        int status=session.Run(entry,cpu,loaded.entry);
                        std::cerr << "Guest entry status: " << status << std::endl;
                        _exit(status==0?0:5);
                    } catch(const std::exception& ex) {
                        std::cerr << ex.what() << std::endl;
                        _exit(5);
                    }
                }
                int status=0;
                if(waitpid(pid,&status,0)!=pid) throw std::runtime_error("waitpid failed");
                if(WIFSIGNALED(status)) {
                    std::cerr << "Guest diagnostic stopped by signal " << WTERMSIG(status) << '\n';
                    return 6;
                }
                return WIFEXITED(status)?WEXITSTATUS(status):6;
            }
#endif
        } else {
            std::cout << "No ELF supplied: native function mapping NOT checked.\n";
        }
        std::cout << "Preflight only: no Xbox instructions, graphics or audio executed.\n";
        return 0;
    } catch(const std::exception& ex) {
        std::cerr << "XBE local diagnostic failed: " << ex.what() << '\n';
        return 1;
    }
}
