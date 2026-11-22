# Lua2OMP

Lua 5.1 runtime component for open.mp.

## Features

- Embedded Lua 5.1.5 runtime inside the open.mp server process.
- Standalone Lua 5.1 bytecode compiler (`luac`).
- Script directory: `Server/lua/`.
- Automatic loading of scripts in `Server/lua/autorun/`.
- Console commands: `lua_run <file.lua|file.luac>` and `lua_reload`.
- Initial server bindings for player chat, player position/name, dialogs and global TextDraw UI.
- Lua callbacks: `OnDialogResponse` and `OnPlayerClickTextDraw`.

## Build

Requirements: CMake 3.20+, Git, and a C++17 toolchain.

Windows 32-bit open.mp:
```powershell
cmake -S . -B build -A Win32
cmake --build build --config Release
```

Windows x64:
```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

The build downloads the pinned open.mp SDK and official Lua 5.1.5 source archive.

Outputs:
- `Lua2OMP.dll`: copy to `Server/components/`.
- `luac.exe`: Lua 5.1 bytecode compiler. Compile scripts with `luac -o output.luac input.lua`.

## Scripts

Put scripts in `Server/lua/`. Files in `Server/lua/autorun/` load when the component starts.

From the server console:
```
lua_run example.lua
lua_run autorun/example.lua
lua_reload
```

Lua files can use these initial bindings:

```lua
print("Hello from Lua 5.1")

SendClientMessage(0, 0xFFFFFFFF, "Hello player")
SetPlayerPos(0, 1958.0, 1343.0, 15.0)
local name = GetPlayerName(0)

local dialog = ShowPlayerDialog(0, 1000, DIALOG_STYLE_MSGBOX,
    "Lua2OMP", "Lua UI is running", "OK", "Close")

local td = TextDrawCreate(320.0, 100.0, "Lua2OMP")
TextDrawLetterSize(td, 0.5, 1.2)
TextDrawColor(td, 0xFFFFFFFF)
TextDrawSetOutline(td, 1)
TextDrawSetSelectable(td, true)
TextDrawShowForPlayer(td, 0)
```

The first version intentionally exposes a limited server-side API. It does not yet mirror every SA-MP/open.mp Pawn native. More player, vehicle, object, player-textdraw, timer and event bindings can be added incrementally.

## Security

Scripts run inside the server process with the permissions of the server account. Only install trusted scripts. The embedded runtime opens Lua base, table, string, math and coroutine libraries; it does not open the Lua `io`, `os`, `package` or `debug` libraries.
