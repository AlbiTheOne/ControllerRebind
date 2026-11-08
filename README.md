# Controller Rebind 1.1

Rebind every controller button and both triggers in GTA IV, with separate layouts for vehicles and on foot. Buttons can also press keyboard keys for mods such as Manual Shift.

**[Download and mod page on Nexus Mods](https://www.nexusmods.com/gta4/mods/1753)**

![Controller Rebind menu demonstration](https://staticdelivery.nexusmods.com/mods/253/images/1753/1753-1794178075-976662456.gif)

## What's new in 1.1

- **Performance optimizations:** reduced keyboard polling and render checks while the menu is closed; additional controller polling only when the open menu needs it.
- **Pause-menu integration:** open **Controls > Controller Configuration**. The old Page Up shortcut has been removed.
- **Automatic controller artwork:** the Xbox or PlayStation picture follows the game's **Gamepad Icons** setting.
- **Improved input isolation:** the underlying pause menu does not receive mouse, keyboard or controller input while editing.
- **Consistent remapping:** controller Configuration is kept at Standard.
- **Cleaner interface:** removed the separate controller-style selector and on-screen version label.

## Requirements and compatibility

- GTA IV: The Complete Edition **1.2.0.59** with [FusionFix](https://www.nexusmods.com/gta4/mods/716).
- An **XInput** controller: Xbox, or PlayStation through Steam Input / DS4Windows.
- Tested setup: FusionFix 5.1.2 with Vulkan. TLAD, TBoGT and native D3D9 are untested.
- Older Manual Shift versions that filter controller input may conflict. Use the updated keyboard-based version and map buttons to its keyboard keys.

## Installation and use

1. Close the game and place `ControllerRebind.asi` and `ControllerRebind.ini` in the game's `plugins` folder. Back up an existing INI before replacing it to preserve your bindings.
2. Open the pause menu and choose **Controls > Controller Configuration**.
3. Use mouse and keyboard to choose a button and assign an action, Off, or a keyboard key.
4. **Save and Close** saves changes. **Esc** closes without saving; it closes the selection window first if open.

On Foot and Combat share one layout; In Vehicle uses another. Menu languages: English, German, French, Italian and Spanish. The old `[Menu] Key` and `Style` settings are ignored; `Language` remains supported.

Known issue: when multiple controller buttons are bound to the same keyboard key, releasing one can release that key while another is still held.

## Building

The source in this repository matches the version 1.1 source included in the Nexus upload.

Create a `build` directory, then run with Zig (the published build used Zig 0.17.1):

```sh
zig c++ -target x86-windows-gnu -std=c++20 -O2 -shared -fno-exceptions -o build/ControllerRebind.dll main.cpp -luser32 -lkernel32 -lgdi32
```

Rename `build/ControllerRebind.dll` to `ControllerRebind.asi`. See [BUILD.txt](BUILD.txt) for the graphics-generation scripts and implementation details.

## License

GPL-3.0 - see [LICENSE](LICENSE).

## Credits

- [FusionFix](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix) (ThirteenAG, GPL-3.0): byte patterns and native call method
- IV-SDK (Zolika1351): structure layouts
- stb_image (Sean Barrett, public domain): PNG decoding
