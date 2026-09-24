# Genesis Color

Genesis display color pipeline. AgX is the default view transform, with
neutral, medium-high-contrast, punchy, and golden looks.

```python
genesis.color.agx(look="medium_high", exposure=0.0)
genesis.color.auto_exposure(True)
```

The plugin name is deliberately broader than `agx`: future white balance,
creative 3D LUTs, HDR output, and display transforms belong here too.
