print("Lua2OMP is ready")

function OnDialogResponse(playerid, dialogid, response, listitem, inputtext)
    print("Dialog response", playerid, dialogid, response, listitem, inputtext)
end

function OnPlayerClickTextDraw(playerid, textdrawid)
    print("TextDraw clicked", playerid, textdrawid)
end
