# Key bindings: `UserData/settings/keyboard.txt`

Reader: `src/input/KeyBindings.{h,cpp}` (`parseKeyBindings`, `parseComboList`). Tests:
`tests/test_keybindings.cpp`, plus `RealInstall.KeyboardBindingsRead` on the
shipped file (`testdata/ts2010/UserData/settings/keyboard.txt`, a byte-exact copy
from the installer).

**Traced** means read from `trainz.exe` disassembly; **observed** means seen in the
file only; **port's choice** means a decision of ours.

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

Top-level groups: `driver`, `surveyor`, `standard_dialog`, `menu`, `gameplay`, `minimap`.
Each line inside is `action  value`. The shipped file has 6 groups and 383 actions.
Nothing after the action name means no key.

## How a value is read (traced)

Two functions in `trainz.exe`:

- **Splitter, VA `0x4d5bb0`.** It walks the value one character at a time.
  - Each character goes through `tolower` and is added to the current word.
  - `,` (0x2c) ends the word and the combo; the combo is stored and a new one starts.
  - A space (0x20) ends the word only.
  - Nothing else separates words: a tab becomes part of the word.
- **Word parser, VA `0x4d4b60`.** It compares the word with
  `Jet::AnsiString::operator==` against a fixed list, in order:
  - an empty word does nothing;
  - `null` clears the combo (no key, no modifiers);
  - `ctrl` or `control`, `alt`, `shift` set that modifier bit and leave the rest;
  - a key name writes the key's one-byte engine code at byte 2 of the combo,
    replacing any earlier key and keeping the modifiers;
  - **any other word clears the whole combo** (it calls `0x43b8b0(0)`).

What follows from that:
- Word order doesn't matter for modifiers: `b ctrl` is ctrl+B.
- **`driver/camera_car  alt ctrl shift c a r z`** (line 116) is ctrl+alt+shift+**Z**:
  `c`, `a`, `r` are key names too, and each one replaces the one before. The reader
  does the same and warns about each replaced key.
- `ctrl warpdrive b` is plain B; `ctrl b warpdrive` is nothing.
- `page up` is **Up**: `page` is unknown and clears, then `up` binds.

The game also stores combos that ended up with no key. They can't match a key, so
the reader leaves them out and keeps one copy of a repeated combo (`KP9, KPPgUp`
are the same key).

## Key names (traced)

These are exactly the 141 strings the word parser compares, with the code it stores.
Display names elsewhere in the exe (`Page Up`, `KP/`, `KPDel`, `Caps Lock`, `F13`…)
are **not** accepted by the parser, so the reader doesn't accept them either. Also
not accepted: `.`, `,` and `~`; use `period`, `comma` and `tilde`.

| Engine code | Canonical | Names the game accepts |
|---:|---|---|
| 0x01 | Esc | `esc`, `escape` |
| 0x02 | Pause | `pause`, `break` |
| 0x03 | 1 | `1` |
| 0x04 | 2 | `2` |
| 0x05 | 3 | `3` |
| 0x06 | 4 | `4` |
| 0x07 | 5 | `5` |
| 0x08 | 6 | `6` |
| 0x09 | 7 | `7` |
| 0x0a | 8 | `8` |
| 0x0b | 9 | `9` |
| 0x0c | 0 | `0` |
| 0x0d | Minus | `minus`, `underscore`, `-` |
| 0x0e | Equals | `plus`, `equals`, `=` |
| 0x0f | Backspace | `backspace` |
| 0x10 | Tab | `tab` |
| 0x11 | Q | `q` |
| 0x12 | W | `w` |
| 0x13 | E | `e` |
| 0x14 | R | `r` |
| 0x15 | T | `t` |
| 0x16 | Y | `y` |
| 0x17 | U | `u` |
| 0x18 | I | `i` |
| 0x19 | O | `o` |
| 0x1a | P | `p` |
| 0x1b | LeftBracket | `openbrace`, `openbracket`, `leftbracket`, `[` |
| 0x1c | RightBracket | `closedbrace`, `closedbracket`, `rightbracket`, `]` |
| 0x1d | Enter | `enter` |
| 0x1e | LeftCtrl | `leftcontrolkey` |
| 0x1f | A | `a` |
| 0x20 | S | `s` |
| 0x21 | D | `d` |
| 0x22 | F | `f` |
| 0x23 | G | `g` |
| 0x24 | H | `h` |
| 0x25 | J | `j` |
| 0x26 | K | `k` |
| 0x27 | L | `l` |
| 0x28 | Semicolon | `semi`, `semicolon`, `;` |
| 0x29 | Apostrophe | `quote`, `apostrophe`, `'` |
| 0x2a | Grave | `tilde`, `` ` `` |
| 0x2b | LeftShift | `leftshiftkey` |
| 0x2c | Backslash | `backslash`, `\` |
| 0x2d | Z | `z` |
| 0x2e | X | `x` |
| 0x2f | C | `c` |
| 0x30 | V | `v` |
| 0x31 | B | `b` |
| 0x32 | N | `n` |
| 0x33 | M | `m` |
| 0x34 | Comma | `comma` |
| 0x35 | Period | `period`, `fullstop` |
| 0x36 | Slash | `slash`, `/` |
| 0x37 | RightShift | `rightshiftkey` |
| 0x38 | KPStar | `kpstar` |
| 0x3a | Space | `space` |
| 0x3c | F1 | `f1` |
| 0x3d | F2 | `f2` |
| 0x3e | F3 | `f3` |
| 0x3f | F4 | `f4` |
| 0x40 | F5 | `f5` |
| 0x41 | F6 | `f6` |
| 0x42 | F7 | `f7` |
| 0x43 | F8 | `f8` |
| 0x44 | F9 | `f9` |
| 0x45 | F10 | `f10` |
| 0x46 | NumLock | `numlock` |
| 0x47 | ScrollLock | `scrlk`, `scrolllock` |
| 0x48 | KP7 | `kp7`, `kphome` |
| 0x49 | KP8 | `kp8`, `kpup` |
| 0x4a | KP9 | `kp9`, `kppgup`, `kppageup` |
| 0x4b | KPMinus | `kpminus` |
| 0x4c | KP4 | `kp4`, `kpleft` |
| 0x4d | KP5 | `kp5`, `kpclear`, `kpclr` |
| 0x4e | KP6 | `kp6`, `kpright` |
| 0x4f | KPPlus | `kpplus` |
| 0x50 | KP1 | `kp1`, `kpend` |
| 0x51 | KP2 | `kp2`, `kpdown`, `kpdn` |
| 0x52 | KP3 | `kp3`, `kppgdn`, `kppagedown` |
| 0x53 | KP0 | `kp0`, `kpins`, `kpinsert` |
| 0x54 | KPPeriod | `kpperiod`, `kpdot` |
| 0x55 | F11 | `f11` |
| 0x56 | F12 | `f12` |
| 0x5a | KPEnter | `kpenter` |
| 0x5b | KPSlash | `kpslash` |
| 0x5c | RightCtrl | `rightcontrolkey` |
| 0x5e | Home | `home` |
| 0x5f | Up | `up` |
| 0x60 | PageUp | `pgup`, `pageup` |
| 0x61 | Left | `left` |
| 0x62 | Right | `right` |
| 0x63 | End | `end` |
| 0x64 | Down | `down` |
| 0x65 | PageDown | `pgdn`, `pagedown` |
| 0x66 | Insert | `ins`, `insert` |
| 0x67 | Delete | `del`, `delete` |
| 0x6b | PrintScreen | `printscreen`, `prtscn`, `sysrq` |

**The codes follow keyboard position (traced numbers, inferred meaning).** For 80
of the 98 keys the code is exactly the DirectInput `DIK_` scancode + 1 (`DIK_Q`
0x10 → 0x11, `DIK_LSHIFT` 0x2A → 0x2B). The other 18 are the extended keys (arrows,
Home/End, PgUp/PgDn, Ins/Del, keypad Enter and `/`, right Ctrl, SysRq, F11, F12,
Pause). 17 of them are packed into 0x55–0x6b in `DIK_` order; Pause is 0x02, the
one free slot between Esc (0x01) and `1` (0x03). So the engine numbers keys
by position, not by the character they type. **Not found:** the code that converts
a live `DIK_` code to an engine code. No lookup table exists in `InputDirectX_7c.dll`,
`InputWin32_7c.dll`, `InputSystem_7c.dll` or `trainz.exe`, so it's probably done in code.

## From a key press to actions (port's choice)

`src/input/ActionMap` answers "which actions does this press trigger?":

- Modifiers must match exactly (`shift W` does not trigger an action bound to `w`).
  Left and right Shift and Ctrl count the same.
- Pressing a modifier key on its own (`leftcontrolkey` in `camera_mod_control`) is
  matched without its own flag.
- Every action bound to the combo comes back, group by group in the order asked.

The game's own matching code (what happens with extra held modifiers, which group
wins) is not traced.

**Shared keys are normal.** In the shipped file 52 combos are bound to more than
one action in the same group (driver 15, surveyor 27, menu 5, standard_dialog 5).
For example, `W` is `train_cabin_throttle_up`, `train_dcc_up` and
`train_cabin_regulator_up`, and Surveyor tools reuse letters. The action names
**suggest** the game picks one by mode (cab vs DCC, the current Surveyor tool). That
is an inference, not traced: the game might also fire several at once, or use
other rules. It belongs to the parts that use the input (steps 13 and 14).

`input-tool shared KEYBOARD.txt` lists them; `input-tool press KEYBOARD.txt driver "alt 1"`
shows what a press triggers (the key text is read exactly like a keyboard.txt value).

## SDL (src/input/SdlKeys, needs libsdl2-dev)

Each canonical key maps to one SDL **scancode**, a physical key position. That is
the port's choice, supported by the position-ordered engine codes above. Every key
round-trips key → scancode → key with no two keys sharing a scancode
(`tests/test_sdl_keys.cpp`). Keys the game has no name for (Caps Lock, the Alt keys,
the Windows keys, Menu, F13+) map to nothing; Alt still works as a modifier.

`key-tester KEYBOARD.txt [GROUPS]` opens a small window and prints what each
key press triggers. It's built in CI but **has not been run on a screen yet**.

## Unknowns

- The game's dispatch: exact vs loose modifier matching, group priority, and how
  shared keys are chosen.
- The live `DIK_` → engine-code conversion (see above), so behaviour on a non-US
  keyboard isn't proven.
- What the 0x80 starting value of a combo and the other flag bits mean
  (`0x43b900` repacks them); the reader only needs key + shift/ctrl/alt.
- Whether the game writes this file back (key rebinding in the options screen).
  The port must never write into Dad's install; any rebinding will be saved in
  `~/.local/share/trainz-native/`.
- Mouse and joystick bindings: none are in `keyboard.txt`; not looked at yet.
