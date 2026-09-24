# Viewport header controls to place

The old Scene viewport header had these controls. The new header follows the compact Blender-style layout. Keep this list when designing the final homes for controls that do not belong in that header.

| Previous control | Current access | Placement to decide |
| --- | --- | --- |
| Scene label | Scene dock tab | Viewport title or context display |
| Frame Selected | View menu and F shortcut | View/navigation menu |
| Grid | Overlay icon, overlay dropdown, and View menu | Fold into a larger viewport overlays popover |
| Snap | Magnet icon and dropdown with Fine, Standard, and Coarse increments; Ctrl also enables snapping while transforming | Add custom increments and snap targets |
| Animate | Overlay dropdown | Timeline or viewport animation options |
| Low / Med / High renderer quality | Shading dropdown | Renderer or viewport performance settings |
| Pivot choices beyond Median Point | Shown disabled in the pivot dropdown | Implement cursor and individual origins transform behavior |
| Editing modes beyond Object Mode | Object Mode dropdown currently has one choice | Add modes as editing tools are implemented |

Do not restore these as text controls in the compact viewport header. Add their final UI placements as those areas are built.
