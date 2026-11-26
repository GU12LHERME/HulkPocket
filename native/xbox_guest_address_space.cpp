#include "xbox_guest_address_space.h"
#include <sys/mman.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <cstring>
#include <stdexcept>

namespace hulkpocket {
XboxGuestAddressSpace::XboxGuestAddressSpace() {
    const long p = sysconf(_SC_PAGESIZE);
    if (p <= 0 || (p & (p - 1)) != 0 || kSize % std::uint64_t(p))
        throw std::runtime_error("Xbox guest memory requires power-of-two OS page size");
    page_ = static_cast<std::size_t>(p);
    void* mapping = mmap(nullptr, static_cast<std::size_t>(kSize),
                         PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
                         -1, 0);
    if (mapping == MAP_FAILED)
        throw std::runtime_error("cannot reserve 4 GiB guest virtual address space");
    base_ = static_cast<std::uint8_t*>(mapping);
    try {
        committed_.resize(static_cast<std::size_t>(kSize / page_), 0);
    } catch (...) {
        munmap(base_, static_cast<std::size_t>(kSize));
        base_ = nullptr;
        throw;
    }
}

XboxGuestAddressSpace::~XboxGuestAddressSpace() {
    if (base_) munmap(base_, static_cast<std::size_t>(kSize));
}

void XboxGuestAddressSpace::Validate(
    std::uint64_t address, std::uint64_t length) const {
    if (address > kSize || length > kSize - address)
        throw std::out_of_range("Xbox guest address outside 32-bit range");
    if (length && address < page_)
        throw std::out_of_range("Xbox guest null page must remain inaccessible");
}

bool XboxGuestAddressSpace::IsCommitted(
    std::uint32_t guest_address, std::size_t bytes) const {
    const std::uint64_t addr = guest_address;
    if (bytes > kSize - addr || (bytes && addr < page_))
        return false;
    if (!bytes) return true;
    const std::uint64_t first = addr / page_;
    const std::uint64_t last = (addr + bytes - 1) / page_;
    for (std::uint64_t i = first; i <= last; ++i)
        if (!committed_[static_cast<std::size_t>(i)]) return false;
    return true;
}

void XboxGuestAddressSpace::Commit(
    std::uint32_t guest_address, std::size_t bytes) {
    Validate(guest_address, bytes);
    if (!bytes) return;
    const std::uint64_t first = std::uint64_t(guest_address) / page_;
    const std::uint64_t last =
        (std::uint64_t(guest_address) + bytes - 1) / page_;
    const std::uint64_t offset = first * page_;
    const std::uint64_t length = (last - first + 1) * page_;
    if (mprotect(base_ + static_cast<std::size_t>(offset),
                 static_cast<std::size_t>(length),
                 PROT_READ | PROT_WRITE) != 0)
        throw std::runtime_error("failed to commit Xbox guest pages");
    for (std::uint64_t i = first; i <= last; ++i)
        committed_[static_cast<std::size_t>(i)] = 1;
}

// Map the same anonymous file into two non-overlapping guest windows.
// Both views are normal host RAM: no physical DMA or host cache-mode claim.
void XboxGuestAddressSpace::CommitSharedPair(uint32_t first,uint32_t second,size_t bytes){
    Validate(first,bytes);Validate(second,bytes);
    if(!bytes||first%page_||second%page_||bytes%page_||
       (uint64_t(first)<uint64_t(second)+bytes&&uint64_t(second)<uint64_t(first)+bytes))
        throw std::invalid_argument("Invalid shared guest mapping");
    for(size_t off=0;off<bytes;off+=page_)
        if(IsCommitted(first+uint32_t(off),1)||IsCommitted(second+uint32_t(off),1))
            throw std::runtime_error("Shared guest mapping overlaps committed pages");
#ifdef SYS_memfd_create
    int fd=static_cast<int>(syscall(SYS_memfd_create,"hulk-physical",1u));
#else
    int fd=-1;
#endif
    if(fd<0)throw std::runtime_error("Cannot create shared Xbox RAM backing");
    if(ftruncate(fd,static_cast<off_t>(bytes))!=0){close(fd);throw std::runtime_error("Cannot size shared Xbox RAM backing");}
    void* a=mmap(base_+first,bytes,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_FIXED,fd,0);
    if(a==MAP_FAILED){close(fd);throw std::runtime_error("Cannot map Xbox physical RAM");}
    void* b=mmap(base_+second,bytes,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_FIXED,fd,0);
    close(fd);
    if(b==MAP_FAILED){Decommit(first,bytes);throw std::runtime_error("Cannot map Xbox RAM alias");}
    for(size_t off=0;off<bytes;off+=page_){
        committed_[(first+off)/page_]=1;committed_[(second+off)/page_]=1;
    }
}

void XboxGuestAddressSpace::Decommit(
    std::uint32_t guest_address, std::size_t bytes) {
    Validate(guest_address, bytes);
    if (!bytes) return;
    if (guest_address % page_ || bytes % page_)
        throw std::invalid_argument("Unaligned guest decommit");
    // Replace with fresh inaccessible anonymous pages: recommit must read zero.
    void* ptr = mmap(base_ + guest_address, bytes, PROT_NONE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | MAP_NORESERVE, -1, 0);
    if (ptr == MAP_FAILED)
        throw std::runtime_error("failed to decommit Xbox guest pages");
    const auto first = guest_address / page_;
    for (size_t i=0; i<bytes/page_; ++i) committed_[first+i]=0;
}

void XboxGuestAddressSpace::Read(
    std::uint32_t guest_address, void* destination, std::size_t bytes) const {
    Validate(guest_address, bytes);
    if (!bytes) return;
    if (!destination || !IsCommitted(guest_address, bytes))
        throw std::out_of_range("uncommitted Xbox guest read");
    std::memcpy(destination, base_ + guest_address, bytes);
}

void XboxGuestAddressSpace::Write(
    std::uint32_t guest_address, const void* source, std::size_t bytes) {
    Validate(guest_address, bytes);
    if (!bytes) return;
    if (!source || !IsCommitted(guest_address, bytes))
        throw std::out_of_range("uncommitted Xbox guest write");
    std::memcpy(base_ + guest_address, source, bytes);
}

std::uint32_t XboxGuestAddressSpace::Read32(
    std::uint32_t guest_address) const {
    std::uint32_t value = 0;
    Read(guest_address, &value, sizeof(value));
    return value;
}

void XboxGuestAddressSpace::Write32(
    std::uint32_t guest_address, std::uint32_t value) {
    Write(guest_address, &value, sizeof(value));
}
} // namespace hulkpocket
