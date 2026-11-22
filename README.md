# Lua2OMP

Embedded Lua 5.1.5 runtime, resource manager and protected bytecode compiler for open.mp.

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

Each folder directly under lua/resources is an independent resource with its own Lua state. A resource restart closes its state, runs OnResourceStop, creates a fresh state and executes its manifest scripts again.

## Manifest

<meta>
    <info name="Welcome" author="DeathAmir" version="1.0.0" />
    <script src="config.lua" type="server" />
    <script src="main.lal" type="server" />
</meta>

Supported script extensions are .lua, .luac and encrypted .lal. If meta.xml is absent, scripts are discovered recursively and loaded alphabetically. require() searches the resource directory using ?.lua and ?/init.lua.

## Server console

refresh
resources
start welcome
stop welcome
restart welcome
lua_run example.lua
lua_reload

Resource errors report the resource, script, loading stage and Lua error. A script syntax/runtime error is isolated to its resource.

## Lua API

SendClientMessage, SetPlayerPos, GetPlayerName, GetPlayerDrunkLevel, SetPlayerDrunkLevel, ShowPlayerDialog, TextDrawCreate, TextDrawSetString, TextDrawLetterSize, TextDrawTextSize, TextDrawColor, TextDrawUseBox, TextDrawBoxColor, TextDrawAlignment, TextDrawSetShadow, TextDrawSetOutline, TextDrawSetSelectable, TextDrawShowForPlayer, TextDrawHideForPlayer, TextDrawDestroy, CreateObject, DestroyObject, SetObjectPos, SetObjectRot, CreateVehicle, DestroyVehicle, SetVehiclePos, SetVehicleHealth, addCommandHandler, setTimer and killTimer.

This is an initial API subset, not a complete Pawn/open.mp or MTA API port.

## Compile and encrypt

luac is the standard Lua 5.1 bytecode compiler. Use luaenc to compile and encrypt in one step:

luaenc input.lua output.lal

The output begins with the four-byte L@L v1 marker. The payload is Lua 5.1 bytecode encrypted using AES-256-GCM with a fresh random 96-bit nonce and a 128-bit authentication tag. The resource loader authenticates and decrypts in memory before passing bytecode to Lua; plaintext is not written to disk.

Use the encrypted extension in meta.xml:

<script src="main.lal" type="server" />

## Security

The shared master key is embedded in both compiler and component binaries. A determined person with the binaries can extract it. This protects against casual inspection and detects tampering, but it is not equivalent to per-server secret keys or a secure execution boundary. Install only trusted resources.

## Build

Requirements: CMake 3.20+, Git and a C++17 toolchain.

Windows 32-bit:
cmake -S . -B build -A Win32
cmake --build build --config Release

Windows x64:
cmake -S . -B build -A x64
cmake --build build --config Release

Outputs: Lua2OMP.dll, luac.exe and luaenc.exe.
