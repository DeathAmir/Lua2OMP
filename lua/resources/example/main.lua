function OnResourceStart()
    print("Example resource started:", Config.welcome)
end

function OnResourceStop()
    print("Example resource stopped")
end

function OnDialogResponse(playerid, dialogid, response, listitem, inputtext)
    Messages.send(playerid, "Dialog response received")
end
