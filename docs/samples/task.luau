--!strict
-- The one non-deferred call: runs before it returns.
task.spawn(function()
    print("first")
end)

-- Deferred: runs at the next resumption point, ordered against signal fires.
task.defer(function()
    print("later")
end)

-- Exactly 60 ticks at the default timestep, on every machine and every run.
print(task.wait(1))
