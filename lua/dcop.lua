local base = require("_G")
local math = require("math")
local table = require("table")

module("dcop")

local function typeof(o)
	return base.type(o) == "table" and base.getmetatable(o) and base.getmetatable(o).__object_type or nil
end

local function check_object_type(o, t)
	base.assert(typeof(o) == t, "'" .. base.tostring(o) .. "' is not of type '" .. t .. "'")
end

local function contains(t, s, i, f)
	base.assert(base.type(t) == "table")
	base.assert(base.type(i) == "function" or i == nil)
	base.assert(base.type(f) == "function" or f == nil)

	for _, v in i and i(t) or base.ipairs(t) do
		if f and f(v, s) or v == s then
			return true
		end
	end
	return false
end

resource = {}

resource.status = {
	UNKNOWN = -1,
	FREE = 0,
	TAKEN = 1
}

resource.new = function(t, tile)
	base.assert(base.type(t) == "string", "resource.type must be a string")
	base.assert(base.type(tile) == "number" and tile > 0, "resource.tile must be a number > 0")

	local r = {}

	local mt = {}
	base.setmetatable(r, mt)

	mt.__object_type = "resource"

	r.type = t
	r.status = resource.status.FREE
	r.tile = tile

	return r
end

hardware = {}

hardware.new = function()
	local hw = {}
	
	local mt = {}
	base.setmetatable(hw, mt)

	mt.__object_type = "hardware"

	hw.resources = {}
	hw.number_of_tiles = 0

	hw.add_resource = function(this, r)
		check_object_type(this, "hardware")
		check_object_type(r, "resource")

		base.assert(r.tile > 0 and r.tile <= this.number_of_tiles, "unknown tile " .. r.tile)

		table.insert(this.resources, r)
	end
	
	hw.add_tile = function(this, t)
		check_object_type(this, "hardware")
		base.assert(base.type(t) == "table", "argument 't' must be a table")

		this.number_of_tiles = this.number_of_tiles + 1

		for _, r in base.ipairs(t) do
			local resource = resource.new(base.tostring(r), this.number_of_tiles)
			this:add_resource(resource)
		end
	end

	hw.get_tile = function(this, t)
		check_object_type(this, "hardware")
		base.assert(t > 0 and t <= this.number_of_tiles, "unknown tile " .. t)

		local tile = {}
		for _, r in base.ipairs(this.resources) do
			if r.tile == t then
				table.insert(tile, r)
			end
		end

		return tile
	end

	return hw
end

agent = {}

agent.new = function(i)
	local a = {}

	local mt = {}
	base.setmetatable(a, mt)

	mt.__object_type = "agent"

	a.id = i

	a.constraints = {}
	a.add_constraint = function(this, c)
		check_object_type(this, "agent")
		check_object_type(c, "constraint")

		table.insert(this.constraints, c)
	end
	a.view = {}
	a.rate_view = function(this)
		check_object_type(this, "agent")

		local rating = 0
		for _, c in base.ipairs(this.constraints) do
			rating = rating + c.eval(c.args)
		end
		return rating
	end
	a.neighbors = function(this)
		check_object_type(this, "agent")

		local n = {}

		local process_constraint
		process_constraint = function(agent, list, args)
			for _, a in base.ipairs(args) do
				if typeof(a) == "agent" and a ~= agent and not contains(list, a) then
					table.insert(list, a)
				elseif typeof(a) == "constraint" then
					process_constraint(agent, list, a.args)
				end
			end
		end
		process_constraint(this, n, this.constraints)

		return n
	end
	a.claim_resource = function(this, hw, r)
		check_object_type(this, "agent")
		check_object_type(hw, "hardware")
		base.assert(base.type(r) == "number", "resource argument must be a number")

		this.view[r] = resource.new(hw.resources[r].type, hw.resources[r].tile)
		this.view[r].status = resource.status.TAKEN
		this.view[r].owner = this.id
		hw.resources[r].status = resource.status.TAKEN
		this.view[r].owner = this.id
	end

	return a
end

constraint = {}

constraint.new = function(n, f, a)
	base.assert(base.type(n) == "string", "constraint.name must be a string")
	base.assert(base.type(f) == "function", "constraint.eval must be a function")
	base.assert(base.type(a) == "table", "constraint.args must be a table")

	local c = {}

	local mt = {}
	base.setmetatable(c, mt)

	mt.__object_type = "constraint"

	c.name = n
	c.eval = f
	c.args = a

	return c
end

function new(hw)
	check_object_type(hw, "hardware")

	local p = {}

	local mt = {}
	base.setmetatable(p, mt)

	mt.__object_type = "dcop"

	p.hardware = hw

	p.agents = {}

	p.add_agent = function(this, agent)
		check_object_type(this, "dcop")
		check_object_type(agent, "agent")

		table.insert(this.agents, agent)
		if not agent.id then
			agent.id = table.maxn(this.agents)
		end

		for i, r in base.ipairs(this.hardware.resources) do
			if not agent.view[i] then
				agent.view[i] = resource.new(r.type, r.tile)
				agent.view[i].status = resource.status.UNKNOWN
			end
		end
	end

	p.load = function(this)
		check_object_type(this, "dcop")

		base.print("trying to call C __dcop_laod")
		base.__dcop_load(this)
	end

	return p
end

