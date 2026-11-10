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

## Unknowns

- **`driver/camera_car  alt ctrl shift c a r z`** (line 116): plain letters
  where modifiers go. The reader warns, ignores `c a r`, and binds
  `ctrl+alt+shift+Z`. What the game does with this line is unknown.
- Which group wins when the same key is bound in two active groups.
- Whether the game writes this file back (key rebinding in the options screen).
  The port must never write into Dad's install; any rebinding will be saved in
  `~/.local/share/trainz-native/`.
- The mapping to SDL scancodes isn't written yet.
