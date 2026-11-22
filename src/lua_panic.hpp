#pragma once
#include <lua.h>
#include <lauxlib.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <utility>

namespace lua2omp_panic {
using Paths = std::pair<std::filesystem::path, std::filesystem::path>;
inline std::mutex mutex;
inline std::map<lua_State*, Paths> states;

inline void appendPath(const std::filesystem::path& path, const std::string& message) noexcept {
    try {
        if (path.empty()) return;
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::app | std::ios::binary);
        if (file) {
            file << message << '\n';
            file.flush();
        }
    } catch (...) {}
}

inline void append(lua_State* state, const std::string& message) noexcept {
    try {
        Paths paths;
        {
            std::lock_guard<std::mutex> lock(mutex);
            auto found = states.find(state);
            if (found == states.end()) return;
            paths = found->second;
        }
        appendPath(paths.first / "error.log", message);
        if (paths.second != paths.first) appendPath(paths.second / "error.log", message);
    } catch (...) {}
}

inline int panic(lua_State* state) noexcept {
    const char* raw = lua_tostring(state, -1);
    append(state, std::string("[Lua PANIC] ") + (raw ? raw : "unknown unprotected Lua API error"));
    return 0;
}

inline void attach(lua_State* state, const std::filesystem::path& resourceDirectory, const std::filesystem::path& rootDirectory) noexcept {
    if (!state) return;
    try {
        std::filesystem::create_directories(resourceDirectory);
        std::filesystem::create_directories(rootDirectory);
        {
            std::lock_guard<std::mutex> lock(mutex);
            states[state] = Paths{resourceDirectory, rootDirectory};
        }
        lua_atpanic(state, panic);
        append(state, "[Lua2OMP] Lua state initialized");
    } catch (...) {}
}

inline void detach(lua_State* state) noexcept {
    try {
        std::lock_guard<std::mutex> lock(mutex);
        states.erase(state);
    } catch (...) {}
}
}
