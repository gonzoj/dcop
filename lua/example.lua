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

local number_of_agents = tonumber(arg[1]) or 3

local problem, agent1, agent2, agent3 = dcop.new(dcop.hardware.new{ tile, tile }, number_of_agents)

agent1:claim_resource(problem, 1, "REGULAR")
agent1:claim_resource(problem, 1, "STREAM")
agent2:claim_resource(problem, 2, "REGULAR")
agent2:claim_resource(problem, 2, "REGULAR")
agent2:claim_resource(problem, 2, "REGULAR")
agent2:claim_resource(problem, 2, "REGULAR")

agent1:add_constraint(constraint.create("TILE"))
agent1:add_constraint(constraint.create("TYPE", { "STREAM", 1, 1 }))
agent1:add_constraint(constraint.create("TYPE", { "REGULAR", 1, 1 }))
agent1:add_constraint(constraint.create("PREFER_FREE", { agent2 }))
agent1:add_constraint(constraint.create("SHARE", { agent3 }))

agent2:add_constraint(constraint.create("NEC_RE", { 5, 5 }))
agent2:add_constraint(constraint.create("TYPE", { "RECONFIG", 1, 1 }))
agent2:add_constraint(constraint.create("PREFER_FREE", { agent1 }))
agent2:add_constraint(constraint.create("SHARE", { agent3 }))

agent3:add_constraint(constraint.create("TILE"))
agent3:add_constraint(constraint.create("TYPE", { "REGULAR", 2, 2 }))
agent3:add_constraint(constraint.create("TYPE", { "STREAM", 1, 1 }))
agent3:add_constraint(constraint.create("PREFER_FREE", { agent1 }))
agent3:add_constraint(constraint.create("PREFER_FREE", { agent2 }))

problem:load()

