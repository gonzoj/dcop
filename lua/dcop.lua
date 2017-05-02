local base = require("_G")
local math = require("math")
local table = require("table")

module("dcop")

local function check_object_type(o, t)
	base.assert(base.type(o) == "table" and base.getmetatable(o) and base.getmetatable(o).__object_type == t, "'" .. base.tostring(o) .. "' is not of type '" .. t .. "'")
end

resource = {}

resource.status = {
	UNKNOWN = -1,
	FREE = 0
}

resource.new = function(t, tile)
	base.assert(base.type(t) == "string", "resource.type must be a string")
	base.assert(base.type(tile) == "number" and tile > 0, "resource.tile must be a number > 0")

	local r = {}

	local mt = {}
	base.setmetatable(r, mt)

	mt.__object_type = "resource"

	r.type = t
	r.status = status.UNKNOWN
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

		base.assert(r.tile <= this.number_of_tiles, "unknown tile " .. r.tile)

		table.insert(this.resources, r)
	end
	
	hw.add_tile = function(this, t)
		check_object_type(this, "hardware")
		base.assert(base.type(t) == "table", "argument 't' must be a table")

		this.number_of_tiles = this.number_of_tiles + 1

		for _, r in base.ipairs(t) do
			local resource = resource_new(base.tostring(r), this.number_of_tiles)
			this:add_resource(resource)
		end
	end

	return hw
end

agent = {}

agent.new = function()
	local a = {}

	local mt = {}
	base.setmetatable(a, mt)

	mt.__object_type = "agent"

	local neighbors = {}
	local constraints = {}
	local add_constraint = function(this, c)
		check_object_type(this, "agent")
		check_object_type(c, "constraint")

		table.insert(this.constraints, c)
	end
	local view = {}
	local rate_view = function(this)
		check_object_type(this, "agent")

		local rating = 0
		for _, c in base.ipairs(this.constraints) do
			rating = rating + c(this.view)
		end
		return rating
	end

	return a
end

constraint = {}

constraint.new = function(n, f)
	base.assert(base.type(n) == "string", "constraint.name must be a string")
	base.assert(base.type(f) == "function", "constraint.rate must be a function")

	local c = {}

	local mt = {}
	base.setmetatable(c, mt)

	mt.__object_type = "constraint"

	c.name = n
	c.rate = f

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
	end

	return p
end
