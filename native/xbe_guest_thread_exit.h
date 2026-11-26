#pragma once
#include <cstdint>
namespace hulkpocket {
// Shared by the diagnostic executable and the generated-code DSO.
struct GuestThreadExit { uint32_t code; };
}
