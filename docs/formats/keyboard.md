# Key bindings: `UserData/settings/keyboard.txt`

Reader: `src/input/KeyBindings.{h,cpp}` (`parseKeyBindings`). Tests:
`tests/test_keybindings.cpp`, plus `RealInstall.KeyboardBindingsRead` on the
shipped file (`testdata/ts2010/UserData/settings/keyboard.txt`, a byte-exact copy
from the installer).

What is **traced** and what is only **observed** is marked below.

## Layout (observed)

The file uses the `config.txt` syntax ([config.md](config.md)), so it is read with
`parseConfigText`. It has CR LF line ends and starts with a blank line.

```
driver
{
  train_cabin_brake_release       q, KP9, KPPgUp
  train_cabin_aws_isolate         alt 1
  train_cabin_brake_1             null
  cmd_get_off_train
}
```

- Top-level groups: `driver`, `surveyor`, `standard_dialog`, `menu`, `gameplay`, `minimap`
  (one context each, it seems).
- Each line inside is `action  combo, combo, ...`.
- A combo is zero or more modifiers (`shift`, `ctrl`/`control`, `alt`) and then one key name.
- `null`, or nothing after the action name, means the action has no key.
- Case doesn't matter (`F1` and `f2` both appear).

The shipped file has 6 groups and 383 actions, and every key name in it is in the
table below.

## Key names (traced to the table, not to the parser code)

`trainz.exe` holds a key-name table near file offset `0x543b10`. It has display
names (`KPDel`, `KPIns`, `Page Up`, `F1`–`F15`, `Esc`, `None`, …) and typing
aliases (`pgup`/`pageup`, `kpdn`/`kpdown`, `fullstop`/`period`, `semi`/`semicolon`,
`openbrace`/`openbracket`/`leftbracket`, `leftshiftkey`, …). The reader uses them
all and maps each to one canonical name, for example:

| Canonical | Accepted names |
|---|---|
| `A`…`Z`, `0`…`9` | the letter or digit |
| `F1`…`F15` | `f1`… |
| `PageUp` | `page up`, `pgup`, `pageup` |
| `KP9` | `kp9`, `kppgup`, `kppageup` |
| `KP2` | `kp2`, `kpdown`, `kpdn` |
| `Period` | `.`, `period`, `fullstop` |
| `Equals` | `=`, `equals`, `plus` |
| `Minus` | `-`, `minus`, `underscore` |
| `Grave` | `` ` ``, `~`, `tilde` |

The full list is in `keyTable()` in `KeyBindings.cpp`. Pairing an alias with a key
(for example, `plus` → the `=` key, `kppgup` → keypad 9) follows the table's grouping
and the US layout. The lookup code that uses the table was **not traced**:
Ghidra 12.1 is needed.

Numpad aliases name the same physical key (`KP9, KPPgUp`), so the reader keeps
one copy of a repeated combo.

## From a key press to actions (the port's own rules)

`src/input/ActionMap` answers "which actions does this press trigger?":

- Modifiers must match exactly (`shift W` does not trigger an action bound to `w`).
  Left and right Shift/Ctrl/Alt count the same.
- Pressing a modifier key on its own (`leftcontrolkey` in `camera_mod_control`) is
  matched without its own flag.
- Every action bound to the combo comes back, group by group in the order asked.

**Shared keys are normal.** In the shipped file 52 combos are bound to more than
one action in the same group (driver 15, surveyor 27, menu 5, standard_dialog 5).
For example, `W` is `train_cabin_throttle_up`, `train_dcc_up` and
`train_cabin_regulator_up`, and Surveyor tools reuse letters. The game must pick
by mode (cab vs DCC, the current Surveyor tool). That choice is not traced; it
belongs to the parts that use the input (steps 13 and 14).

`input-tool shared KEYBOARD.txt` lists them; `input-tool press KEYBOARD.txt driver "alt 1"`
shows what a press triggers.

## SDL (src/input/SdlKeys, needs libsdl2-dev)

Each canonical name maps to one SDL **scancode**, a physical key position. The game
imports `DINPUT8.dll`, whose `DIK_` codes are positional too, so a binding stays on
the same key on any keyboard layout. Which `DIK_` code each game name stands for
is assumed from the names on a US layout (the game's name-to-code table is not
traced). Every name round-trips name → scancode → name with no two names sharing
a scancode (`tests/test_sdl_keys.cpp`).

`key-tester KEYBOARD.txt [GROUPS]` opens a small window and prints what each
key press triggers. It's built in CI but **has not been run on a screen yet**.

## Unknowns

- **`driver/camera_car  alt ctrl shift c a r z`** (line 116): plain letters
  where modifiers go. The reader binds `ctrl+alt+shift+Z`, ignores `c a r`, and
  warns about each one. That is the port's own **unconfirmed guess**: it
  assumes the last word is the key, as on every other line. The game might
  instead read a key sequence, reject the line, or do something else; its
  parser has not been traced.
- Which group wins when the same key is bound in two active groups.
- Whether the game writes this file back (key rebinding in the options screen).
  The port must never write into Dad's install; any rebinding will be saved in
  `~/.local/share/trainz-native/`.
- Whether the game ignores extra held modifiers (the port requires an exact match).
- Mouse and joystick bindings: none are in `keyboard.txt`; not looked at yet.
