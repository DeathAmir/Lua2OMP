#include <sdk.hpp>
#include <Server/Components/Console/console.hpp>
#include <Server/Components/Dialogs/dialogs.hpp>
#include <Server/Components/TextDraws/textdraws.hpp>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>

class Lua2OMPComponent final : public IComponent, public ConsoleEventHandler, public PlayerDialogEventHandler, public TextDrawEventHandler
{
    ICore* core_ = nullptr;
    IConsoleComponent* console_ = nullptr;
    IDialogsComponent* dialogs_ = nullptr;
    ITextDrawsComponent* textdraws_ = nullptr;
    lua_State* state_ = nullptr;
    std::filesystem::path root_;
    std::map<int, ITextDraw*> textDrawPool_;

    static Lua2OMPComponent* self(lua_State* state)
    {
        return static_cast<Lua2OMPComponent*>(lua_touserdata(state, lua_upvalueindex(1)));
    }

    IPlayer* player(int id) const
    {
        return core_ ? core_->getPlayers().get(id) : nullptr;
    }

    static int luaPrint(lua_State* state)
    {
        auto* instance = self(state);
        const int count = lua_gettop(state);
        std::string output;
        for (int i = 1; i <= count; ++i)
        {
            if (i > 1) output += "\t";
            size_t length = 0;
            const char* value = luaL_checklstring(state, i, &length);
            if (value) output.append(value, length);
            lua_pop(state, 1);
        }
        if (instance->core_) instance->core_->printLnU8("[Lua] %s", output.c_str());
        return 0;
    }

    static int sendClientMessage(lua_State* state)
    {
        auto* instance = self(state);
        const int id = static_cast<int>(luaL_checkinteger(state, 1));
        const auto colour = static_cast<uint32_t>(luaL_checknumber(state, 2));
        size_t length = 0;
        const char* message = luaL_checklstring(state, 3, &length);
        if (auto* target = instance->player(id))
            target->sendClientMessage(Colour::FromARGB(colour), StringView(message, length));
        lua_pushboolean(state, instance->player(id) != nullptr);
        return 1;
    }

    static int setPlayerPosition(lua_State* state)
    {
        auto* instance = self(state);
        const int id = static_cast<int>(luaL_checkinteger(state, 1));
        auto* target = instance->player(id);
        if (!target) { lua_pushboolean(state, 0); return 1; }
        target->setPosition(Vector3(
            static_cast<float>(luaL_checknumber(state, 2)),
            static_cast<float>(luaL_checknumber(state, 3)),
            static_cast<float>(luaL_checknumber(state, 4))));
        lua_pushboolean(state, 1);
        return 1;
    }

    static int getPlayerName(lua_State* state)
    {
        auto* instance = self(state);
        auto* target = instance->player(static_cast<int>(luaL_checkinteger(state, 1)));
        if (!target) { lua_pushnil(state); return 1; }
        const auto name = target->getName();
        lua_pushlstring(state, name.data(), name.size());
        return 1;
    }

    static int showPlayerDialog(lua_State* state)
    {
        auto* instance = self(state);
        const int id = static_cast<int>(luaL_checkinteger(state, 1));
        auto* target = instance->player(id);
        if (!target) { lua_pushboolean(state, 0); return 1; }
        const int dialogId = static_cast<int>(luaL_checkinteger(state, 2));
        const int style = static_cast<int>(luaL_checkinteger(state, 3));
        const char* title = luaL_checkstring(state, 4);
        const char* body = luaL_checkstring(state, 5);
        const char* button1 = luaL_checkstring(state, 6);
        const char* button2 = luaL_optstring(state, 7, "");
        auto* data = queryExtension<IPlayerDialogData>(target);
        if (!data) { lua_pushboolean(state, 0); return 1; }
        data->show(*target, dialogId, static_cast<DialogStyle>(style), title, body, button1, button2);
        lua_pushboolean(state, 1);
        return 1;
    }

    static int createTextDraw(lua_State* state)
    {
        auto* instance = self(state);
        if (!instance->textdraws_) { lua_pushinteger(state, -1); return 1; }
        const float x = static_cast<float>(luaL_checknumber(state, 1));
        const float y = static_cast<float>(luaL_checknumber(state, 2));
        const char* text = luaL_checkstring(state, 3);
        auto* draw = instance->textdraws_->create(Vector2(x, y), text);
        if (!draw) { lua_pushinteger(state, -1); return 1; }
        instance->textDrawPool_[draw->getID()] = draw;
        lua_pushinteger(state, draw->getID());
        return 1;
    }

    static ITextDraw* textDraw(lua_State* state, int index)
    {
        auto* instance = self(state);
        const int id = static_cast<int>(luaL_checkinteger(state, index));
        const auto found = instance->textDrawPool_.find(id);
        return found == instance->textDrawPool_.end() ? nullptr : found->second;
    }

    static int textDrawSetText(lua_State* state)
    {
        if (auto* draw = textDraw(state, 1)) draw->setText(luaL_checkstring(state, 2));
        return 0;
    }

    static int textDrawSetLetterSize(lua_State* state)
    {
        if (auto* draw = textDraw(state, 1))
            draw->setLetterSize(Vector2(static_cast<float>(luaL_checknumber(state, 2)), static_cast<float>(luaL_checknumber(state, 3))));
        return 0;
    }

    static int textDrawSetTextSize(lua_State* state)
    {
        if (auto* draw = textDraw(state, 1))
            draw->setTextSize(Vector2(static_cast<float>(luaL_checknumber(state, 2)), static_cast<float>(luaL_checknumber(state, 3))));
        return 0;
    }

    static int textDrawColor(lua_State* state)
    {
        if (auto* draw = textDraw(state, 1)) draw->setColour(Colour::FromARGB(static_cast<uint32_t>(luaL_checknumber(state, 2))));
        return 0;
    }

    static int textDrawBox(lua_State* state)
    {
        if (auto* draw = textDraw(state, 1)) draw->useBox(lua_toboolean(state, 2) != 0);
        return 0;
    }

    static int textDrawBoxColor(lua_State* state)
    {
        if (auto* draw = textDraw(state, 1)) draw->setBoxColour(Colour::FromARGB(static_cast<uint32_t>(luaL_checknumber(state, 2))));
        return 0;
    }

    static int textDrawAlignment(lua_State* state)
    {
        if (auto* draw = textDraw(state, 1)) draw->setAlignment(static_cast<TextDrawAlignmentTypes>(luaL_checkinteger(state, 2)));
        return 0;
    }

    static int textDrawShadow(lua_State* state)
    {
        if (auto* draw = textDraw(state, 1)) draw->setShadow(static_cast<int>(luaL_checkinteger(state, 2)));
        return 0;
    }

    static int textDrawOutline(lua_State* state)
    {
        if (auto* draw = textDraw(state, 1)) draw->setOutline(static_cast<int>(luaL_checkinteger(state, 2)));
        return 0;
    }

    static int textDrawSelectable(lua_State* state)
    {
        if (auto* draw = textDraw(state, 1)) draw->setSelectable(lua_toboolean(state, 2) != 0);
        return 0;
    }

    static int textDrawShowForPlayer(lua_State* state)
    {
        auto* instance = self(state);
        if (auto* draw = textDraw(state, 1))
            if (auto* target = instance->player(static_cast<int>(luaL_checkinteger(state, 2)))) draw->showForPlayer(*target);
        return 0;
    }

    static int textDrawHideForPlayer(lua_State* state)
    {
        auto* instance = self(state);
        if (auto* draw = textDraw(state, 1))
            if (auto* target = instance->player(static_cast<int>(luaL_checkinteger(state, 2)))) draw->hideForPlayer(*target);
        return 0;
    }

    static int textDrawDestroy(lua_State* state)
    {
        auto* instance = self(state);
        const int id = static_cast<int>(luaL_checkinteger(state, 1));
        if (instance->textdraws_ && instance->textDrawPool_.erase(id))
            instance->textdraws_->release(id);
        return 0;
    }

    void registerFunction(const char* name, lua_CFunction function)
    {
        lua_pushlightuserdata(state_, this);
        lua_pushcclosure(state_, function, 1);
        lua_setglobal(state_, name);
    }

    void createState()
    {
        if (state_) lua_close(state_);
        state_ = luaL_newstate();
        if (!state_) return;
        luaopen_base(state_);
        luaopen_table(state_);
        luaopen_string(state_);
        luaopen_math(state_);
        registerFunction("print", luaPrint);
        registerFunction("SendClientMessage", sendClientMessage);
        registerFunction("SetPlayerPos", setPlayerPosition);
        registerFunction("GetPlayerName", getPlayerName);
        registerFunction("ShowPlayerDialog", showPlayerDialog);
        registerFunction("TextDrawCreate", createTextDraw);
        registerFunction("TextDrawSetString", textDrawSetText);
        registerFunction("TextDrawLetterSize", textDrawSetLetterSize);
        registerFunction("TextDrawTextSize", textDrawSetTextSize);
        registerFunction("TextDrawColor", textDrawColor);
        registerFunction("TextDrawUseBox", textDrawBox);
        registerFunction("TextDrawBoxColor", textDrawBoxColor);
        registerFunction("TextDrawAlignment", textDrawAlignment);
        registerFunction("TextDrawSetShadow", textDrawShadow);
        registerFunction("TextDrawSetOutline", textDrawOutline);
        registerFunction("TextDrawSetSelectable", textDrawSelectable);
        registerFunction("TextDrawShowForPlayer", textDrawShowForPlayer);
        registerFunction("TextDrawHideForPlayer", textDrawHideForPlayer);
        registerFunction("TextDrawDestroy", textDrawDestroy);
        lua_pushinteger(state_, 0); lua_setglobal(state_, "DIALOG_STYLE_MSGBOX");
        lua_pushinteger(state_, 1); lua_setglobal(state_, "DIALOG_STYLE_INPUT");
        lua_pushinteger(state_, 2); lua_setglobal(state_, "DIALOG_STYLE_LIST");
        lua_pushinteger(state_, 3); lua_setglobal(state_, "DIALOG_STYLE_PASSWORD");
        lua_pushinteger(state_, 4); lua_setglobal(state_, "DIALOG_STYLE_TABLIST");
        lua_pushinteger(state_, 5); lua_setglobal(state_, "DIALOG_STYLE_TABLIST_HEADERS");
    }

    bool runFile(const std::filesystem::path& file)
    {
        if (!state_) return false;
        const auto full = std::filesystem::weakly_canonical(file);
        const auto base = std::filesystem::weakly_canonical(root_);
        const auto relative = full.lexically_relative(base);
        if (relative.empty() || relative.generic_string().rfind("..", 0) == 0 || full == base) return false;
        const auto ext = full.extension().string();
        if (ext != ".lua" && ext != ".luac") return false;
        const int result = luaL_loadfile(state_, full.string().c_str());
        if (result == 0) {
            if (lua_pcall(state_, 0, 0, 0) != 0) {
                const char* error = lua_tostring(state_, -1);
                core_->logLnU8(Error, "[Lua2OMP] %s: %s", relative.string().c_str(), error ? error : "unknown runtime error");
                lua_pop(state_, 1);
                return false;
            }
            core_->printLnU8("[Lua2OMP] Loaded %s", relative.string().c_str());
            return true;
        }
        const char* error = lua_tostring(state_, -1);
        core_->logLnU8(Error, "[Lua2OMP] %s: %s", relative.string().c_str(), error ? error : "load error");
        lua_pop(state_, 1);
        return false;
    }

    void loadAutorun()
    {
        const auto autorun = root_ / "autorun";
        std::filesystem::create_directories(autorun);
        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::directory_iterator(autorun))
            if (entry.is_regular_file() && (entry.path().extension() == ".lua" || entry.path().extension() == ".luac"))
                files.push_back(entry.path());
        std::sort(files.begin(), files.end());
        for (const auto& file : files) runFile(file);
    }

public:
    StringView componentName() const override { return "Lua2OMP"; }
    SemanticVersion componentVersion() const override { return SemanticVersion(1, 0, 0, BUILD_NUMBER); }
    UID getUID() override { return UID(0x4c7561324f4d5001); }

    void onLoad(ICore* core) override
    {
        core_ = core;
        root_ = std::filesystem::current_path() / "lua";
        std::filesystem::create_directories(root_);
        createState();
    }

    void onInit(IComponentList* components) override
    {
        console_ = components->queryComponent<IConsoleComponent>();
        dialogs_ = components->queryComponent<IDialogsComponent>();
        textdraws_ = components->queryComponent<ITextDrawsComponent>();
        if (console_) console_->getEventDispatcher().addEventHandler(this);
        if (dialogs_) dialogs_->getEventDispatcher().addEventHandler(this);
        if (textdraws_) textdraws_->getEventDispatcher().addEventHandler(this);
    }

    void onReady() override
    {
        core_->printLnU8("[Lua2OMP] Lua 5.1 runtime initialized. Script folder: lua/");
        loadAutorun();
    }

    void onFree(IComponent* component) override
    {
        if (component == console_) console_ = nullptr;
        if (component == dialogs_) dialogs_ = nullptr;
        if (component == textdraws_) textdraws_ = nullptr;
    }

    bool onConsoleText(StringView command, StringView parameters, const ConsoleCommandSenderData& sender) override
    {
        if (command == "lua_run")
        {
            if (sender.sender != ConsoleCommandSender::Console) return true;
            std::string name(parameters);
            while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) name.erase(name.begin());
            if (name.empty()) {
                core_->printLnU8("Usage: lua_run <file.lua|file.luac>");
                return true;
            }
            runFile(root_ / name);
            return true;
        }
        if (command == "lua_reload")
        {
            if (sender.sender != ConsoleCommandSender::Console) return true;
            textDrawPool_.clear();
            createState();
            loadAutorun();
            core_->printLnU8("[Lua2OMP] Runtime reloaded.");
            return true;
        }
        return false;
    }

    void onDialogResponse(IPlayer& player, int dialogId, DialogResponse response, int listItem, StringView inputText) override
    {
        if (!state_) return;
        lua_getglobal(state_, "OnDialogResponse");
        if (!lua_isfunction(state_, -1)) { lua_pop(state_, 1); return; }
        lua_pushinteger(state_, player.getID());
        lua_pushinteger(state_, dialogId);
        lua_pushinteger(state_, static_cast<int>(response));
        lua_pushinteger(state_, listItem);
        lua_pushlstring(state_, inputText.data(), inputText.size());
        if (lua_pcall(state_, 5, 0, 0) != 0) {
            const char* error = lua_tostring(state_, -1);
            core_->logLnU8(Error, "[Lua2OMP] OnDialogResponse: %s", error ? error : "runtime error");
            lua_pop(state_, 1);
        }
    }

    void onPlayerClickTextDraw(IPlayer& player, ITextDraw& draw) override
    {
        if (!state_) return;
        lua_getglobal(state_, "OnPlayerClickTextDraw");
        if (!lua_isfunction(state_, -1)) { lua_pop(state_, 1); return; }
        lua_pushinteger(state_, player.getID());
        lua_pushinteger(state_, draw.getID());
        if (lua_pcall(state_, 2, 0, 0) != 0) {
            const char* error = lua_tostring(state_, -1);
            core_->logLnU8(Error, "[Lua2OMP] OnPlayerClickTextDraw: %s", error ? error : "runtime error");
            lua_pop(state_, 1);
        }
    }

    void reset() override
    {
        textDrawPool_.clear();
        createState();
        if (core_) loadAutorun();
    }

    void free() override { delete this; }

    ~Lua2OMPComponent()
    {
        if (console_) console_->getEventDispatcher().removeEventHandler(this);
        if (dialogs_) dialogs_->getEventDispatcher().removeEventHandler(this);
        if (textdraws_) textdraws_->getEventDispatcher().removeEventHandler(this);
        if (state_) lua_close(state_);
    }
};

COMPONENT_ENTRY_POINT()
{
    return new Lua2OMPComponent();
}
