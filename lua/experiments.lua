dir = "../experiments/" .. os.date("%m-%d-%H:%M")
tile_size = 6
cluster_size = 2

function run(dir, tiles, load_percent, max_per_agent, algo)
	local agents = math.ceil(math.ceil(tiles * tile_size * load_percent) / max_per_agent) -- a-priori agents
	local cores = agents + 2 -- invading agent + dcop thread
	local seed = "-f " .. dir .. "/seed"
	if algo == "distrm" then
		cores = cores + math.ceil(tiles * tile_size / cluster_size) + 1 -- directory services + idle thread
		seed = "-s " .. last_seed
	end
	local cmd = string.format("./run-dcop -n%i -s\"%s\" -- -q -o -t%i -o -a%i -o -l%f -o -m%i -a %s %s lua/generator.lua", cores, dir, tiles, agents, load_percent, max_per_agent, algo, seed)
	print("running command: " .. cmd)
	return os.execute(cmd)
end

--number_of_tiles = { 5, 10, 20, 30, 40, 50 }
number_of_tiles = { 1, 2 }

for i, n in ipairs(number_of_tiles) do
	run(dir .. "/var_dom/mgm-" .. n, n, 0.4, 4, "mgm")
	local f = io.open(dir .. "/var_dom/mgm-" .. n .. "/seed", "r")
	last_seed = tonumber(f:read("*all"))
	f:close()
	run(dir .. "/var_dom/distrm-" .. n, n, 0.4, 4, "distrm")
end

--number_of_agents = { 1, 5, 10, 20, 30, 40, 50 }
number_of_agents = { 2, 3 }

for i, n in ipairs(number_of_agents) do
	local tiles = 2
	local load_percent = (n * 2) / (tiles * tile_size)
	run(dir .. "/var_ag/mgm-" .. n, tiles, load_percent, 2, "mgm")
	local f = io.open(dir .. "/var_ag/mgm-" .. n .. "/seed", "r")
	last_seed = tonumber(f:read("*all"))
	f:close()
	run(dir .. "/var_ag/distrm-" .. n, tiles, load_percent, 2, "distrm")
end

