#pragma once
#include <cstdint>
// Shared visual tokens for the 468 x 466 framebuffer / round 466px viewport.
namespace design {
constexpr uint32_t background=0x141c18,foreground=0xe9eee8,muted=0x8d9e93;
constexpr uint32_t accent=0xc9f27b,line=0x334038,button=0x26352a;
constexpr int center_x=234,center_y=233,button_top=354;
namespace type {
constexpr int hero=90,status=44,ready=60,label=22,body=26;
constexpr int metric=36,result=46,button=34,time=64;
}
namespace motion {
constexpr int frame_us=33000,enter_us=240000,value_us=180000;
constexpr int carousel_us=380000,breath_us=2400000;
}
}
