"""Run using Genesis --validate-script (real PocketPy, no GPU needed)."""
import genesis as gx
import json

assert gx.clouds is gx.sky.clouds
assert gx.configuration()["clouds"]["configured"] is False
assert len(gx.clouds.types()) == 4
for preset in gx.clouds.presets():
    assert gx.clouds.preset(preset) is gx.clouds
gx.clouds.preset("partly_cloudy").wind(speed=12, direction=90).quality("high", update_hz=4).seed(42)
before = gx.configuration()["clouds"]
gx.clouds.layer("low", coverage=0.25)
after = gx.configuration()["clouds"]
assert after["layers"][0]["coverage"] == 0.25
assert after["layers"][0]["density"] == before["layers"][0]["density"]
assert after["wind_speed"] == 12 and after["seed"] == 42 and after["quality"] == "high"
assert gx.clouds.enable(False) is gx.clouds
assert gx.configuration()["clouds"]["enabled"] is False
gx.clouds.enable()

checks = 0
def rejects(error, function):
    global checks
    before = gx.configuration()
    try:
        function()
    except error:
        assert gx.configuration() == before, "invalid input changed native cloud state"
        checks += 1
        return
    raise AssertionError("expected invalid cloud input to fail")

rejects(ValueError, lambda: gx.clouds.preset("tornado"))
rejects(ValueError, lambda: gx.clouds.layer("middle"))
rejects(ValueError, lambda: gx.clouds.layer(kind="unknown"))
rejects(ValueError, lambda: gx.clouds.layer(coverage=1.1))
rejects(TypeError, lambda: gx.clouds.layer(coverage=True))
rejects(ValueError, lambda: gx.clouds.layer(density=-1))
rejects(ValueError, lambda: gx.clouds.layer(altitude=float("nan")))
rejects(ValueError, lambda: gx.clouds.layer(thickness=0))
rejects(ValueError, lambda: gx.clouds.layer(size=1))
rejects(ValueError, lambda: gx.clouds.layer(detail=2))
rejects(TypeError, lambda: gx.clouds.layer(enabled=1))
rejects(ValueError, lambda: gx.clouds.layer("low", altitude=8500, thickness=2000))
rejects(ValueError, lambda: gx.clouds.layer("high", altitude=20000, thickness=6000))
rejects(ValueError, lambda: gx.clouds.wind(speed=151))
rejects(ValueError, lambda: gx.clouds.wind(direction=float("inf")))
rejects(ValueError, lambda: gx.clouds.quality("ultra"))
rejects(ValueError, lambda: gx.clouds.quality(update_hz=0))
rejects(ValueError, lambda: gx.clouds.seed(-1))
rejects(TypeError, lambda: gx.clouds.seed(1.5))

# Exercise the native boundary too, independently of facade validation.
rejects(ValueError, lambda: gx._clouds_configure("not json"))
rejects(ValueError, lambda: gx._clouds_configure("{}"))
invalid = gx.configuration()["clouds"]
invalid["layers"][0]["density"] = -1
rejects(ValueError, lambda: gx._clouds_configure(json.dumps(invalid)))
invalid = gx.configuration()["clouds"]
invalid["layers"] = []
rejects(ValueError, lambda: gx._clouds_configure(json.dumps(invalid)))

gx.clouds.preset("broken")
state = gx.configuration()["clouds"]
assert state["wind_speed"] == 12 and state["quality"] == "high" and state["seed"] == 42
gx.clouds.layer("low", coverage=0, density=0, detail=0)
assert gx.configuration()["clouds"]["layers"][0]["coverage"] == 0
gx.clouds.clear()
assert not gx.configuration()["clouds"]["enabled"]
gx.clouds.layer("high", kind="cirrus", coverage=0.5)
assert gx.configuration()["clouds"]["enabled"]
assert gx.configuration()["clouds"]["layers"][1]["enabled"]
gx.clouds.preset("legacy")
assert not gx.configuration()["clouds"]["configured"]
print("PASS: cloud presets, layers, alias, settings, and " + str(checks) + " invalid configurations")
