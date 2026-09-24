#pragma once
#include "ui/Toolkit.h"

namespace genesis::ui::theme {
// Shared editor tokens. Dimensions are logical pixels; type sizes are points.
inline constexpr Color canvas{32,32,32,255};
inline constexpr Color panel{56,56,56,255};
inline constexpr Color raised{65,65,65,255};
inline constexpr Color input{42,42,42,255};
inline constexpr Color border{30,30,30,255};
inline constexpr Color text{210,210,210,255};
inline constexpr Color muted{165,165,165,255};
inline constexpr Color accent{90,153,213,255};
inline constexpr Color selection{44,93,135,255};
inline constexpr Color hover{79,79,79,255};
inline constexpr Color pressed{47,47,47,255};
inline constexpr Color success{131,201,169,255};
inline constexpr Color warning{230,190,116,255};
inline constexpr Color axes[]{{231,135,145,255},{131,201,169,255},{133,173,255,255}};
inline Style label(float size=10, Color color=text) {
    Style s; s.fontSize=size; s.textColor=color; s.shrink=0; return s;
}
inline Style button(bool primary=false) {
    Style s; s.height=22; s.shrink=0; s.padding=Insets(8,2); s.radius=2;
    s.font=Font::Regular; s.fontSize=9; s.textAlign=.5f; s.textColor=text;
    s.background=primary ? selection : raised; s.hoverBackground=hover;
    s.pressedBackground=pressed; s.focusColor=accent; return s;
}
inline Style field() {
    Style s; s.height=20; s.padding=Insets(5,2); s.radius=2;
    s.fontSize=9; s.textColor=text; s.background=input; s.borderWidth=1;
    s.borderColor=border; s.focusColor=accent; s.textWrap=TextWrap::None;
    return s;
}
}
