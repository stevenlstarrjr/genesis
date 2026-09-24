#pragma once
#include "ui/Toolkit.h"
#include <SDL3/SDL.h>

namespace genesis::ui {
// Optional platform adapter. Not linked into the renderer-neutral UI library.
class SdlUiInput {
public:
    SdlUiInput() = default;
    ~SdlUiInput();
    SdlUiInput(const SdlUiInput&) = delete;
    SdlUiInput& operator=(const SdlUiInput&) = delete;
    bool event(Document& document, SDL_Window* window, const SDL_Event& event);
    void sync(Document& document, SDL_Window* window);
private:
    SDL_Cursor *m_horizontalCursor{}, *m_verticalCursor{}, *m_crosshairCursor{};
    bool m_cursorsInitialized=false;
};
}
