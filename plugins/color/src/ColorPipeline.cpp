#include <genesis/color/ColorPipeline.h>

namespace genesis::color {
namespace {
Settings g_settings;
}

Settings& settings() { return g_settings; }
void reset() { g_settings = {}; }

} // namespace genesis::color
