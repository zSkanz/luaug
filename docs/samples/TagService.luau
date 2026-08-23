--!strict
local TagService = game:GetService("TagService")

-- Everything wearing the tag now...
for _, instance in TagService:GetTagged("Checkpoint") do
    print(instance.Name)
end

-- ...and everything that ever will, whoever creates it.
TagService:GetInstanceAddedSignal("Checkpoint"):Connect(function(instance: Instance)
    print("a new checkpoint:", instance.Name)
end)
