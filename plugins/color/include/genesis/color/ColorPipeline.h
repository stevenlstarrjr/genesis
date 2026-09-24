#pragma once

namespace genesis::color {

enum class ViewTransform {
    Linear,
    Reinhard,
    Hable,
    Aces,
    AgX,
};

enum class Look {
    Neutral,
    MediumHighContrast,
    Punchy,
    Golden,
};

struct Settings {
    ViewTransform viewTransform = ViewTransform::AgX;
    Look look = Look::MediumHighContrast;
    float exposure = 0.0f;
    bool autoExposure = true;
    float autoExposureMinEv = -6.0f;
    float autoExposureMaxEv = 6.0f;
};

Settings& settings();
void reset();

} // namespace genesis::color
