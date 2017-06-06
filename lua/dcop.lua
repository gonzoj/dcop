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

local function contains(t, s, f, i)
	base.assert(base.type(t) == "table")
	base.assert(base.type(f) == "function" or f == nil)
	base.assert(base.type(i) == "function" or i == nil)

	f = f or function(x, y) return x == y end
	i = i or base.ipairs

	for _, v in i(t) do
		if f(v, s) then
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
	r.owner = -1

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

	hw.add_tile = function(this, t, n)
		check_object_type(this, "hardware")
		base.assert(base.type(t) == "table", "argument 't' must be a table")
		base.assert(base.type(i) == "number" and i >= 0 or i == nil)
		
		n = n or 1

		for i = 1, n do
			this.number_of_tiles = this.number_of_tiles + 1

			for _, r in base.ipairs(t) do
				local resource = resource.new(base.tostring(r), this.number_of_tiles)
				table.insert(this.resources, resource)
			end
		end
	end

	hw.get_tile = function(this, t)
		check_object_type(this, "hardware")
		base.assert(t > 0 and t <= this.number_of_tiles, "unknown tile " .. t)

		local tile = {}
		local index
		for i, r in base.ipairs(this.resources) do
			if r.tile == t then
				table.insert(tile, r)
				if not index then
					index = i
				end
			end
		end

		return tile, index
	end

	hw.get_resource = function(this, t, r)
		check_object_type(this, "hardware")
		base.assert(t > 0 and t <= this.number_of_tiles, "unknown tile " .. t)
		base.assert(base.type(r) == "number" or base.type(r) == "string", "argument 'r' must be a number or a string")

		local tile, i = this:get_tile(t)
		if base.type(r) == "number" then
			base.assert(r > 0 and r <= table.maxn(tile), "resource " .. r .. " not on tile " .. t)

			return tile[r], i + r - 1
		else
			for j, res in base.ipairs(tile) do
				if res.type == r and res.status == resource.status.FREE then
					return tile[j], i + j - 1
				end
			end

			return nil
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

	a.id = -1

	a.constraints = {}

	a.neighbors = {}

	a.agent_view = {}
	a.view = {}

	a.add_constraint = function(this, c)
		check_object_type(this, "agent")
		check_object_type(c, "constraint")

		table.insert(this.constraints, c)
	end

	a.rate_view = function(this)
		check_object_type(this, "agent")

		local rating = 0
		for _, c in base.ipairs(this.constraints) do
			rating = rating + c.eval(c.param)
		end
		return rating
	end

	a.claim_resource = function(this, dcop, t, r)
		check_object_type(this, "agent")
		check_object_type(dcop, "dcop")
		base.assert(base.type(t) == "number", "tile argument must be a number")
		base.assert(base.type(r) == "number" or base.type(r) == "string", "resource argument must be a number or string")

		local res, i = dcop.hardware:get_resource(t, r)
		if res.status == resource.status.TAKEN then
			dcop.agents[res.owner].view[i].owner = this.id
		end
		this.view[i] = resource.new(res.type, res.tile)
		this.view[i].status = resource.status.TAKEN
		this.view[i].owner = this.id	
		res.status = resource.status.TAKEN
		res.owner = this.id
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

	c.param = {}
	c.param.agent = nil
	c.param.neighbors = {}
	c.param.args = {} 

	for _, arg in base.ipairs(a) do
		if typeof(arg) == "agent" then
			table.insert(c.param.neighbors, arg)
		else
			table.insert(c.param.args, arg)
		end
	end

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
		agent.id = table.maxn(this.agents)

		return agent.id
	end

	p.create_agents = function(this, n)
		check_object_type(this, "dcop")
		base.assert(base.type(n) == "number", "argument 'n' must be a number")

		local start
		for i = 1, n do
			local s = this:add_agent(agent.new())
			if not start then
				start = s
			end
		end

		return base.unpack(this.agents, start)
	end

	p.load = function(this)
		check_object_type(this, "dcop")

		for _, a in base.ipairs(this.agents) do
			for _, c in base.pairs(a.constraints) do
				local process_constraint
				process_constraint = function(agent, constraint)
					constraint.param.agent = agent
					local _neighbors = {}
					for _, n in base.ipairs(constraint.param.neighbors) do
						if not contains(agent.neighbors, n.id) then
							table.insert(agent.neighbors, n.id)
						end
						table.insert(_neighbors, n.id)
					end
					c.param.neighbors = _neighbors
					for _, a in base.ipairs(constraint.param.args) do
						if typeof(a) == "constraint" then
							process_constraint(agent, a)
						end
					end
				end
				process_constraint(a, c)
			end
		end

		for _, a in base.ipairs(this.agents) do
			for _, n in base.ipairs(a.neighbors) do
				if not contains(this.agents[n].neighbors, a.id) then
					table.insert(this.agents[n].neighbors, a.id)
				end
				a.agent_view[n] = {}
			end
		end

		for _, a in base.ipairs(this.agents) do
			for i, r in base.ipairs(this.hardware.resources) do
				if not a.view[i] then
					a.view[i] = resource.new(r.type, r.tile)
					a.view[i].status = r.status
					a.view[i].owner = r.owner
				end
				for _, j in base.ipairs(a.neighbors) do
					a.agent_view[j][i] = resource.new(r.type, r.tile)
					a.agent_view[j][i].status = resource.status.UNKNOWN
				end
			end
		end

		if base.__dcop_load then
			base.__dcop_load(this)
		else
			base.print("error: __dcop_load undefined")
		end
	end

	return p
end

