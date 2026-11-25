#include <cstdint>
#include <cstdio>
static_assert(sizeof(void*) == 8, "ARM64 required");
#if !defined(__aarch64__) || !defined(__ANDROID__)
#error Android ARM64 required
#endif
extern "C" int hulkpocket_smoke() { return 0; }
int main() { std::puts("HulkPocket Android ARM64 smoke OK"); return hulkpocket_smoke(); }
