local function toboolean(s)
	if not s then
		return false
	else
		return string.lower(tostring(s)) == "true"
	end
end

local function getopt(optstring, ...)
	local opts = { }
	local args = { ... }

	for optc, optv in optstring:gmatch"(%a)(:?)" do
		opts[optc] = { hasarg = optv == ":" }
	end

	return coroutine.wrap(function()
		local yield = coroutine.yield
		local i = 1

		while i <= #args do
			local arg = args[i]

			i = i + 1

			if arg == "--" then
				break
			elseif arg:sub(1, 1) == "-" then
				for j = 2, #arg do
					local opt = arg:sub(j, j)

					if opts[opt] then
						if opts[opt].hasarg then
							if j == #arg then
								if args[i] then
									yield(opt, args[i])
									i = i + 1
								elseif optstring:sub(1, 1) == ":" then
									yield(':', opt)
								else
									yield('?', opt)
								end
							else
								yield(opt, arg:sub(j + 1))
							end

							break
						else
							yield(opt, false)
						end
					else
						yield('?', opt)
					end
				end
			else
				yield(false, arg)
			end
		end

		for i = i, #args do
			yield(false, args[i])
		end
	end)
end

local function usage()
	print("")
	print("usage:")
	print("")
	print("$ ... generator.lua [OPTIONS]")
	print("")
	print("OPTIONS:")
	print("	-a AGENTS")
	print("		number of a-priori agents")
	print("	-t TILES")
	print("		number of tiles in NoC")
	print("	-l LOAD")
	print("		a-priori load of NoC [0.0, 1.0]")
	print("	-y BOOLEAN")
	print("		allow agents to yield their occupied resources [true, false]")
	print("	-i FILE")
	print("		lua script returing the invading agent's object")
	print("	-m MAX")
	print("		maximum number of resources per a-priori agent")
	print("")
end

local number_of_agents = 2
local number_of_tiles = 2
local load_percent = 0.4
local can_yield = true
local init_invader
local max_per_agent = math.huge

local function parse_arguments(arg)
	for opt, arg in getopt("a:t:l:y:i:m:uh", unpack(arg)) do
		if opt == "a" then
			number_of_agents = tonumber(arg)
		elseif opt == "t" then
			number_of_tiles = tonumber(arg)
		elseif opt == "l" then
			load_percent = tonumber(arg)
		elseif opt == "y" then
			can_yield = toboolean(arg)
		elseif opt == "i" then
			init_invader = arg
		elseif opt == "m" then
			max_per_agent = tonumber(arg)
		elseif opt == "u" or "h" then
			usage()
		else
			print("error: failed to parse argument")
		end
	end
end

parse_arguments(arg)

local dcop = require("dcop")
local constraint = require("constraint")

local tile = {
	"REGULAR",
	"REGULAR",
	"REGULAR",
	"REGULAR",
	"RECONFIG",
	"STREAM"
}

--local problem = dcop.new(dcop.hardware.new({ { "REGULAR", "RECONFIG" } },  number_of_tiles), number_of_agents)
local problem = dcop.new(dcop.hardware.new({ tile },  number_of_tiles), number_of_agents)

local number_of_resources = problem.hardware.number_of_resources

if load_percent > 1.0 then
	load_percent = 1.0

	print("warning: adjusting a-priori system load down to 100%")
end

--[[
if math.ceil(number_of_resources * load_percent) > max_per_agent * number_of_agents then
	local prior = number_of_agents
	number_of_agents = math.ceil(math.ceil(number_of_resources * load_percent) / max_per_agent)

	print("warning: adjusted number of agents from " .. prior .. " to " .. number_of_agents)

	problem = dcop.new(dcop.hardware.new( { tile }, number_of_tiles), number_of_agents)
end
--]]

local resources_taken = 0

downey_params = {}

for _, agent in ipairs(problem.agents) do
	local n = math.min(math.ceil((number_of_resources * load_percent) / number_of_agents), math.ceil(number_of_resources * load_percent) - resources_taken)

	local resources = {}
	local tid
	local same_tile = true

	for m = 1, n do
		local i = math.random(1, problem.hardware.number_of_resources)
		while problem.hardware.resources[i]:is_taken() do
			i = math.random(1, problem.hardware.number_of_resources)
		end
		local resource = problem.hardware.resources[i]

		if not resources[resource.type] then
			resources[resource.type] = 1
		else
			resources[resource.type] = resources[resource.type] + 1
		end

		if not tid then
			tid = resource.tile
		elseif tid ~= resource.tile then
			same_tile = false
		end

		agent:claim_resource(problem, resource.tile, resource.type)

		resources_taken = resources_taken + 1
	end

	for t, n in pairs(resources) do
		agent:add_constraint(constraint.create("TYPE", { t, n, n }))
	end

	agent:add_constraint(constraint.create("NEC_RE", { n, n }))

	same_tile = true

	for i = 1, problem.hardware.number_of_tiles do
		local tile = problem.hardware:get_tile(i)

		local claimed = false
		local foreign = false

		for _, resource in ipairs(tile) do
			if resource:is_owner(agent.id) then
				claimed = true
			elseif not resource:is_free() then
				foreign = true
			end

			if claimed and foreign then
				same_tile = false
			end

		end
	end

	if same_tile then
		-- TODO: not all resources are given out yet; another agent might get resources that break this constraint
		agent:add_constraint(constraint.create("TILE"))
	end

	downey_params[agent.id] = { A = math.random(20, 300), sigma = math.random() * 2.5 }
end

-- invading agent
local invader
if init_invader then
	if init_invader:match(".*%.lua$") then
		invader = loadfile(init_invader)()
	else
		-- not really working; issues with string escaping
		invader = loadstring(init_invader)()
	end
else
 	invader = dcop.agent.new()
	--invader:add_constraint(constraint.create("AND", { constraint.create("NEC_RE", { 2, 4 }), constraint.create("TYPE", { "RECONFIG", 1, 1 }) }))
	invader:add_constraint(constraint.create("NEC_RE", { 2, 4 }))
end
problem:add_agent(invader)
--downey_params[invader.id] = { A = math.random(20, 300), sigma = math.random(0, 2.5) }
downey_params[invader.id] = { A = 300, sigma = 0.001 }

for _, agent in ipairs(problem.agents) do
	--agent:add_constraint(constraint.create("SPEEDUP", { downey_params[agent.id].A, downey_params[agent.id].sigma }))

	for _, neighbor in ipairs(problem.agents) do
		if agent ~= neighbor then
			if can_yield then
				agent:add_constraint(constraint.create("DOWNEY", {
					neighbor,
					downey_params[agent.id].A, downey_params[agent.id].sigma,
					downey_params[neighbor.id].A, downey_params[neighbor.id].sigma
				}))
				agent:add_constraint(constraint.create("PREFER_FREE", { neighbor }))
			else
				agent:add_constraint(constraint.create("SHARE", { neighbor }))
			end
		end
	end
end

problem:load()

