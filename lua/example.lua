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

hardware.add_tile(tile)
hardware.add_tile(tile)
hardware.add_tile(tile)
hardware.add_tile(tile)

local problem = dcop.new(hardware)

local agent1 = dcop.agent.new()
local agent2 = dcop.agent.new()
local agent3 = dcop.agent.new()
local agent4 = dcop.agent.new()
local agent5 = dcop.agent.new()
problem.add_agent(agent1)
problem.add_agent(agent2)
problem.add_agent(agent3)
problem.add_agent(agent4)
problem.add_agent(agent5)


