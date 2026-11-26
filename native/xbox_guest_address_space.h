#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace hulkpocket {

// XboxXBERecomp code indexes memory as base + guest_32_bit_address.
// This reserves 4 GiB of *virtual* 64-bit address space (not RAM)
// and commits pages on demand. It must run in a 64-bit Android process.
class XboxGuestAddressSpace {
public:
    static constexpr std::uint64_t kSize = 1ULL << 32;
    XboxGuestAddressSpace();
    ~XboxGuestAddressSpace();
    XboxGuestAddressSpace(const XboxGuestAddressSpace&) = delete;
    XboxGuestAddressSpace& operator=(const XboxGuestAddressSpace&) = delete;

    void Decommit(std::uint32_t guest_address, std::size_t bytes);
    void Commit(std::uint32_t guest_address, std::size_t bytes);
    void Read(std::uint32_t guest_address, void* destination, std::size_t bytes) const;
    void Write(std::uint32_t guest_address, const void* source, std::size_t bytes);
    std::uint32_t Read32(std::uint32_t guest_address) const;
    void Write32(std::uint32_t guest_address, std::uint32_t value);
    bool IsCommitted(std::uint32_t guest_address, std::size_t bytes) const;
    std::uint8_t* Base() const noexcept { return base_; }
    std::size_t PageSize() const noexcept { return page_; }
private:
    void Validate(std::uint64_t address, std::uint64_t length) const;
    std::uint8_t* base_ = nullptr;
    std::size_t page_ = 0;
    std::vector<std::uint8_t> committed_;
};
}
