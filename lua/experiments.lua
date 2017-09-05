dir = "../experiments/" .. os.date("%m-%d-%H:%M")

tile_size = 6
cluster_size = 6

function run(dir, tiles, load_percent, max_per_agent, algo)
	local agents = math.ceil(math.ceil(tiles * tile_size * load_percent) / max_per_agent) -- a-priori agents
	local cores = agents + 2 -- invading agent + dcop thread
	local seed = "-f " .. dir .. "/seed"
	local param = ""
	if algo == "distrm" then
		cores = cores + math.ceil(tiles * tile_size / cluster_size) + 1 -- directory services + idle thread
		seed = "-s " .. last_seed
		param = "-p -c" .. cluster_size
	end
	local cmd = string.format("./run-dcop -n%i -s\"%s\" -- -q -o -t%i -o -a%i -o -l%f -o -m%i -a %s %s %s lua/generator.lua", cores, dir, tiles, agents, load_percent, max_per_agent, algo, seed, param)
	print("running command: " .. cmd)
	return os.execute(cmd), agents
end

function get_average(d, c, n)
	local f = io.open(d .. "/sim.out", "r")
	local data = f:read("*all")
	f:close()

	local result = 0

	local stats = data:match(c .. " +| +%d+ (|.*)")

	local i = 0
	for stat in stats:gmatch("| +(%d+) ") do
		result = result + tonumber(stat)

		i = i + 1
		if i == n then
			break
		end
	end

	result = result / n

	return math.ceil(result)
end

if arg[1] then
	dir = arg[1]
	if dir:sub(-1) == '/' then
		dir = dir:sub(1, -2)
	end

	print("processing " .. dir)

	sniper = false
else
	sniper = true
end

--number_of_tiles = { 5, 10, 20, 30, 40, 50 }
number_of_tiles = { 1, 2, 3, 4, 5 }

for i, n in ipairs(number_of_tiles) do
	if sniper then
		run(dir .. "/var_dom/mgm-" .. n, n, 0.4, 4, "mgm")

		local f = io.open(dir .. "/var_dom/mgm-" .. n .. "/seed", "r")
		last_seed = tonumber(f:read("*all"))
		f:close()
		run(dir .. "/var_dom/distrm-" .. n, n, 0.4, 4, "distrm")
	end

	local agents = math.ceil(math.ceil(n * tile_size * 0.4) / 4)

	local mgm_data = get_average(dir .. "/var_dom/mgm-" .. n, "remote cache", agents)
	mgm_data = mgm_data .. ";" .. get_average(dir .. "/var_dom/mgm-" .. n, "Instructions", agents)
	local distrm_data = get_average(dir .. "/var_dom/distrm-" .. n, "remote cache", agents)
	distrm_data = distrm_data .. ";" .. get_average(dir .. "/var_dom/distrm-" .. n, "Instructions", agents)

	if not pd then
		pd = io.open(dir .. "/var_dom/plot-var_dom.csv", "w")
	end
	pd:write(string.format("%i;%s;%s\n", n, mgm_data, distrm_data))

	os.execute(string.format("cp %s/var_dom/mgm-%i/cpi-stack.png %s/plots/cpi-stack-mgm-var_dom-%i.png", dir, n, dir, n))
	os.execute(string.format("convert %s/plots/cpi-stack-mgm-var_dom-%i.png %s/plots/cpi-stack-mgm-var_dom-%i.pdf", dir, n, dir, n))
	os.execute(string.format("cp %s/var_dom/distrm-%i/cpi-stack.png %s/plots/cpi-stack-distrm-var_dom-%i.png", dir, n, dir, n))
	os.execute(string.format("convert %s/plots/cpi-stack-distrm-var_dom-%i.png %s/plots/cpi-stack-distrm-var_dom-%i.pdf", dir, n, dir, n))
end 

pd:close()
pd = nil

--number_of_agents = { 1, 5, 10, 20, 30, 40, 50 }
number_of_agents = { 1, 2, 4 }

-- TODO: fix number of agents adjusting in generator.lua

for i, n in ipairs(number_of_agents) do
	if sniper then
		local tiles = 2
		local load_percent = (n * 2) / (tiles * tile_size)

		run(dir .. "/var_ag/mgm-" .. n, tiles, load_percent, 2, "mgm")

		local f = io.open(dir .. "/var_ag/mgm-" .. n .. "/seed", "r")
		last_seed = tonumber(f:read("*all"))
		f:close()
		run(dir .. "/var_ag/distrm-" .. n, tiles, load_percent, 2, "distrm")
	end

	local mgm_data = get_average(dir .. "/var_ag/mgm-" .. n, "remote cache", n)
	mgm_data = mgm_data .. ";" .. get_average(dir .. "/var_ag/mgm-" .. n, "Instructions", n)
	local distrm_data = get_average(dir .. "/var_ag/distrm-" .. n, "remote cache", n)
	distrm_data = distrm_data .. ";" .. get_average(dir .. "/var_ag/distrm-" .. n, "Instructions", n)

	if not pd then
		pd = io.open(dir .. "/var_ag/plot-var_ag.csv", "w")
	end
	pd:write(string.format("%i;%s;%s\n", n, mgm_data, distrm_data))

	os.execute(string.format("cp %s/var_ag/mgm-%i/cpi-stack.png %s/plots/cpi-stack-mgm-var_ag-%i.png", dir, n, dir, n))
	os.execute(string.format("convert %s/plots/cpi-stack-mgm-var_ag-%i.png %s/plots/cpi-stack-mgm-var_ag-%i.pdf", dir, n, dir, n))
	os.execute(string.format("cp %s/var_ag/distrm-%i/cpi-stack.png %s/plots/cpi-stack-distrm-var_ag-%i.png", dir, n, dir, n))
	os.execute(string.format("convert %s/plots/cpi-stack-distrm-var_ag-%i.png %s/plots/cpi-stack-distrm-var_ag-%i.pdf", dir, n, dir, n))
end 

pd:close()

print("plotting with matplotlib...")
os.execute("mkdir -p " .. dir .. "/plots")
os.execute("python plot.py " .. dir .. "/")

