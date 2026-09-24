"""The legacy ACES fit must retain its linear-to-display encoding."""
import genesis as gx

gx.window.configure(title="ACES transform regression", width=512, height=256)
gx.color.agx(look="neutral", exposure=0, auto_exposure=False)
gx.color.aces(exposure=0)
