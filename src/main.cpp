#define SDL_MAIN_USE_CALLBACKS 1

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <pocketpy/pocketpy.h>

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>

namespace {

struct App {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    std::uint64_t startTicks = 0;
    bool pythonInitialized = false;
};

std::uint8_t wave(double value) {
    return static_cast<std::uint8_t>(
        255.0 * (0.5 + 0.5 * std::sin(value)));
}

bool runDemoScript() {
    const std::string path = std::string(GENESIS_DEMO_DIR) + "/hello.py";
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        SDL_Log("Could not open Python demo: %s", path.c_str());
        return false;
    }

    const std::string source((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
    if (!py_exec(source.c_str(), path.c_str(), EXEC_MODE, nullptr)) {
        py_printexc();
        return false;
    }
    return true;
}

} // namespace

SDL_AppResult SDL_AppInit(void** appState, int, char**) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    auto* app = new App;
    py_initialize();
    app->pythonInitialized = true;
    if (!runDemoScript()) {
        py_finalize();
        delete app;
        SDL_Quit();
        return SDL_APP_FAILURE;
    }

    if (!SDL_CreateWindowAndRenderer(
            "Genesis - SDL3 shell", 1280, 720,
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY,
            &app->window, &app->renderer)) {
        SDL_Log("Window creation failed: %s", SDL_GetError());
        py_finalize();
        delete app;
        SDL_Quit();
        return SDL_APP_FAILURE;
    }

    app->startTicks = SDL_GetTicks();
    *appState = app;
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void*, SDL_Event* event) {
    if (event->type == SDL_EVENT_QUIT ||
        (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE)) {
        return SDL_APP_SUCCESS;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appState) {
    auto& app = *static_cast<App*>(appState);
    const double seconds =
        static_cast<double>(SDL_GetTicks() - app.startTicks) / 1000.0;

    SDL_SetRenderDrawColor(app.renderer,
                           wave(seconds * 0.45 + 0.2),
                           wave(seconds * 0.31 + 2.2),
                           wave(seconds * 0.23 + 4.0),
                           255);
    SDL_RenderClear(app.renderer);
    SDL_RenderPresent(app.renderer);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appState, SDL_AppResult) {
    auto* app = static_cast<App*>(appState);
    if (app != nullptr) {
        SDL_DestroyRenderer(app->renderer);
        SDL_DestroyWindow(app->window);
        if (app->pythonInitialized) {
            py_finalize();
        }
        delete app;
    }
    SDL_Quit();
}
