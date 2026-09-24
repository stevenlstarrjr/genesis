#define SDL_MAIN_HANDLED
#include "backends/raster/BgfxRasterBackend.h"
#include "editor/UiWorkbench.h"
#include <string_view>

int main(int argc, char** argv) {
    if (argc > 1 && std::string_view(argv[1]) == "--ui-workbench") return genesis::editor::runUiWorkbench();
    return genesis::raster::run(argc, argv);
}
