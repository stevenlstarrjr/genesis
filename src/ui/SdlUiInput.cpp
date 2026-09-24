#include "ui/SdlUiInput.h"
#include <optional>

namespace genesis::ui {
namespace {
std::optional<Key> key(SDL_Keycode key) {
    switch (key) {
    case SDLK_TAB: return Key::Tab;
    case SDLK_RETURN: case SDLK_KP_ENTER: return Key::Enter;
    case SDLK_SPACE: return Key::Space;
    case SDLK_ESCAPE: return Key::Escape;
    case SDLK_LEFT: return Key::Left;
    case SDLK_RIGHT: return Key::Right;
    case SDLK_UP: return Key::Up;
    case SDLK_DOWN: return Key::Down;
    case SDLK_HOME: return Key::Home;
    case SDLK_END: return Key::End;
    case SDLK_BACKSPACE: return Key::Backspace;
    case SDLK_DELETE: return Key::Delete;
    default: return std::nullopt;
    }
}
}
SdlUiInput::~SdlUiInput() {
    // SDL_Quit also frees cursors, so tolerate a host shutting SDL down first.
    if(!(SDL_WasInit(SDL_INIT_VIDEO)&SDL_INIT_VIDEO))return;
    const auto* current=SDL_GetCursor();
    if(current && (current==m_horizontalCursor || current==m_verticalCursor || current==m_crosshairCursor))
        SDL_SetCursor(SDL_GetDefaultCursor());
    SDL_DestroyCursor(m_horizontalCursor);SDL_DestroyCursor(m_verticalCursor);
    SDL_DestroyCursor(m_crosshairCursor);
}
void SdlUiInput::sync(Document& doc, SDL_Window* window) {
    if (doc.wantsTextInput() && !SDL_TextInputActive(window)) SDL_StartTextInput(window);
    else if (!doc.wantsTextInput() && SDL_TextInputActive(window)) SDL_StopTextInput(window);
    const auto cursor=doc.cursor();
    if(cursor!=Cursor::Arrow && !m_cursorsInitialized) {
        m_cursorsInitialized=true;
        m_horizontalCursor=SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_EW_RESIZE);
        m_verticalCursor=SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NS_RESIZE);
        m_crosshairCursor=SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
    }
    auto* native=cursor==Cursor::ResizeHorizontal?m_horizontalCursor:
        cursor==Cursor::ResizeVertical?m_verticalCursor:
        cursor==Cursor::Crosshair?m_crosshairCursor:nullptr;
    if(!native)native=SDL_GetDefaultCursor(); // Headless/unsupported drivers can reject system cursors.
    if(native && SDL_GetCursor()!=native)SDL_SetCursor(native);
}
bool SdlUiInput::event(Document& doc, SDL_Window* window, const SDL_Event& event) {
    bool consumed = false;
    if (event.type == SDL_EVENT_MOUSE_MOTION) consumed = doc.pointerMove(event.motion.x,event.motion.y);
    else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
        const bool control = (SDL_GetModState() & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
        consumed = doc.pointerDown(event.button.x,event.button.y,control); if (consumed) SDL_CaptureMouse(true);
    } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT) {
        consumed = doc.pointerUp(event.button.x,event.button.y); SDL_CaptureMouse(false);
    } else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
        float x = 0, y = 0; SDL_GetMouseState(&x,&y);
        const float sign = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? 1.0f : -1.0f;
        consumed = doc.wheel(x,y,event.wheel.y*36*sign);
    } else if (event.type == SDL_EVENT_WINDOW_MOUSE_LEAVE) doc.pointerLeave();
    else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) { doc.cancelInput(); SDL_CaptureMouse(false); }
    else if (event.type == SDL_EVENT_TEXT_INPUT) consumed = doc.textInput(event.text.text);
    else if (event.type == SDL_EVENT_KEY_DOWN) {
        const bool control = (event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
        if (control && doc.wantsTextInput()) {
            if (event.key.key == SDLK_A) consumed = doc.keyDown(Key::SelectAll);
            else if (event.key.key == SDLK_C || event.key.key == SDLK_X) {
                const auto selected = doc.selectedText();
                if (!selected.empty() && SDL_SetClipboardText(selected.c_str()) && event.key.key == SDLK_X) doc.cutSelection();
                consumed = true;
            } else if (event.key.key == SDLK_V) {
                char* clipboard = SDL_GetClipboardText();
                if (clipboard) { doc.textInput(clipboard); SDL_free(clipboard); } consumed = true;
            }
        }
        if (!consumed) if (auto mapped = key(event.key.key))
            consumed = doc.keyDown(*mapped,(event.key.mod & SDL_KMOD_SHIFT) != 0,event.key.repeat);
    } else if (event.type == SDL_EVENT_KEY_UP) {
        if (auto mapped = key(event.key.key)) consumed = doc.keyUp(*mapped);
    }
    sync(doc,window); return consumed;
}
}
