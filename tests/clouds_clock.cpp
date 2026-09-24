// Standalone test: compile with plugins/atmosphere/src/Clouds.cpp, no GPU.
#include <genesis/atmosphere/Clouds.h>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

int main() {
    using namespace genesis::clouds;
    reset();
    advance(1);
    assert(windOffsetKm()[0] == 0);
    auto& s = settings();
    s.configured = true;
    s.windSpeed = 10;
    s.windDirection = 0;
    s.updateHz = 2;
    advance(.25);
    advance(.25);
    assert(windOffsetKm()[0] == 0); // invisible layers do not trigger rebakes
    s.layers[0].enabled = true;
    advance(std::numeric_limits<double>::quiet_NaN());
    advance(.24);
    const auto before = windOffsetKm();
    advance(.25);
    assert(windOffsetKm() == before); // no rebake before the half-second tick
    advance(.02);
    assert(std::abs(windOffsetKm()[0] - .005f) < 1e-6f);
    const auto moved = windOffsetKm();
    s.enabled = false;
    advance(.25);
    advance(.25);
    assert(windOffsetKm() == moved);
    s.enabled = true;
    s.windSpeed = 0;
    assert(windOffsetKm()[0] == 0);
    advance(.25);
    s.windSpeed = 10;
    assert(windOffsetKm() == moved); // paused clock did not advance
    reset();
    assert(!settings().configured && windOffsetKm()[0] == 0);
    std::cout << "PASS: cloud clock, static/disabled/invisible freeze, capped updates, reset\n";
}
