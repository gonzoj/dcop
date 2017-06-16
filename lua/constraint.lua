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
		if r.status == dcop.resource.status.TAKEN and r.owner == a.id then
			i = i + 1
		end
	end
	if i >= n  and i <= m then
		return 0
	elseif i < n then
		--return math.huge
		return n - i
	else
		--return math.huge
		return i - m
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
	elseif i < n then
		--return math.huge
		return n - i
	else
		--return math.huge
		return i - m
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

constraints["PREFER_FREE"] = function(param)
	local a = param.agent
	local b = param.neighbors[1]

	local n = 0

	for i in base.ipairs(a.view) do
		if a.view[i].status == dcop.resource.status.TAKEN and a.view[i].owner == a.id and
		   a.agent_view[b][i].status == dcop.resource.status.TAKEN and a.agent_view[b][i].owner == b then
			n = n + 1
		end
	end

	return n / 2
end

local function downey(A, sigma, n)
	local S = 0

	if sigma < 1 then
		if 1 <= n and n <= A then
			S = (n * A) / (A + (sigma / (2 * (n - 1))))
		elseif A <= n and n <= 2 * A - 1 then
			S = (n * A) / (sigma * (A - 1/2) + n * (1 - sigma/2))
		else
			S = A
		end
	else
		if n <= 1 and n <= A + A * sigma - sigma then
			S = (n * A * (sigma + 1)) / (sigma * (n + A - 1) + A)
		else
			S = A
		end
	end

	return S
end

constraints["DOWNEY"] = function(param)
	local a = param.agent
	local b = param.neighbors[1]
	local a_A = param.args[1]
	local a_sigma = param.args[2]
	local b_A = param.args[3]
	local b_sigma = param.args[4]

	if not a:has_conflicting_view(b) then
		return 0
	end

	local d_A = downey(a_A, a_sigma, a:occupied_resources())
	local d_B = downey(b_A, b_sigma, a:occupied_resources(b))

	if (d_A > d_B) then
		return 1 / (d_A - d_B)
	else
		return math.huge
	end
end

constraints["SPEEDUP"] = function(param)
	local agent = param.agent
	local A = param.args[1]
	local sigma = param.args[2]

	return 1 / downey(A, sigma, agent:occupied_resources())
end

constraints["AND"] = function(param)
	local c1, c2 = param.args[1], param.args[2]
	return math.max(c1.eval(c1.param), c2.eval(c2.param))
end

constraints["OR"] = function(param)
	local c1, c2 = param.args[1], param.args[2]
	return math.min(c1.eval(c1.param), c2.eval(c2.param))
end

constraints["NOP"] = function(param)
	return 0
end

function create(n, a)
	return dcop.constraint.new(n, constraints[n], a)
end

