# Controls

Max Spin maps **actions** to physical controls. Controller names and glyphs follow the active controller family: Xbox, PlayStation, or Nintendo Switch. Keyboard input is supported, although some original prompts still show controller symbols during keyboard use. Automatic controller selection is intended to follow the last active controller, without requiring a separate control configuration.

## Default face buttons

| Action | Position | Xbox | PlayStation | Nintendo Switch |
| --- | --- | --- | --- | --- |
| Confirm / normal shot | Bottom | A | Cross | B |
| Cancel / topspin | Right | B | Circle | A |
| Alternate / slice | Left | X | Square | Y |
| Help / lob | Top | Y | Triangle | X |

Nintendo swaps both A/B and X/Y relative to Xbox. The table names the actual physical button for each layout. A family selection changes the displayed names and glyphs; it does not silently move the action to a different position.

Move with the left stick or directional controls. Pause uses Start, Menu, Options, or Plus, according to the controller. Back uses Back, View, Share, or Minus. Some screens retain original game-specific controls.

Keyboard defaults for the four face-button actions are **Space**, **Escape**, **U**, and **I**, in the same order as the table. Pause is **Return** and Back is **Tab**. Arrow keys provide directional input; **W/A/S/D** map to the left stick and **T/F/G/H** to the right stick.

## Controller family

Use **Controller Prompts** in Graphics Options when automatic detection reports the wrong family. Available families are Auto, Xbox, PlayStation, and Nintendo. The choice is shared during transitions between game modes. Device and controller testing is still ongoing; report mismatches with both the displayed prompt and the action produced by the physical button.

For a launch-time override on Linux:

```sh
TOPSPIN_CONTROLLER_FAMILY=nintendo bash native-linux/play.sh
```

The accepted values are `auto`, `xbox`, `playstation`, and `nintendo`; `switch` is also accepted as an alias.

## Optional action remapping

Advanced users can set `TOPSPIN_BIND_<ACTION>` for controller positions and `TOPSPIN_KEY_<ACTION>` for keyboard bindings before launch. Action names are `CONFIRM`, `CANCEL`, `ALTERNATE`, `HELP`, `NORMAL_SHOT`, `TOPSPIN`, `SLICE`, `LOB`, `PAUSE`, and `BACK`.

For example, swapping menu Confirm and Cancel together avoids assigning two active actions to one button:

```sh
TOPSPIN_BIND_CONFIRM=east TOPSPIN_BIND_CANCEL=south bash native-linux/play.sh
```

Use positions such as `south`, `east`, `west`, and `north`, rather than a controller's printed face-button letters. Prompts are derived from the applied action binding. Duplicate physical bindings within one active context are rejected. These advanced overrides are not a graphical remapping interface.
