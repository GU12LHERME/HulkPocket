#include "hulk_verified_fibers.h"
#if HULK_VERIFIED_FIBERS
#include "hulk_live_fibers.h"
extern "C" void __imp__sub_0035550A(X86Context&,uint8_t*);
extern "C" __attribute__((visibility("default")))
void sub_0035551D(X86Context& ctx,uint8_t* base){
    hulkpocket::HulkLiveFibers::SwitchGuest(ctx,base);
}
extern "C" __attribute__((visibility("default")))
void sub_0035550A(X86Context& ctx,uint8_t* base){
    hulkpocket::HulkLiveFibers::DeleteGuest(ctx,base);
    __imp__sub_0035550A(ctx,base);
}
#endif
