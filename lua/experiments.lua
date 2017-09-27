dir = "../extern/" .. os.date("%m-%d-%H:%M")

tile_size = 6
cluster_size = 12
region_size = 5

max_distance = 20
max_tiles = 2

function strtime(t)
	local h, m, s
	h = math.floor(t / 3600)
	m = math.floor((t - h * 3600) / 60)
	s = math.floor(t - h * 3600 - m * 60)
	return string.format("%02i:%02i:%02i", h, m, s)
end

function run(dir, tiles, load_percent, max_per_agent, algo)
	local agents = math.ceil(math.ceil(tiles * tile_size * load_percent) / max_per_agent) -- a-priori agents
	local cores = agents + 2 -- invading agent + dcop thread
	local seed = "-f " .. dir .. "/seed"
	local param = "-p -d" .. max_distance .. " -p -p" .. max_tiles
	if algo == "distrm" then
		cores = cores + math.ceil(tiles * tile_size / cluster_size) + 1 -- directory services + idle thread
		seed = "-s " .. last_seed
		param = "-p -c" .. cluster_size .. " -p -r" .. region_size
	end
	local cmd = string.format("./run-dcop -n%i -s\"%s\" -- -q -o -t%i -o -a%i -o -l%f -o -m%i -a %s %s %s -l%s -t%s lua/generator.lua", cores, dir, tiles, agents, load_percent, max_per_agent, algo, seed, param, dir .. "/dcop.log", dir .. "/tlm.stats")
	repeat
		print(os.date() .. ": running command: " .. cmd)
		local start = os.time()
		local result = os.execute(cmd)
		if result == 0 then
			local time = os.difftime(os.time(), start)
			local f = io.open(dir .. "/run", "w")
			if not f then
				result = -1
			else
				f:write(string.format("%i %s %s\n", time, strtime(time), cmd))
				f:close()
			end
		end

		if result ~= 0 then
			local f = io.open(dir .. "/../fail", "a")
			f:write(string.format("%s: failed to run %s\n", os.date(), cmd))
			f:close()
		end
	until result == 0
	return agents + 1
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

function get_tlm_stats(d)
	local n = 0
	local tlm_max = 0
	local sent = 0

	local f = io.open(d .. "/tlm.stats", "r")

	if (f) then
		for line in f:lines() do
			n = n + 1
			tlm_max = tlm_max + tonumber(line:match("^%d+ (%d+)"))
			sent = sent + tonumber(line:match(" (%d+)$"))
		end

		f:close()
	else
		return "0;0"
	end

	return tostring(math.ceil(tlm_max / n)) .. ";" .. tostring(math.ceil(sent / n))
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

os.execute("mkdir -p " .. dir .. "/plots")
---[[
number_of_tiles = { 1, 2, 3, 4, 5, 6, 7, 8 }
--number_of_tiles = { 1, 2, 3 }

system_load = 0.4
cores_per_agent = 4

for i, n in ipairs(number_of_tiles) do
	if sniper then
		run(dir .. "/var_dom/mgm-" .. n, n, system_load, cores_per_agent, "mgm")

		local f = io.open(dir .. "/var_dom/mgm-" .. n .. "/seed", "r")
		last_seed = tonumber(f:read("*all"))
		f:close()
		run(dir .. "/var_dom/distrm-" .. n, n, system_load, cores_per_agent, "distrm")
	end

	local agents = math.ceil(math.ceil(n * tile_size * system_load) / cores_per_agent) + 1

	local mgm_data = get_average(dir .. "/var_dom/mgm-" .. n, "remote cache", agents)
	mgm_data = mgm_data .. ";" .. get_average(dir .. "/var_dom/mgm-" .. n, "Instructions", agents)
	mgm_data = mgm_data .. ";" .. get_tlm_stats(dir .. "/var_dom/mgm-" .. n)
	mgm_data = mgm_data .. ";" .. get_average(dir .. "/var_dom/mgm-" .. n, "Time %(ns%)", agents)

	local distrm_data = get_average(dir .. "/var_dom/distrm-" .. n, "remote cache", agents)
	distrm_data = distrm_data .. ";" .. get_average(dir .. "/var_dom/distrm-" .. n, "Instructions", agents)
	distrm_data = distrm_data .. ";" .. get_tlm_stats(dir .. "/var_dom/distrm-" .. n)
	distrm_data = distrm_data .. ";" .. get_average(dir .. "/var_dom/distrm-" .. n, "Time %(ns%)", agents)

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

number_of_agents = { 1, 2, 3, 4, 5, 6, 7, 8, 9 }
--number_of_agents = { 1, 2, 3 }
-- TODO: fix number of agents adjusting in generator.lua

tiles = 3
cores_per_agent = 2

for i, n in ipairs(number_of_agents) do
	if sniper then
		local load_percent = (n * cores_per_agent) / (tiles * tile_size)

		while load_percent > 1.0 do
			tiles = tiles + 1
			load_percent = (n * cores_per_agent) / (tiles * tile_size)
			print("warning: adjusting number of tiles to " .. tiles .. " with load of " .. load_percent)
		end

		while 2 * n > load_percent * tiles * tile_size do
			tiles = tiles + 1
			load_percent = (n * cores_per_agent) / (tiles * tile_size)
			print("warning: adjusting number of tiles to " .. tiles .. " with load of " .. load_percent)
		end

		run(dir .. "/var_ag/mgm-" .. n, tiles, load_percent, cores_per_agent, "mgm")

		local f = io.open(dir .. "/var_ag/mgm-" .. n .. "/seed", "r")
		last_seed = tonumber(f:read("*all"))
		f:close()
		run(dir .. "/var_ag/distrm-" .. n, tiles, load_percent, cores_per_agent, "distrm")
	end

	local mgm_data = get_average(dir .. "/var_ag/mgm-" .. n, "remote cache", n + 1)
	mgm_data = mgm_data .. ";" .. get_average(dir .. "/var_ag/mgm-" .. n, "Instructions", n + 1)
	mgm_data = mgm_data .. ";" .. get_tlm_stats(dir .. "/var_ag/mgm-" .. n)
	mgm_data = mgm_data .. ";" .. get_average(dir .. "/var_ag/mgm-" .. n, "Time %(ns%)", n + 1)

	local distrm_data = get_average(dir .. "/var_ag/distrm-" .. n, "remote cache", n + 1)
	distrm_data = distrm_data .. ";" .. get_average(dir .. "/var_ag/distrm-" .. n, "Instructions", n + 1)
	distrm_data = distrm_data .. ";" .. get_tlm_stats(dir .. "/var_ag/distrm-" .. n)
	distrm_data = distrm_data .. ";" .. get_average(dir .. "/var_ag/distrm-" .. n, "Time %(ns%)", n + 1)

	if not pd then
		pd = io.open(dir .. "/var_ag/plot-var_ag.csv", "w")
	end
	pd:write(string.format("%i;%s;%s\n", n + 1, mgm_data, distrm_data))

	os.execute(string.format("cp %s/var_ag/mgm-%i/cpi-stack.png %s/plots/cpi-stack-mgm-var_ag-%i.png", dir, n, dir, n))
	os.execute(string.format("convert %s/plots/cpi-stack-mgm-var_ag-%i.png %s/plots/cpi-stack-mgm-var_ag-%i.pdf", dir, n, dir, n))
	os.execute(string.format("cp %s/var_ag/distrm-%i/cpi-stack.png %s/plots/cpi-stack-distrm-var_ag-%i.png", dir, n, dir, n))
	os.execute(string.format("convert %s/plots/cpi-stack-distrm-var_ag-%i.png %s/plots/cpi-stack-distrm-var_ag-%i.pdf", dir, n, dir, n))
end 

pd:close()
pd = nil
--]]
---[[
number_of_tiles = { 1, 2, 3, 4, 5, 6, 7, 8 }

system_load = 1.0
cores_per_agent = 4

for i, n in ipairs(number_of_tiles) do
	if sniper then
		run(dir .. "/var_hyb/mgm-" .. n, n, system_load, cores_per_agent, "mgm")

		local f = io.open(dir .. "/var_hyb/mgm-" .. n .. "/seed", "r")
		last_seed = tonumber(f:read("*all"))
		f:close()
		run(dir .. "/var_hyb/distrm-" .. n, n, system_load, cores_per_agent, "distrm")
	end

	local agents = math.ceil(math.ceil(n * tile_size * system_load) / cores_per_agent) + 1

	local mgm_data = get_average(dir .. "/var_hyb/mgm-" .. n, "remote cache", agents)
	mgm_data = mgm_data .. ";" .. get_average(dir .. "/var_hyb/mgm-" .. n, "Instructions", agents)
	mgm_data = mgm_data .. ";" .. get_tlm_stats(dir .. "/var_hyb/mgm-" .. n)
	mgm_data = mgm_data .. ";" .. get_average(dir .. "/var_hyb/mgm-" .. n, "Time %(ns%)", agents)

	local distrm_data = get_average(dir .. "/var_hyb/distrm-" .. n, "remote cache", agents)
	distrm_data = distrm_data .. ";" .. get_average(dir .. "/var_hyb/distrm-" .. n, "Instructions", agents)
	distrm_data = distrm_data .. ";" .. get_tlm_stats(dir .. "/var_hyb/distrm-" .. n)
	distrm_data = distrm_data .. ";" .. get_average(dir .. "/var_hyb/distrm-" .. n, "Time %(ns%)", agents)

	if not pd then
		pd = io.open(dir .. "/var_hyb/plot-var_hyb.csv", "w")
	end
	pd:write(string.format("%i;%s;%s\n", n, mgm_data, distrm_data))

	os.execute(string.format("cp %s/var_hyb/mgm-%i/cpi-stack.png %s/plots/cpi-stack-mgm-var_hyb-%i.png", dir, n, dir, n))
	os.execute(string.format("convert %s/plots/cpi-stack-mgm-var_hyb-%i.png %s/plots/cpi-stack-mgm-var_hyb-%i.pdf", dir, n, dir, n))
	os.execute(string.format("cp %s/var_hyb/distrm-%i/cpi-stack.png %s/plots/cpi-stack-distrm-var_hyb-%i.png", dir, n, dir, n))
	os.execute(string.format("convert %s/plots/cpi-stack-distrm-var_hyb-%i.png %s/plots/cpi-stack-distrm-var_hyb-%i.pdf", dir, n, dir, n))
end 

pd:close()
pd = nil
--]]
print("plotting with matplotlib...")
os.execute("python plot.py " .. dir .. "/")

