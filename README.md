# Lua2OMP

Embedded Lua 5.1.5 runtime and bytecode compiler for open.mp.

## Resource layout

Server/
  components/Lua2OMP.dll
  lua/
    autorun/
    resources/
      welcome/
        meta.xml
        config.lua
        main.lua
        modules/messages.lua

Every folder directly under lua/resources is an independent resource with its own Lua state. A resource restart closes its state, runs OnResourceStop, creates a fresh state and executes its manifest scripts again. Globals and Lua-side state are therefore reset.

A resource manifest is named meta.xml:

<meta>
    <script src="config.lua" type="server" />
    <script src="modules/messages.lua" type="server" />
    <script src="main.lua" type="server" />
</meta>

Scripts execute in manifest order. If meta.xml is absent, Lua files are discovered recursively and loaded alphabetically. require() is available and searches the resource directory using ?.lua and ?/init.lua.

## Server console

refresh
resources
start welcome
stop welcome
restart welcome
lua_run example.lua
lua_reload

refresh only discovers folders. Start each desired resource explicitly. Resource callbacks OnResourceStart and OnResourceStop are called during lifecycle transitions.

## Lua API currently exposed

SendClientMessage, SetPlayerPos, GetPlayerName, ShowPlayerDialog, TextDrawCreate, TextDrawSetString, TextDrawLetterSize, TextDrawTextSize, TextDrawColor, TextDrawUseBox, TextDrawBoxColor, TextDrawAlignment, TextDrawSetShadow, TextDrawSetOutline, TextDrawSetSelectable, TextDrawShowForPlayer, TextDrawHideForPlayer, TextDrawDestroy.

Current callbacks: OnDialogResponse and OnPlayerClickTextDraw.

The resource lifecycle and modular loading are now implemented. This is not yet a complete Pawn/open.mp API port: player command registration, broad player state, vehicle/object creation, timers, and their full event set are not included in this commit.

## Build

Requirements: CMake 3.20+, Git, and a C++17 toolchain.

Windows 32-bit:
cmake -S . -B build -A Win32
cmake --build build --config Release

Windows x64:
cmake -S . -B build -A x64
cmake --build build --config Release

Outputs: Lua2OMP.dll and luac.exe. Compile Lua bytecode using luac -o output.luac input.lua.

## Security

Resources run inside the server process. Only install trusted scripts. Resource states open Lua base, table, string, math and package libraries; io, os and debug are not opened.
