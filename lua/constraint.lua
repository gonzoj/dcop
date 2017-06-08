local base = require("_G")
local math = require("math")

local dcop = require("dcop")

module("constraint")

local constraints = {}

constraints["TILE"] = function(param)
	local a = param.agent
	local tile
	for _, r in base.ipairs(a.view) do
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

constraints["NEC_RE"] = function(param)
	local a = param.agent
	local n = param.args[1]
	local m = param.args[2]
	local i =  0
	for _, r in base.ipairs(a.view) do
		if r.status == dcop.resource.status.TAKEN then
		end
		if r.status == dcop.resource.status.TAKEN and r.owner == a.id then
			i = i + 1
		end
	end
	if i >= n  and i <= m then
		return 0
	else
		return math.huge
	end
end

constraints["TYPE"] = function(param)
	local a = param.agent
	local t = param.args[1]
	local n = param.args[2]
	local m = param.args[3]
	local i =  0
	for _, r in base.ipairs(a.view) do
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

constraints["SHARE"] = function(param)
	local a = param.agent
	local b = param.neighbors[1]
	for i in base.ipairs(a.view) do
		if a.view[i].status == dcop.resource.status.TAKEN and a.view[i].owner == a.id and
		   a.agent_view[b][i].status == dcop.resource.status.TAKEN and a.agent_view[b][i].owner == b then
			return math.huge
		end
	end
	
	return 0
end

function create(n, a)
	return dcop.constraint.new(n, constraints[n], a)
end

