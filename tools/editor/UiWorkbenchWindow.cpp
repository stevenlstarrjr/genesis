#include "editor/UiWorkbench.h"
#include "ui/SdlUiInput.h"
#include <algorithm>
#include <cstdio>

namespace genesis::editor {
int runUiWorkbench() {
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    auto* window = SDL_CreateWindow("Genesis - Editor UI Workbench",1280,820,SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) { SDL_Quit(); return 1; }
    SDL_SetWindowMinimumSize(window,1000,680);
    auto* renderer = SDL_CreateRenderer(window,nullptr);
    if (!renderer) { SDL_DestroyWindow(window); SDL_Quit(); return 1; }
    SDL_Texture* texture = nullptr;
    int result = 0;
    try {
        UiWorkbench workbench; ui::SdlUiInput input;
        uint32_t textureWidth = 0, textureHeight = 0; uint64_t revision = 0;
        bool running = true;
        while (running) {
            int w = 1,h = 1,pw = 1,ph = 1;
            SDL_GetWindowSize(window,&w,&h); SDL_GetWindowSizeInPixels(window,&pw,&ph);
            w = std::max(w,1); h = std::max(h,1); pw = std::max(pw,1);
            workbench.document().layout(float(w),float(h));
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) running = false;
                else input.event(workbench.document(),window,event);
            }
            if (!running) break;
            input.sync(workbench.document(),window);
            const auto& surface = workbench.render(float(w),float(h),float(pw)/w);
            if (!texture || textureWidth != surface.width || textureHeight != surface.height) {
                if (texture) SDL_DestroyTexture(texture);
                texture = SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,surface.width,surface.height);
                if (!texture) { result = 1; break; }
                textureWidth = surface.width; textureHeight = surface.height; revision = 0;
            }
            if (revision != surface.revision) {
                if (!SDL_UpdateTexture(texture,nullptr,surface.pixels.data(),surface.width*4)) { result = 1; break; }
                revision = surface.revision;
            }
            SDL_RenderClear(renderer); SDL_RenderTexture(renderer,texture,nullptr,nullptr); SDL_RenderPresent(renderer);
            SDL_Delay(16);
        }
    } catch (const std::exception& error) { std::fprintf(stderr,"UI workbench: %s\n",error.what()); result = 1; }
    if (texture) SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit(); return result;
}
}
