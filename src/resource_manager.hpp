#pragma once
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <regex>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

class LuaResourceManager
{
    struct Resource
    {
        std::string name;
        std::filesystem::path directory;
        lua_State* state = nullptr;
        std::vector<std::filesystem::path> scripts;
    };
    std::filesystem::path root_;
    std::map<std::string, Resource> resources_;
    std::function<void(lua_State*)> binder_;

    static bool validName(const std::string& name)
    {
        return !name.empty() && name != "." && name != ".." &&
            std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '-' || c == '.'; });
    }

    static bool inside(const std::filesystem::path& path, const std::filesystem::path& base)
    {
        const auto relative = path.lexically_relative(base);
        return !relative.empty() && relative != "." && relative.generic_string().rfind("..", 0) != 0;
    }

    std::vector<std::filesystem::path> findScripts(const std::filesystem::path& directory)
    {
        std::vector<std::filesystem::path> result;
        const auto manifest = directory / "meta.xml";
        if (std::filesystem::exists(manifest))
        {
            std::ifstream file(manifest);
            const std::string xml((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            const std::regex tag(R"(<script\b([^>]*)/?>)", std::regex::icase);
            const std::regex src(R"(src\s*=\s*["']([^"']+)["'])", std::regex::icase);
            const std::regex type(R"(type\s*=\s*["']([^"']+)["'])", std::regex::icase);
            for (std::sregex_iterator it(xml.begin(), xml.end(), tag), end; it != end; ++it)
            {
                const auto attributes = (*it)[1].str();
                std::smatch source, scriptType;
                if (!std::regex_search(attributes, source, src)) continue;
                if (std::regex_search(attributes, scriptType, type) && scriptType[1].str() != "server") continue;
                const auto candidate = std::filesystem::weakly_canonical(directory / source[1].str());
                if (!inside(candidate, std::filesystem::weakly_canonical(directory)) || !std::filesystem::is_regular_file(candidate)) continue;
                if (candidate.extension() == ".lua" || candidate.extension() == ".luac") result.push_back(candidate);
            }
        }
        else
        {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(directory))
                if (entry.is_regular_file() && (entry.path().extension() == ".lua" || entry.path().extension() == ".luac"))
                    result.push_back(std::filesystem::weakly_canonical(entry.path()));
            std::sort(result.begin(), result.end());
        }
        return result;
    }

    bool load(Resource& resource)
    {
        resource.scripts = findScripts(resource.directory);
        if (resource.scripts.empty()) return false;
        resource.state = luaL_newstate();
        if (!resource.state) return false;
        if (binder_) binder_(resource.state);
        const std::string modulePath = (resource.directory / "?.lua").string() + ";" + (resource.directory / "?/init.lua").string();
        lua_getglobal(resource.state, "package");
        lua_pushlstring(resource.state, modulePath.c_str(), modulePath.size());
        lua_setfield(resource.state, -2, "path");
        lua_pop(resource.state, 1);
        for (const auto& script : resource.scripts)
        {
            if (luaL_loadfile(resource.state, script.string().c_str()) != 0 || lua_pcall(resource.state, 0, 0, 0) != 0)
            {
                lua_close(resource.state);
                resource.state = nullptr;
                return false;
            }
        }
        lua_getglobal(resource.state, "OnResourceStart");
        if (lua_isfunction(resource.state, -1))
        {
            if (lua_pcall(resource.state, 0, 0, 0) != 0) lua_pop(resource.state, 1);
        }
        else lua_pop(resource.state, 1);
        return true;
    }

public:
    void setRoot(std::filesystem::path root) { root_ = std::move(root); std::filesystem::create_directories(root_); }
    void setBinder(std::function<void(lua_State*)> binder) { binder_ = std::move(binder); }

    bool start(const std::string& name)
    {
        if (!validName(name)) return false;
        auto found = resources_.find(name);
        if (found != resources_.end() && found->second.state) return true;
        const auto base = std::filesystem::weakly_canonical(root_);
        const auto directory = std::filesystem::weakly_canonical(root_ / name);
        if (!inside(directory, base) || !std::filesystem::is_directory(directory)) return false;
        Resource resource;
        resource.name = name;
        resource.directory = directory;
        if (!load(resource)) return false;
        resources_[name] = std::move(resource);
        return true;
    }

    bool stop(const std::string& name)
    {
        auto found = resources_.find(name);
        if (found == resources_.end() || !found->second.state) return false;
        lua_getglobal(found->second.state, "OnResourceStop");
        if (lua_isfunction(found->second.state, -1))
        {
            if (lua_pcall(found->second.state, 0, 0, 0) != 0) lua_pop(found->second.state, 1);
        }
        else lua_pop(found->second.state, 1);
        lua_close(found->second.state);
        found->second.state = nullptr;
        return true;
    }

    bool restart(const std::string& name) { stop(name); return start(name); }

    void stopAll()
    {
        for (auto& item : resources_)
            if (item.second.state) stop(item.first);
    }

    void restartAll()
    {
        for (auto& item : resources_)
        {
            if (item.second.state) stop(item.first);
            load(item.second);
        }
    }

    size_t refresh()
    {
        std::filesystem::create_directories(root_);
        for (const auto& entry : std::filesystem::directory_iterator(root_))
        {
            if (!entry.is_directory()) continue;
            const auto name = entry.path().filename().string();
            if (validName(name) && resources_.find(name) == resources_.end())
                resources_.emplace(name, Resource{name, std::filesystem::weakly_canonical(entry.path()), nullptr, {}});
        }
        for (auto it = resources_.begin(); it != resources_.end();)
        {
            if (!std::filesystem::is_directory(root_ / it->first))
            {
                if (it->second.state) stop(it->first);
                it = resources_.erase(it);
            }
            else ++it;
        }
        return resources_.size();
    }

    void printList(const std::function<void(const std::string&)>& output) const
    {
        if (resources_.empty()) output("[Lua2OMP] No resources found.");
        for (const auto& item : resources_)
            output(item.first + (item.second.state ? " [running]" : " [stopped]"));
    }

    template<class PushArgs>
    void call(const char* callback, PushArgs pushArgs)
    {
        for (auto& item : resources_)
        {
            lua_State* state = item.second.state;
            if (!state) continue;
            lua_getglobal(state, callback);
            if (!lua_isfunction(state, -1)) { lua_pop(state, 1); continue; }
            const int count = pushArgs(state);
            if (lua_pcall(state, count, 0, 0) != 0) lua_pop(state, 1);
        }
    }
};
