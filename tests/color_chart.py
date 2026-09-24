"""Render with --debug-view color-chart to test the real GPU output transform."""
import genesis as gx

gx.window.configure(title="Color transform regression", width=512, height=256)
gx.color.agx(look="neutral", exposure=0, auto_exposure=False)
