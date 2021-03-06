#define _GNU_SOURCE

#include <getopt.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include "agent.h"
#include "algorithm.h"
#include "console.h"
#include "constraint.h"
#include "dcop.h"
#include "distrm.h"
#include "hardware.h"
#include "list.h"
#include "mgm.h"
#include "native.h"
#include "resource.h"

#include <sim_api.h>

static LIST_HEAD(algorithms);

bool skip_lua = true;

pthread_t main_tid = 0;

static time_t r_seed = 0;
static char *r_seedfile = NULL;

static char *algorithm = NULL;
static char *spec = NULL;
static int algorithm_argc = 1;
static char **algorithm_argv = NULL;
static int spec_argc = 0;
static char **spec_argv = NULL;

static algorithm_t *algo = NULL;

static dcop_t *dcop = NULL;

static char *tlm_stats_file = NULL;

void dcop_register_algorithm(algorithm_t *a) {
	list_add_tail(&a->_l, &algorithms);
}

static algorithm_t * dcop_get_algorithm(const char *name) {
	for_each_entry(algorithm_t, a, &algorithms) {
		if (!strcmp(a->name, name)) return a;
	}

	return NULL;
}

static void dcop_init_algorithms() {
	mgm_register();
	distrm_register();
}

agent_t * dcop_get_agent(dcop_t *dcop, int id) {
	for_each_entry(agent_t, a, &dcop->agents) {
		if (a->id == id) return a;
	}

	return NULL;
}

void dcop_refresh_hardware(dcop_t *dcop) {
	for_each_entry(resource_t, r, &dcop->hardware->view->resources) {
		resource_refresh(dcop->L, r);
	}
}

void dcop_refresh_agents(dcop_t *dcop) {
	for_each_entry(agent_t, a, &dcop->agents) {
		agent_refresh(a);
	}
}

void dcop_refresh(dcop_t *dcop) {
	dcop_refresh_hardware(dcop);
	dcop_refresh_agents(dcop);
}

void dcop_merge_view(dcop_t *dcop) {
	view_clear(dcop->hardware->view);
	bool clear = true;

	for_each_entry(agent_t, a, &dcop->agents) {
		if (!clear && !view_compare(dcop->hardware->view, a->view)) {
			print_warning("inconsistent hardware view\n");
		}

		view_merge(dcop->hardware->view, a->view, true);
		clear = false;
	}
}

static void dcop_free(dcop_t *dcop) {
	if (dcop) {
		if (dcop->L) {
			lua_close(dcop->L);
		}

		if (dcop->hardware) {
			hardware_free(dcop->hardware);
		}

		for_each_entry_safe(agent_t, a, _a, &dcop->agents) {
			list_del(&a->_l);
			agent_free(a);
		}

		pthread_mutex_destroy(&dcop->mt);
		pthread_cond_destroy(&dcop->cv);

		free(dcop);
	}
}

static int __dcop_load(lua_State *L) {
	lua_getglobal(L, "__this");
	dcop_t *dcop = lua_touserdata(L, -1);
	lua_pop(L, 1);
	if (!dcop) {
		print_error("failed to retrieve '__this' userdata\n");

		return 0;
	}

	dcop->L = L;

	print("loading hardware...\n");
	dcop->hardware = (hardware_t *) calloc(1, sizeof(hardware_t));
	lua_getfield(L, -1, "hardware");
	hardware_load(dcop->L, dcop->hardware);

	print("loading agents...\n");
	dcop->number_of_agents = 0;
	lua_getfield(L, -1, "agents");
	int t = lua_gettop(L);
	lua_pushnil(L);
	while (lua_next(L, t)) {
		agent_t *a = agent_new();
		agent_load(dcop, a);
		print("loading agent %i\n", a->id);

		list_add_tail(&a->_l, &dcop->agents);

		dcop->number_of_agents++;
	}

	lua_pushnil(L);
	for_each_entry(agent_t, a, &dcop->agents) {
		lua_next(L, t);

		print("loading neighbors for agent %i\n", a->id);
		agent_load_neighbors(a);

		lua_pop(L, 1);
	}
	
	lua_pop(L, 1);

	return 0;
}

static int __dcop_load_agent(lua_State *L) {
	lua_getglobal(L, "__this");
	agent_t *agent = (agent_t *) lua_touserdata(L, -1);
	lua_pop(L, 1);
	if (!agent) {
		print_error("failed to retrieve '__this' userdata\n");

		return 0;
	}

	agent->L = L;

	lua_getfield(L, -1, "agents");
	lua_pushnumber(L, agent->id);
	lua_gettable(L, -2);

	print("loading view of agent %i\n", agent->id);
	agent_load_view(agent);

	agent_load_agent_view(agent);

	print("loading constraints of agent %i...\n", agent->id);
	agent_load_constraints(agent);

	lua_setglobal(L, "__agent");

	lua_pop(L, 1);

	return 0;
}

static lua_State * dcop_create_lua_state(void *object, const char *file, int (*load)(lua_State *), int argc, char **argv) {
	lua_State *L = luaL_newstate();
	if (!L) {
		return NULL;
	}
	luaL_openlibs(L);

	lua_pushnumber(L, r_seed);
	lua_setglobal(L, "__dcop_seed");

	lua_pushlightuserdata(L, object);
	lua_setglobal(L, "__this");

	lua_newtable(L);
	lua_setglobal(L, "__resources");

	lua_register(L, "__dcop_load", load);

	lua_newtable(L);
	for (int i = 0; i < argc; i++) {
		lua_pushnumber(L, i + 1);
		lua_pushstring(L, argv[i]);
		lua_settable(L, -3);
	}
	lua_setglobal(L, "arg");

	lua_getglobal(L, "package");
	lua_getfield(L, -1, "path");
	char *path = strdup(lua_tostring(L, -1));
	path = (char *) realloc(path, strlen(path) + 1 + strlen(DCOP_ROOT_DIR) + strlen("/lua/?.lua") + 1);
	strcat(path, ";");
	strcat(path, DCOP_ROOT_DIR);
	strcat(path, "/lua/?.lua");
	lua_pushstring(L, path);
	lua_setfield(L, -3, "path");
	lua_pop(L, 2);
	free(path);

	if (luaL_dofile(L, file)) {
		print_error("failed to load file '%s': %s\n", file, lua_tostring(L, -1));
		lua_pop(L, 1);

		lua_close(L);
		
		return NULL;
	}

	return L;
}

static struct dcop * dcop_load(const char *file, int argc, char **argv) {
	dcop_t *dcop = (dcop_t *) calloc(1, sizeof(dcop_t));

	INIT_LIST_HEAD(&dcop->agents);

	if (!dcop_create_lua_state(dcop, file, __dcop_load, argc, argv)) {
		dcop->L = NULL;
		dcop_free(dcop);

		return NULL;
	}

	for_each_entry(agent_t, a, &dcop->agents) {
		if (!dcop_create_lua_state(a, file, __dcop_load_agent, argc, argv)) {
			a->L = NULL;

			dcop_free(dcop);

			return NULL;
		} else if (!a->has_lua_constraints) {
			lua_close(a->L);

			a->L = NULL;
		}


	}

	pthread_mutex_init(&dcop->mt, NULL);
	pthread_cond_init(&dcop->cv, NULL);
	dcop->ready = 0;

	return dcop;
}

int dcop_get_number_of_cores() {
	return sysconf(_SC_NPROCESSORS_ONLN);
}

void * dcop_malloc_aligned(size_t size) {
	void *p;
	posix_memalign(&p, sysconf(_SC_PAGE_SIZE), size);
	memset(p, 0, size);

	return p;
}

void dcop_start_ROI(dcop_t *dcop) {
	pthread_mutex_lock(&dcop->mt);

	if (++dcop->ready == dcop->number_of_agents) {
		SimRoiStart();

		pthread_cond_broadcast(&dcop->cv);
	} else {
		pthread_cond_wait(&dcop->cv, &dcop->mt);
	}

	pthread_mutex_unlock(&dcop->mt);
}

void dcop_stop_ROI(dcop_t *dcop) {
	pthread_mutex_lock(&dcop->mt);

	if (dcop->ready == 0) {
		pthread_mutex_unlock(&dcop->mt);

		return;
	}

	if (--dcop->ready == 0) {
		SimRoiEnd();

		pthread_cond_broadcast(&dcop->cv);
	} else {
		pthread_cond_wait(&dcop->cv, &dcop->mt);
	}

	pthread_mutex_unlock(&dcop->mt);
}

static void dcop_dump_tlm_stats(dcop_t *dcop) {
	if (!tlm_stats_file || !use_tlm) {
		return;
	}

	print("dumping TLM statistics\n");

	FILE *f = fopen(tlm_stats_file, "w");

	for_each_entry(agent_t, a, &dcop->agents) {
		fprintf(f, "%i %lu %lu\n", a->id, a->tlm->max_used, a->bytes_sent);
	}

	fclose(f);
}

static void usage() {
	printf("\n");
	printf("usage:\n");
	printf("\n");
	printf("dcop [OPTIONS] SPECIFICATION\n");
	printf("\n");
	printf("OPTIONS:\n");
	printf("	--help, -h\n");
	printf("		print usage description and exit\n");
	printf("\n");
	printf("	--algorithm NAME, -a NAME\n");
	printf("		use algorithm NAME to solve SPECIFICATION\n");
	printf("\n");
	printf("	--logfile FILE, -l FILE\n");
	printf("		log program output to FILE\n");
	printf("\n");
	printf("	--debug , -d\n");
	printf("		enable debug output\n");
	printf("\n");
	printf("	--param PARAMTER, -p PARAMETER\n");
	printf("		pass PARAMTER to algorithm\n");
	printf("\n");
	printf("	--option OPTION, -o OPTION\n");
	printf("		pass OPTION to SPECIFICATION\n");
	printf("\n");
	printf("	--seedfile FILE, -f FILE\n");
	printf("		write seed to FILE\n");
	printf("\n");
	printf("	--seed INTEGER, -s INTEGER\n");
	printf("		use INTEGER for seeding RNG\n");
	printf("\n");
	printf("	--precise , -e\n");
	printf("		do not skip lua callbacks in cache-only mode\n");
	printf("\n");
	printf("	--shared , -m\n");
	printf("		do not use TLM for agents but shared memory instead\n");
	printf("\n");
	printf("	--quiet , -q\n");
	printf("		run algorithm in quiet mode (console is suppressed)\n");
	printf("\n");
	printf("	--tlmstats FILE, -t FILE\n");
	printf("		dump tlm statistics to FILE\n");
	printf("\n");

	printf("algorithms:\n");
	printf("\n");
	for_each_entry(algorithm_t, a, &algorithms) {
		printf("* %s:\n", a->name);
		a->usage();
	}
	printf("\n");
}

static int parse_arguments(int argc, char **argv) {
	struct option long_options[] = {
		{ "help", no_argument, NULL, 'h' },
		{ "algorithm", required_argument, NULL, 'a' },
		{ "logfile", required_argument, NULL, 'l' },
		{ "debug", no_argument, NULL, 'd' },
		{ "param", required_argument, NULL, 'p' },
		{ "option", required_argument, NULL, 'o' },
		{ "seedfile", required_argument, NULL, 'f' },
		{ "seed", required_argument, NULL, 's' },
		{ "precise", no_argument, NULL, 'e' },
		{ "shared", no_argument, NULL, 'm' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "tlmstats", required_argument, NULL, 't'},
		{ 0 }
	};

	while (true) {
		int result = getopt_long(argc, argv, "ha:l:dp:o:f:s:emqt:", long_options, NULL);
		if (result == -1) {
			break;
		}

		switch (result) {
			case 'h':
				usage();
				exit(EXIT_SUCCESS);

			case 'a':
				algorithm = strdup(optarg);
				break;

			case 'l':
				printf("logging to %s\n", optarg);
				logfile = strdup(optarg);
				break;

			case 'd':
				debug = true;
				break;

			case 'p':
				algorithm_argv = realloc(algorithm_argv, ++algorithm_argc * sizeof(char *));
				algorithm_argv[algorithm_argc - 1] = strdup(optarg);
				break;

			case 'o':
				spec_argv = realloc(spec_argv, ++spec_argc * sizeof(char *));
				spec_argv[spec_argc - 1] = strdup(optarg);
				break;

			case 'f':
				r_seedfile = strdup(optarg);
				break;

			case 's':
				r_seed = strtol(optarg, NULL, 0);
				if (r_seed <= 0) {
					printf("invalid seed given\n");
					r_seed = 0;
				}
				break;

			case 'e':
				printf("running in precise mode\n");
				skip_lua = false;
				break;

			case 'm':
				printf("running in shared memory mode\n");
				use_tlm = false;
				break;

			case 'q':
				printf("running algorithm in quiet mode\n");
				silent = true;
				break;

			case 't':
				printf("dumping tlm stats to %s\n", optarg);
				tlm_stats_file = strdup(optarg);
				break;

			case '?':
			case ':':
			default:
				printf("failed to parse program arguments.\n");
				goto error;
		}
	}

	if (argc - optind < 1) {
		printf("no specification file given.\n");
		goto error;
	} else {
		spec = strdup(argv[optind]);
	}

	return 0;

error:
	if (logfile) {
		free(logfile);
	}
	if (algorithm) {
		free(algorithm);
	}
	if (algorithm_argv) {
		for (int i = 0; i < algorithm_argc; i++) {
			free(algorithm_argv[i]);
		}
		free(algorithm_argv);
	}
	if (spec_argv) {
		for (int i = 0; i < spec_argc; i++) {
			free(spec_argv[i]);
		}
		free(spec_argv);
	}

	return -1;
}

void sigint_handler(int signal) {
	algo->kill(dcop);
}

int main(int argc, char **argv) {
	//SimSetInstrumentMode(SIM_OPT_INSTRUMENT_FASTFORWARD);

	main_tid = pthread_self();

	dcop_init_algorithms();

	if (parse_arguments(argc, argv)) {
		usage();
		exit(EXIT_FAILURE);
	}

	int status = EXIT_SUCCESS;

	if (!algorithm) {
		algorithm = strdup("mgm");
	}

	if (!algorithm_argv) {
		algorithm_argv = malloc(sizeof(char *));
	}
	algorithm_argv[0] = strdup(algorithm);
	console_init();

	register_native_constraints();

	print("number of cores available: %i\n", dcop_get_number_of_cores());

	print("pinning main thread to core 0\n");
	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(0, &cpuset);
	if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset)) {
		print_warning("failed to set core affinity for main thread\n");
	}

	if (r_seed == 0) {
		print("creating seed...\n");
		r_seed = time(NULL);
	}
	if (r_seedfile) {
		FILE *f = fopen(r_seedfile, "w");
		if (f) {
			print("dumping seed to file '%s'\n", r_seedfile);
			fprintf(f, "%li\n", r_seed);
			fclose(f);
		} else {
			print_warning("failed to open seed file '%s'\n", r_seedfile);
		}
	}
	print("using seed %lX\n", r_seed);

	print("loading dcop specification from '%s'\n", spec);
	dcop = dcop_load(spec, spec_argc, spec_argv);
	if (!dcop) {
		print_error("failed to load dcop specification\n");
		status = EXIT_FAILURE;
		goto cleanup;
	}
	print("completed loading '%s'\n", spec);

	if (dcop->number_of_agents > dcop_get_number_of_cores()) {
		print_warning("number of agents exceeds available physical cores\n");
	}

	print("\ninital resource assignment:\n");
	view_dump(dcop->hardware->view);
	print("\n");

	algo = dcop_get_algorithm(algorithm);
	if (!algo) {
		print_error("unknown algorithm '%s'\n", algorithm);
		status = EXIT_FAILURE;
		goto cleanup;
	}

	signal(SIGINT, sigint_handler);

	print("initialize algorithm '%s'\n\n", algo->name);
	algo->init(dcop, algorithm_argc, algorithm_argv);

	//SimRoiStart();

	print("starting algorithm '%s'\n\n", algo->name);
	algo->run(dcop);

	algo->cleanup(dcop);
	print("algorithm '%s' finished\n", algo->name);

	//SimRoiEnd();

	print("\nprevious resource assignment (%lX):\n", r_seed);
	view_dump(dcop->hardware->view);
	print("\n");

	dcop_refresh(dcop);

	dcop_merge_view(dcop);

	print("final resource assignment:\n");
	view_dump(dcop->hardware->view);

	dcop_dump_tlm_stats(dcop);

	dcop_free(dcop);

	free_native_constraints();

cleanup:
	console_cleanup();

	free(algorithm);
	free(spec);
	for (int i = 0; i < algorithm_argc; i++) {
		free(algorithm_argv[i]);
	}
	free(algorithm_argv);
	if (spec_argv) {
		for (int i = 0; i < spec_argc; i++) {
			free(spec_argv[i]);
		}
		free(spec_argv);
	}
	if (r_seedfile) {
		free(r_seedfile);
	}

	exit(status);
}

