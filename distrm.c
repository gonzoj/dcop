#include <getopt.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "agent.h"
#include "algorithm.h"
#include "console.h"
#include "dcop.h"
#include "list.h"
#include "resource.h"
#include "view.h"

static algorithm_t _distrm;

static int cluster_size = 2;

static void distrm_usage() {
	printf("\n");
	printf("OPTIONS:\n");
	printf("	--cluster SIZE, -c SIZE\n");
	printf("		size of core clusters on NoC\n");
	printf("\n");
}

static int parse_arguments(int argc, char **argv) {
	struct option long_options[] = {
		{ "cluster", required_argument, NULL, 'c' },
		{ 0 }
	};

	optind = 1;

	while (true) {
		int result = getopt_long(argc, argv, "c:", long_options, NULL);
		if (result == -1) {
			break;
		}

		int size;

		switch (result) {
			case 'c':
				size = (int) strtol(optarg, NULL, 10);
				if (size <= 0) {
					print_error("distrm: invalid cluster size given\n");
					print("distrm: using default cluster size %i\n", cluster_size);
				} else {
					cluster_size = size;
					print("distrm: cluster size set to %i\n", size);
				}
				break;

			case '?':
			case ':':
			default:
				print_error("distrm: failed to parse algorithm paramters\n");
				return -1;
		}
	}

	return 0;
}

static void distrm_init(dcop_t *dcop, int argc, char **argv) {
	parse_arguments(argc, argv);
}

static void distrm_cleanup(dcop_t *dcop) {
}

static void distrm_run(dcop_t *dcop) {
}

static void distrm_kill(dcop_t *dcop) {
}

void distrm_register() {
	_distrm = algorithm_new("distrm", distrm_init, distrm_cleanup, distrm_run, distrm_kill, distrm_usage);
	dcop_register_algorithm(&_distrm);
}

