#include "ui/ProjectPrompt.h"
#include "ui/LauncherView.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <memory>
#include <mutex>

namespace genesis::ui {
namespace {
struct DialogResult {
    std::mutex mutex;
    bool ready = false;
    std::optional<std::filesystem::path> path;
    std::string error;
};
void SDLCALL dialogFinished(void* userdata, const char* const* paths, int) {
    // May run on another thread: own the result, never touch widgets/window.
    std::unique_ptr<std::shared_ptr<DialogResult>> handle(static_cast<std::shared_ptr<DialogResult>*>(userdata));
    auto& result = **handle;
    std::lock_guard lock(result.mutex);
    if (!paths) result.error = SDL_GetError();
    else if (*paths) result.path = std::filesystem::u8path(*paths);
    result.ready = true;
}
std::optional<Key> uiKey(SDL_Keycode key) {
    if (key == SDLK_TAB) return Key::Tab;
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) return Key::Enter;
    if (key == SDLK_SPACE) return Key::Space;
    return std::nullopt;
}
}

std::optional<std::filesystem::path> showProjectPrompt(const std::string& message) {
    SDL_SetAppMetadata("Genesis", "0.2.0", "com.genesis.renderer");
    if (!SDL_Init(SDL_INIT_VIDEO)) return std::nullopt;
    auto* window = SDL_CreateWindow("Genesis",1000,680,SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) { SDL_Quit(); return std::nullopt; }
    SDL_SetWindowMinimumSize(window,380,640);
    auto* renderer = SDL_CreateRenderer(window,nullptr);
    if (!renderer) { SDL_DestroyWindow(window); SDL_Quit(); return std::nullopt; }
    SDL_Texture* texture = nullptr;
    std::optional<std::filesystem::path> selected;
    std::shared_ptr<DialogResult> dialog;
    bool closeRequested = false;
    try {
        auto open = [&](bool folder) {
            if (dialog) return;
            dialog = std::make_shared<DialogResult>();
            auto* callbackData = new std::shared_ptr<DialogResult>(dialog);
            static constexpr SDL_DialogFileFilter filters[] = {
                {"Genesis projects, scenes and scripts", "project;gscene;py"}, {"All files", "*"}
            };
            if (folder) SDL_ShowOpenFolderDialog(dialogFinished,callbackData,window,nullptr,false);
            else SDL_ShowOpenFileDialog(dialogFinished,callbackData,window,filters,2,nullptr,false);
        };
        LauncherView view(message,[&] { open(true); },[&] { open(false); });
        auto& input = view.document();
        uint64_t revision = 0;
        uint32_t textureWidth = 0, textureHeight = 0;
        while (true) {
            int width = 0, height = 0, pixelWidth = 0, pixelHeight = 0;
            SDL_GetWindowSize(window,&width,&height);
            SDL_GetWindowSizeInPixels(window,&pixelWidth,&pixelHeight);
            // SDL reports window units for input; render separately at pixel density.
            width = std::max(width,1); height = std::max(height,1);
            pixelWidth = std::max(pixelWidth,1); pixelHeight = std::max(pixelHeight,1);
            view.layout(float(width),float(height));
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT ||
                    (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) closeRequested = true;
                else if (event.type == SDL_EVENT_MOUSE_MOTION) input.pointerMove(event.motion.x,event.motion.y);
                else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
                    if (input.pointerDown(event.button.x,event.button.y)) SDL_CaptureMouse(true);
                } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT) {
                    input.pointerUp(event.button.x,event.button.y); SDL_CaptureMouse(false);
                } else if (event.type == SDL_EVENT_WINDOW_MOUSE_LEAVE) input.pointerLeave();
                else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) { input.cancelInput(); SDL_CaptureMouse(false); }
                else if (event.type == SDL_EVENT_KEY_DOWN) {
                    if (auto key = uiKey(event.key.key)) input.keyDown(*key,(event.key.mod & SDL_KMOD_SHIFT) != 0,event.key.repeat);
                } else if (event.type == SDL_EVENT_KEY_UP) {
                    if (auto key = uiKey(event.key.key)) input.keyUp(*key);
                } else if (event.type == SDL_EVENT_DROP_FILE && event.drop.data && !dialog) {
                    selected = std::filesystem::u8path(event.drop.data); closeRequested = true;
                }
            }
            if (dialog) {
                bool ready = false;
                {
                    std::lock_guard lock(dialog->mutex);
                    ready = dialog->ready;
                    if (ready) {
                        if (dialog->path) { selected = dialog->path; closeRequested = true; }
                        else view.setMessage(dialog->error.empty() ? message : "Unable to open dialog: " + dialog->error);
                    }
                }
                if (ready) dialog.reset();
            }
            // Do not tear SDL down while one of its modal dialogs is outstanding.
            if (closeRequested && !dialog) break;
            view.setBusy(bool(dialog));
            const auto& surface = view.render(float(width),float(height),float(pixelWidth)/width);
            if (!texture || textureWidth != surface.width || textureHeight != surface.height) {
                if (texture) SDL_DestroyTexture(texture);
                texture = SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,surface.width,surface.height);
                if (!texture) break;
                SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);
                textureWidth = surface.width; textureHeight = surface.height; revision = 0;
            }
            if (revision != surface.revision) {
                SDL_UpdateTexture(texture,nullptr,surface.pixels.data(),surface.width*sizeof(uint32_t));
                revision = surface.revision;
            }
            SDL_SetRenderDrawColor(renderer,8,11,18,255); SDL_RenderClear(renderer);
            SDL_RenderTexture(renderer,texture,nullptr,nullptr); SDL_RenderPresent(renderer);
            SDL_Delay(16);
        }
    } catch (const std::exception& error) { SDL_Log("Genesis UI: %s",error.what()); }
    if (texture) SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit();
    return selected;
}
}
