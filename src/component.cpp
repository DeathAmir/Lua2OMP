#include <sdk.hpp>
#include <Server/Components/Console/console.hpp>
#include <Server/Components/Dialogs/dialogs.hpp>
#include <Server/Components/TextDraws/textdraws.hpp>
#include <Server/Components/Objects/objects.hpp>
#include <Server/Components/Vehicles/vehicles.hpp>
#include <Server/Components/Timers/timers.hpp>
#ifdef __cplusplus
extern "C" {
#endif
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#ifdef __cplusplus
}
#endif
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>
#include <sstream>
#include <cctype>
#include "crypto.hpp"
#include "resource_manager.hpp"

class Lua2OMPComponent final : public IComponent, public ConsoleEventHandler, public PlayerDialogEventHandler, public TextDrawEventHandler, public PlayerTextEventHandler, public PlayerConnectEventHandler, public PlayerSpawnEventHandler, public PlayerDamageEventHandler, public PlayerChangeEventHandler
{
    ICore* core_ = nullptr;
    IConsoleComponent* console_ = nullptr;
    IDialogsComponent* dialogs_ = nullptr;
    ITextDrawsComponent* textdraws_ = nullptr;
    IObjectsComponent* objects_ = nullptr;
    IVehiclesComponent* vehicles_ = nullptr;
    ITimersComponent* timers_ = nullptr;
    lua_State* state_ = nullptr;
    std::filesystem::path root_;
    std::map<int, ITextDraw*> textDrawPool_;
    std::map<int, lua_State*> textDrawOwners_;
    std::map<lua_State*, std::map<std::string, int>> commandHandlers_;
    std::map<int, IObject*> objectPool_;
    std::map<int, lua_State*> objectOwners_;
    std::map<int, IVehicle*> vehiclePool_;
    std::map<int, lua_State*> vehicleOwners_;
    int nextTimerId_ = 1;
    struct LuaTimer final : TimerTimeOutHandler {
        Lua2OMPComponent* owner; lua_State* state; int reference; int id;
        LuaTimer(Lua2OMPComponent* o, lua_State* s, int r, int i) : owner(o), state(s), reference(r), id(i) {}
        void timeout(ITimer&) override { lua_rawgeti(state, LUA_REGISTRYINDEX, reference); if (lua_pcall(state, 0, 0, 0) != 0) { const char* e = lua_tostring(state, -1); if (owner->core_) owner->core_->logLnU8(Error, "[Lua2OMP] Timer: %s", e ? e : "runtime error"); lua_pop(state, 1); } }
        void free(ITimer&) override { owner->timerPool_.erase(id); owner->timerOwners_.erase(id); luaL_unref(state, LUA_REGISTRYINDEX, reference); delete this; }
    };
    std::map<int, ITimer*> timerPool_;
    std::map<int, lua_State*> timerOwners_;
    LuaResourceManager resources_;

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
        instance->textDrawOwners_[draw->getID()] = state;
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
        if (instance->textdraws_ && instance->textDrawPool_.erase(id)) {
            instance->textDrawOwners_.erase(id);
            instance->textdraws_->release(id);
        }
        return 0;
    }

    void registerFunction(lua_State* target, const char* name, lua_CFunction function)
    {
        lua_pushlightuserdata(target, this);
        lua_pushcclosure(target, function, 1);
        lua_setglobal(target, name);
    }

    void registerFunction(const char* name, lua_CFunction function)
    {
        registerFunction(state_, name, function);
    }


    static int getPlayerDrunkLevel(lua_State* state)
    {
        auto* instance = self(state);
        auto* target = instance->player(static_cast<int>(luaL_checkinteger(state, 1)));
        if (!target) { lua_pushnil(state); return 1; }
        lua_pushinteger(state, target->getDrunkLevel());
        return 1;
    }

    static int setPlayerDrunkLevel(lua_State* state)
    {
        auto* instance = self(state);
        auto* target = instance->player(static_cast<int>(luaL_checkinteger(state, 1)));
        if (!target) { lua_pushboolean(state, 0); return 1; }
        target->setDrunkLevel(static_cast<int>(luaL_checkinteger(state, 2)));
        lua_pushboolean(state, 1); return 1;
    }

    static int addCommandHandler(lua_State* state)
    {
        auto* instance = self(state);
        const char* name = luaL_checkstring(state, 1);
        luaL_checktype(state, 2, LUA_TFUNCTION);
        std::string command(name);
        if (command.empty() || command.find_first_of(" \t/") != std::string::npos) { lua_pushboolean(state, 0); return 1; }
        lua_pushvalue(state, 2);
        const int ref = luaL_ref(state, LUA_REGISTRYINDEX);
        auto& handlers = instance->commandHandlers_[state];
        auto old = handlers.find(command);
        if (old != handlers.end()) luaL_unref(state, LUA_REGISTRYINDEX, old->second);
        handlers[command] = ref;
        lua_pushboolean(state, 1); return 1;
    }

    static int createObject(lua_State* state)
    {
        auto* instance = self(state);
        if (!instance->objects_) { lua_pushinteger(state, -1); return 1; }
        const int model = static_cast<int>(luaL_checkinteger(state, 1));
        Vector3 pos(static_cast<float>(luaL_checknumber(state, 2)), static_cast<float>(luaL_checknumber(state, 3)), static_cast<float>(luaL_checknumber(state, 4)));
        Vector3 rot(static_cast<float>(luaL_checknumber(state, 5)), static_cast<float>(luaL_checknumber(state, 6)), static_cast<float>(luaL_checknumber(state, 7)));
        auto* object = instance->objects_->create(model, pos, rot);
        if (!object) { lua_pushinteger(state, -1); return 1; }
        const int id = object->getID(); instance->objectPool_[id] = object; instance->objectOwners_[id] = state;
        lua_pushinteger(state, id); return 1;
    }

    static int destroyObject(lua_State* state)
    {
        auto* instance = self(state); const int id = static_cast<int>(luaL_checkinteger(state, 1));
        auto it = instance->objectPool_.find(id);
        if (it != instance->objectPool_.end() && instance->objectOwners_[id] == state) {
            instance->objects_->release(id); instance->objectPool_.erase(it); instance->objectOwners_.erase(id); lua_pushboolean(state, 1);
        } else lua_pushboolean(state, 0);
        return 1;
    }

    static int setObjectPos(lua_State* state)
    {
        auto* instance = self(state); const int id = static_cast<int>(luaL_checkinteger(state, 1));
        auto it = instance->objectPool_.find(id);
        if (it == instance->objectPool_.end() || instance->objectOwners_[id] != state) { lua_pushboolean(state, 0); return 1; }
        it->second->setPosition(Vector3(static_cast<float>(luaL_checknumber(state, 2)), static_cast<float>(luaL_checknumber(state, 3)), static_cast<float>(luaL_checknumber(state, 4))));
        lua_pushboolean(state, 1); return 1;
    }

    static int setObjectRot(lua_State* state)
    {
        auto* instance = self(state); const int id = static_cast<int>(luaL_checkinteger(state, 1));
        auto it = instance->objectPool_.find(id);
        if (it == instance->objectPool_.end() || instance->objectOwners_[id] != state) { lua_pushboolean(state, 0); return 1; }
        it->second->setRotation(GTAQuat(Vector3(static_cast<float>(luaL_checknumber(state, 2)), static_cast<float>(luaL_checknumber(state, 3)), static_cast<float>(luaL_checknumber(state, 4)))));
        lua_pushboolean(state, 1); return 1;
    }

    static int createVehicle(lua_State* state)
    {
        auto* instance = self(state);
        if (!instance->vehicles_) { lua_pushinteger(state, -1); return 1; }
        const int model = static_cast<int>(luaL_checkinteger(state, 1));
        Vector3 pos(static_cast<float>(luaL_checknumber(state, 2)), static_cast<float>(luaL_checknumber(state, 3)), static_cast<float>(luaL_checknumber(state, 4)));
        auto* vehicle = instance->vehicles_->create(false, model, pos, static_cast<float>(luaL_optnumber(state, 5, 0)), static_cast<int>(luaL_optinteger(state, 6, -1)), static_cast<int>(luaL_optinteger(state, 7, -1)));
        if (!vehicle) { lua_pushinteger(state, -1); return 1; }
        const int id = vehicle->getID(); instance->vehiclePool_[id] = vehicle; instance->vehicleOwners_[id] = state;
        lua_pushinteger(state, id); return 1;
    }

    static int destroyVehicle(lua_State* state)
    {
        auto* instance = self(state); const int id = static_cast<int>(luaL_checkinteger(state, 1));
        auto it = instance->vehiclePool_.find(id);
        if (it != instance->vehiclePool_.end() && instance->vehicleOwners_[id] == state) {
            instance->vehicles_->release(id); instance->vehiclePool_.erase(it); instance->vehicleOwners_.erase(id); lua_pushboolean(state, 1);
        } else lua_pushboolean(state, 0);
        return 1;
    }

    static int setVehicleHealth(lua_State* state)
    {
        auto* instance = self(state); const int id = static_cast<int>(luaL_checkinteger(state, 1));
        auto it = instance->vehiclePool_.find(id);
        if (it == instance->vehiclePool_.end() || instance->vehicleOwners_[id] != state) { lua_pushboolean(state, 0); return 1; }
        it->second->setHealth(static_cast<float>(luaL_checknumber(state, 2))); lua_pushboolean(state, 1); return 1;
    }

    static int setVehiclePos(lua_State* state)
    {
        auto* instance = self(state); const int id = static_cast<int>(luaL_checkinteger(state, 1));
        auto it = instance->vehiclePool_.find(id);
        if (it == instance->vehiclePool_.end() || instance->vehicleOwners_[id] != state) { lua_pushboolean(state, 0); return 1; }
        it->second->setPosition(Vector3(static_cast<float>(luaL_checknumber(state, 2)), static_cast<float>(luaL_checknumber(state, 3)), static_cast<float>(luaL_checknumber(state, 4))));
        lua_pushboolean(state, 1); return 1;
    }

    static int setTimer(lua_State* state)
    {
        auto* instance = self(state);
        if (!instance->timers_) { lua_pushnil(state); return 1; }
        luaL_checktype(state, 1, LUA_TFUNCTION);
        const int interval = static_cast<int>(luaL_checkinteger(state, 2));
        const bool repeating = lua_toboolean(state, 3) != 0;
        if (interval < 1) { lua_pushnil(state); return 1; }
        lua_pushvalue(state, 1);
        const int ref = luaL_ref(state, LUA_REGISTRYINDEX);
        const int id = instance->nextTimerId_++;
        auto* handler = new LuaTimer(instance, state, ref, id);
        auto* timer = instance->timers_->create(handler, Milliseconds(interval), repeating);
        if (!timer) { luaL_unref(state, LUA_REGISTRYINDEX, ref); delete handler; lua_pushnil(state); return 1; }
        instance->timerPool_[id] = timer; instance->timerOwners_[id] = state;
        lua_pushinteger(state, id); return 1;
    }

    static int killTimer(lua_State* state)
    {
        auto* instance = self(state); const int id = static_cast<int>(luaL_checkinteger(state, 1));
        auto it = instance->timerPool_.find(id);
        if (it == instance->timerPool_.end() || instance->timerOwners_[id] != state) { lua_pushboolean(state, 0); return 1; }
        it->second->kill(); lua_pushboolean(state, 1); return 1;
    }

    void cleanupResource(lua_State* state)
    {
        for (auto it = textDrawOwners_.begin(); it != textDrawOwners_.end();) {
            if (it->second == state) { if (textdraws_) textdraws_->release(it->first); textDrawPool_.erase(it->first); it = textDrawOwners_.erase(it); } else ++it;
        }
        for (auto it = objectOwners_.begin(); it != objectOwners_.end();) {
            if (it->second == state) { if (objects_) objects_->release(it->first); objectPool_.erase(it->first); it = objectOwners_.erase(it); } else ++it;
        }
        for (auto it = vehicleOwners_.begin(); it != vehicleOwners_.end();) {
            if (it->second == state) { if (vehicles_) vehicles_->release(it->first); vehiclePool_.erase(it->first); it = vehicleOwners_.erase(it); } else ++it;
        }
        std::vector<int> timerIds;
        for (const auto& item : timerOwners_) if (item.second == state) timerIds.push_back(item.first);
        for (int id : timerIds) { auto it = timerPool_.find(id); if (it != timerPool_.end()) it->second->kill(); }
        auto commands = commandHandlers_.find(state);
        if (commands != commandHandlers_.end()) { for (const auto& item : commands->second) luaL_unref(state, LUA_REGISTRYINDEX, item.second); commandHandlers_.erase(commands); }
    }

    void bindResourceState(lua_State* target)
    {
        luaopen_base(target);
        luaopen_table(target);
        luaopen_string(target);
        luaopen_math(target);
        luaopen_package(target);
        registerFunction(target, "print", luaPrint);
        registerFunction(target, "SendClientMessage", sendClientMessage);
        registerFunction(target, "SetPlayerPos", setPlayerPosition);
        registerFunction(target, "GetPlayerName", getPlayerName);
        registerFunction(target, "GetPlayerDrunkLevel", getPlayerDrunkLevel);
        registerFunction(target, "SetPlayerDrunkLevel", setPlayerDrunkLevel);
        registerFunction(target, "addCommandHandler", addCommandHandler);
        registerFunction(target, "CreateObject", createObject);
        registerFunction(target, "DestroyObject", destroyObject);
        registerFunction(target, "SetObjectPos", setObjectPos);
        registerFunction(target, "SetObjectRot", setObjectRot);
        registerFunction(target, "CreateVehicle", createVehicle);
        registerFunction(target, "DestroyVehicle", destroyVehicle);
        registerFunction(target, "SetVehicleHealth", setVehicleHealth);
        registerFunction(target, "SetVehiclePos", setVehiclePos);
        registerFunction(target, "setTimer", setTimer);
        registerFunction(target, "killTimer", killTimer);
        registerFunction(target, "ShowPlayerDialog", showPlayerDialog);
        registerFunction(target, "TextDrawCreate", createTextDraw);
        registerFunction(target, "TextDrawSetString", textDrawSetText);
        registerFunction(target, "TextDrawLetterSize", textDrawSetLetterSize);
        registerFunction(target, "TextDrawTextSize", textDrawSetTextSize);
        registerFunction(target, "TextDrawColor", textDrawColor);
        registerFunction(target, "TextDrawUseBox", textDrawBox);
        registerFunction(target, "TextDrawBoxColor", textDrawBoxColor);
        registerFunction(target, "TextDrawAlignment", textDrawAlignment);
        registerFunction(target, "TextDrawSetShadow", textDrawShadow);
        registerFunction(target, "TextDrawSetOutline", textDrawOutline);
        registerFunction(target, "TextDrawSetSelectable", textDrawSelectable);
        registerFunction(target, "TextDrawShowForPlayer", textDrawShowForPlayer);
        registerFunction(target, "TextDrawHideForPlayer", textDrawHideForPlayer);
        registerFunction(target, "TextDrawDestroy", textDrawDestroy);
        lua_pushinteger(target, 0); lua_setglobal(target, "DIALOG_STYLE_MSGBOX");
        lua_pushinteger(target, 1); lua_setglobal(target, "DIALOG_STYLE_INPUT");
        lua_pushinteger(target, 2); lua_setglobal(target, "DIALOG_STYLE_LIST");
        lua_pushinteger(target, 3); lua_setglobal(target, "DIALOG_STYLE_PASSWORD");
        lua_pushinteger(target, 4); lua_setglobal(target, "DIALOG_STYLE_TABLIST");
        lua_pushinteger(target, 5); lua_setglobal(target, "DIALOG_STYLE_TABLIST_HEADERS");
    }

    void createState()
    {
        if (state_) { cleanupResource(state_); lua_close(state_); }
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
        registerFunction("GetPlayerDrunkLevel", getPlayerDrunkLevel);
        registerFunction("SetPlayerDrunkLevel", setPlayerDrunkLevel);
        registerFunction("addCommandHandler", addCommandHandler);
        registerFunction("CreateObject", createObject);
        registerFunction("DestroyObject", destroyObject);
        registerFunction("SetObjectPos", setObjectPos);
        registerFunction("SetObjectRot", setObjectRot);
        registerFunction("CreateVehicle", createVehicle);
        registerFunction("DestroyVehicle", destroyVehicle);
        registerFunction("SetVehicleHealth", setVehicleHealth);
        registerFunction("SetVehiclePos", setVehiclePos);
        registerFunction("setTimer", setTimer);
        registerFunction("killTimer", killTimer);
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
        if (ext != ".lua" && ext != ".luac" && ext != ".lal") return false;
        int result = 0;
        std::vector<unsigned char> decrypted;
        std::string decryptError;
        if (ext == ".lal") {
            if (!lua2omp_crypto::loadEncrypted(full.string(), decrypted, decryptError)) {
                core_->logLnU8(Error, "[Lua2OMP] ERROR: %s: %s", relative.string().c_str(), decryptError.c_str());
                return false;
            }
            result = luaL_loadbuffer(state_, reinterpret_cast<const char*>(decrypted.data()), decrypted.size(), relative.string().c_str());
        } else result = luaL_loadfile(state_, full.string().c_str());
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
            if (entry.is_regular_file() && (entry.path().extension() == ".lua" || entry.path().extension() == ".luac" || entry.path().extension() == ".lal"))
                files.push_back(entry.path());
        std::sort(files.begin(), files.end());
        for (const auto& file : files) runFile(file);
    }


    template<class PushArgs>
    void dispatchEvent(const char* callback, PushArgs pushArgs)
    {
        resources_.call(callback, pushArgs);
        if (!state_) return;
        lua_getglobal(state_, callback);
        if (!lua_isfunction(state_, -1)) { lua_pop(state_, 1); return; }
        const int count = pushArgs(state_);
        if (lua_pcall(state_, count, 0, 0) != 0) {
            const char* error = lua_tostring(state_, -1);
            core_->logLnU8(Error, "[Lua2OMP] %s: %s", callback, error ? error : "runtime error");
            lua_pop(state_, 1);
        }
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
        objects_ = components->queryComponent<IObjectsComponent>();
        vehicles_ = components->queryComponent<IVehiclesComponent>();
        timers_ = components->queryComponent<ITimersComponent>();
        resources_.setStopper([this](lua_State* target) { cleanupResource(target); });
        resources_.setLogger([this](const std::string& message) { if (core_) core_->logLnU8(Error, "[Lua2OMP] %s", message.c_str()); });
        core_->getPlayers().getPlayerTextDispatcher().addEventHandler(this);
        core_->getPlayers().getPlayerConnectDispatcher().addEventHandler(this);
        core_->getPlayers().getPlayerSpawnDispatcher().addEventHandler(this);
        core_->getPlayers().getPlayerDamageDispatcher().addEventHandler(this);
        core_->getPlayers().getPlayerChangeDispatcher().addEventHandler(this);
        resources_.setRoot(root_ / "resources");
        resources_.setBinder([this](lua_State* target) { bindResourceState(target); });
        if (console_) console_->getEventDispatcher().addEventHandler(this);
        if (dialogs_) dialogs_->getEventDispatcher().addEventHandler(this);
        if (textdraws_) textdraws_->getEventDispatcher().addEventHandler(this);
    }

    void onReady() override
    {
        core_->printLnU8("[Lua2OMP] Lua 5.1 runtime initialized. Script folder: lua/");
        loadAutorun();
        resources_.refresh();
    }

    void onFree(IComponent* component) override
    {
        if (component == console_) console_ = nullptr;
        if (component == dialogs_) dialogs_ = nullptr;
        if (component == textdraws_) textdraws_ = nullptr;
        if (component == objects_) objects_ = nullptr;
        if (component == vehicles_) vehicles_ = nullptr;
        if (component == timers_) timers_ = nullptr;
    }

    bool onConsoleText(StringView command, StringView parameters, const ConsoleCommandSenderData& sender) override
    {
        if (sender.sender != ConsoleCommandSender::Console) return false;
        std::string args(parameters);
        while (!args.empty() && std::isspace(static_cast<unsigned char>(args.front()))) args.erase(args.begin());
        while (!args.empty() && std::isspace(static_cast<unsigned char>(args.back()))) args.pop_back();

        if (command == "start" || command == "stop" || command == "restart")
        {
            if (args.empty()) {
                core_->printLnU8("Usage: %.*s <resource>", static_cast<int>(command.size()), command.data());
                return true;
            }
            bool ok = command == "start" ? resources_.start(args) :
                      command == "stop" ? resources_.stop(args) : resources_.restart(args);
            core_->printLnU8("[Lua2OMP] %s resource '%s': %s", std::string(command).c_str(), args.c_str(), ok ? "success" : resources_.lastError().c_str());
            return true;
        }
        if (command == "refresh")
        {
            const auto count = resources_.refresh();
            core_->printLnU8("[Lua2OMP] %llu resource(s) discovered.", static_cast<unsigned long long>(count));
            return true;
        }
        if (command == "resources")
        {
            resources_.printList([this](const std::string& line) { core_->printLnU8("%s", line.c_str()); });
            return true;
        }
        if (command == "lua_run")
        {
            if (args.empty()) { core_->printLnU8("Usage: lua_run <file.lua|file.luac|file.lal>"); return true; }
            runFile(root_ / args);
            return true;
        }
        if (command == "lua_reload")
        {
            textDrawPool_.clear();
            createState();
            loadAutorun();
            resources_.restartAll();
            core_->printLnU8("[Lua2OMP] Runtime reloaded.");
            return true;
        }
        return false;
    }


    void onPlayerConnect(IPlayer& player) override
    {
        dispatchEvent("OnPlayerConnect", [&](lua_State* target) { lua_pushinteger(target, player.getID()); return 1; });
    }

    void onPlayerDisconnect(IPlayer& player, PeerDisconnectReason reason) override
    {
        dispatchEvent("OnPlayerDisconnect", [&](lua_State* target) { lua_pushinteger(target, player.getID()); lua_pushinteger(target, static_cast<int>(reason)); return 2; });
    }

    void onPlayerSpawn(IPlayer& player) override
    {
        dispatchEvent("OnPlayerSpawn", [&](lua_State* target) { lua_pushinteger(target, player.getID()); return 1; });
    }

    void onPlayerDeath(IPlayer& player, IPlayer* killer, int reason) override
    {
        dispatchEvent("OnPlayerDeath", [&](lua_State* target) { lua_pushinteger(target, player.getID()); lua_pushinteger(target, killer ? killer->getID() : -1); lua_pushinteger(target, reason); return 3; });
    }

    void onPlayerStateChange(IPlayer& player, PlayerState newState, PlayerState oldState) override
    {
        dispatchEvent("OnPlayerStateChange", [&](lua_State* target) { lua_pushinteger(target, player.getID()); lua_pushinteger(target, static_cast<int>(newState)); lua_pushinteger(target, static_cast<int>(oldState)); return 3; });
    }

    bool onPlayerText(IPlayer& player, StringView message) override
    {
        dispatchEvent("OnPlayerText", [&](lua_State* target) { lua_pushinteger(target, player.getID()); lua_pushlstring(target, message.data(), message.size()); return 2; });
        return true;
    }

    bool onPlayerCommandText(IPlayer& player, StringView message) override
    {
        std::string text(message);
        if (text.empty() || text[0] != '/') return false;
        const auto split = text.find_first_of(" \t");
        const std::string command = text.substr(1, split == std::string::npos ? std::string::npos : split - 1);
        const std::string params = split == std::string::npos ? "" : text.substr(split + 1);
        bool handled = false;
        for (auto& entry : commandHandlers_) {
            auto it = entry.second.find(command);
            if (it == entry.second.end()) continue;
            lua_State* target = entry.first;
            lua_rawgeti(target, LUA_REGISTRYINDEX, it->second);
            lua_pushinteger(target, player.getID());
            lua_pushlstring(target, params.data(), params.size());
            if (lua_pcall(target, 2, 1, 0) != 0) {
                const char* error = lua_tostring(target, -1);
                core_->logLnU8(Error, "[Lua2OMP] command /%s: %s", command.c_str(), error ? error : "runtime error");
                lua_pop(target, 1);
            } else { handled = true; lua_pop(target, 1); }
        }
        return handled;
    }

    void onDialogResponse(IPlayer& player, int dialogId, DialogResponse response, int listItem, StringView inputText) override
    {
        resources_.call("OnDialogResponse", [&](lua_State* target) { lua_pushinteger(target, player.getID()); lua_pushinteger(target, dialogId); lua_pushinteger(target, static_cast<int>(response)); lua_pushinteger(target, listItem); lua_pushlstring(target, inputText.data(), inputText.size()); return 5; });
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
        resources_.call("OnPlayerClickTextDraw", [&](lua_State* target) { lua_pushinteger(target, player.getID()); lua_pushinteger(target, draw.getID()); return 2; });
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
        if (core_) {
            core_->getPlayers().getPlayerTextDispatcher().removeEventHandler(this);
            core_->getPlayers().getPlayerConnectDispatcher().removeEventHandler(this);
            core_->getPlayers().getPlayerSpawnDispatcher().removeEventHandler(this);
            core_->getPlayers().getPlayerDamageDispatcher().removeEventHandler(this);
            core_->getPlayers().getPlayerChangeDispatcher().removeEventHandler(this);
        }
        resources_.stopAll();
        if (state_) { cleanupResource(state_); lua_close(state_); }
    }
};

COMPONENT_ENTRY_POINT()
{
    return new Lua2OMPComponent();
}
