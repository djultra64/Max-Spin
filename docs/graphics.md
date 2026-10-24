# Graphics settings

Open **Graphics Options** from the game's Options menu. Save changes and restart Max Spin when prompted. A running session keeps a consistent graphics configuration while moving between modes.

| Setting | Behavior |
| --- | --- |
| Resolution | Selects a supported 16:9 rendering size. Available choices depend on the active renderer and antialiasing combination. |
| Antialiasing | Offers only combinations accepted by the renderer. The saved setting may reset to the game's antialiasing when changing to a size that cannot use the previous choice. |
| Sprite filter | Original, Super-xBR 2x, or Super-xBR 4x where the prepared sprite resources are available. It applies to selected 2D sprites, not all textures or the whole scene. |
| Window mode | Windowed or desktop fullscreen. Save and restart to apply. |
| Controller prompts | Auto, Xbox, PlayStation, or Nintendo. |
| Frame limit | The displayed frame limit is not currently an adjustable high frame rate gameplay mode. |

The default configuration is 1280 × 720, the game's antialiasing, original 2D filtering, a 60 FPS host cap, and windowed output. A frame cap is an upper bound; it does not guarantee that frame rate or prove correct gameplay timing.

Higher resolutions and antialiasing increase GPU and memory requirements. A settings label does not establish that every original render surface uses that size. Performance and visual compatibility vary by scene, and 90/120 FPS or unlimited gameplay with verified physics are not claimed.

The launcher stores preferences separately from the original game files. On Linux, its default file is `native-linux/cache/pc-preferences/graphics.ini`. Back up or rename that file while Max Spin is closed to return to defaults. Never delete the saves directory when resetting graphics preferences.
