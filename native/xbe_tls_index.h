#pragma once
#include "xbe_bootstrap.h"
#include <stdexcept>
namespace hulkpocket {
// Single-module diagnostic TLS only. Does not construct KPCR or run callbacks.
inline void InitializeDiagnosticTlsIndex(const XbeBootstrapState& boot,
                                         XboxGuestAddressSpace& mem) {
    if(boot.tls.callbacks)
        throw std::runtime_error("TLS callbacks require guest execution support");
    if(!boot.tls_size) {
        if(boot.tls.index) throw std::runtime_error("TLS index without storage");
        return;
    }
    if(!boot.tls.index || !mem.IsCommitted(boot.tls.index,4) ||
       !boot.tls_data || !mem.IsCommitted(boot.tls_data,boot.tls_size))
        throw std::runtime_error("Incomplete TLS allocation");
    // Slot zero is reserved for the sole image in this diagnostic runtime.
    mem.Write32(boot.tls.index,0);
}
}
