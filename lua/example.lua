local dcop = require("dcop")

local hardware = dcop.hardware.new()

local tile = {
	"REGULAR",
	"REGULAR",
	"REGULAR",
	"REGULAR",
	"RECONFIG",
	"STREAM"
}

hardware:add_tile(tile)
hardware:add_tile(tile)

local problem = dcop.new(hardware)

local agent1 = dcop.agent.new()
local agent2 = dcop.agent.new()
local agent3 = dcop.agent.new()
problem:add_agent(agent1)
problem:add_agent(agent2)
problem:add_agent(agent3)

agent1:claim_resource(hardware, 1)
agent1:claim_resource(hardware, 6)
agent2:claim_resource(hardware, 7)
agent2:claim_resource(hardware, 8)
agent2:claim_resource(hardware, 9)

local c1 = dcop.constraint.new("NEC_RE", function(args)
	local a = args[1]
	local i =  0
	for _, r in ipairs(a.view) do
		if r.status == dcop.resource.status.TAKEN and r.owner == a.id then
			i = i + 1
		end
	end
	if i >= 2 then
		return 0
	else
		return math.huge
	end
end, { agent3 })
agent3:add_constraint(c1)

--agent1:neighbors()

agent1:rate_view()

-- agent3 invading

problem:load()

print("TEST FINISHED.")
