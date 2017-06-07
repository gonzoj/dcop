local dcop = require("dcop")

local function tile_constraint(param) 
	--print("TILE_CONSTRAINT")
	local a = param.agent
	local tile
	for _, r in ipairs(a.view) do
		if r.status == dcop.resource.status.TAKEN and r.owner == a.id then
			if not tile then
				tile = r.tile
			elseif tile ~= r.tile then
				return math.huge	
			end
		end
	end

	return 0
end

local function resource_constraint(param)
	local a = param.agent
	local n = param.args[1]
	local m = param.args[2]
	local i =  0
	for _, r in ipairs(a.view) do
		if r.status == dcop.resource.status.TAKEN then
			--io.write(r.owner)
		else
			--io.write("-")
		end
		--io.write(" ")
		if r.status == dcop.resource.status.TAKEN and r.owner == a.id then
			i = i + 1
		end
	end
	--io.write("\n")
	--print(string.format("resource constraint for agent%i: %i <= %i <= %i\n", a.id, n, i, m))
	if i >= n  and i <= m then
		return 0
	else
		return math.huge
	end
end

local function type_constraint(param)
	local a = param.agent
	local t = param.args[1]
	local n = param.args[2]
	local m = param.args[3]
	local i =  0
	for _, r in ipairs(a.view) do
		if r.status == dcop.resource.status.TAKEN and r.owner == a.id  and r.type == t then
			i = i + 1
		end
	end
	if i >= n and i <= m then
		return 0
	else
		return math.huge
	end
end

local function share_constraint(param)
	local a = param.agent
	local b = param.neighbors[1]
	for i in ipairs(a.view) do
		if a.view[i].status == dcop.resource.status.TAKEN and a.view[i].owner == a.id and
		   a.agent_view[b][i].status == dcop.resource.status.TAKEN and a.agent_view[b][i].owner == b then
			return math.huge
		end
	end
	
	return 0
end

local hardware = dcop.hardware.new()

local tile = {
	"REGULAR",
	"REGULAR",
	"REGULAR",
	"REGULAR",
	"RECONFIG",
	"STREAM"
}

hardware:add_tile(tile, 2)

local problem = dcop.new(hardware)

local agent1, agent2, agent3 = problem:create_agents(3)

agent1:claim_resource(problem, 1, "REGULAR")
agent1:claim_resource(problem, 1, "STREAM")
agent2:claim_resource(problem, 2, "REGULAR")
agent2:claim_resource(problem, 2, "REGULAR")
agent2:claim_resource(problem, 2, "REGULAR")
agent2:claim_resource(problem, 2, "REGULAR")

agent1:add_constraint(dcop.constraint.new("TILE", tile_constraint, {}))
agent1:add_constraint(dcop.constraint.new("TYPE", type_constraint, { "STREAM", 1, 1 }))
agent1:add_constraint(dcop.constraint.new("TYPE", type_constraint, { "REGULAR", 1, 1 }))
agent1:add_constraint(dcop.constraint.new("SHARE", share_constraint, { agent2 }))
agent1:add_constraint(dcop.constraint.new("SHARE", share_constraint, { agent3 }))

agent2:add_constraint(dcop.constraint.new("NEC_RE", resource_constraint, { 5, 5 }))
agent2:add_constraint(dcop.constraint.new("TYPE", type_constraint, { "RECONFIG", 1, 1 }))
agent2:add_constraint(dcop.constraint.new("SHARE", share_constraint, { agent1 }))
agent2:add_constraint(dcop.constraint.new("SHARE", share_constraint, { agent3 }))

agent3:add_constraint(dcop.constraint.new("TILE", tile_constraint, {}))
--agent3:add_constraint(dcop.constraint.new("NEC_RE", resource_constraint, { 2, 2 }))
agent3:add_constraint(dcop.constraint.new("TYPE", type_constraint, { "REGULAR", 2, 2 }))
agent3:add_constraint(dcop.constraint.new("TYPE", type_constraint, { "STREAM", 1, 1 }))
--agent3:add_constraint(dcop.constraint.new("SHARE", share_constraint, { agent1 }))
--agent3:add_constraint(dcop.constraint.new("SHARE", share_constraint, { agent2 }))

problem:load()

--agent1:rate_view()

--print("EXAMPLE.LUA LOADED")

