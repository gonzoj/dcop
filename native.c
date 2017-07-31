#include <math.h>
#include <string.h>

#include "agent.h"
#include "constraint.h"
#include "list.h"
#include "resource.h"

double tile(constraint_t *c) {
	agent_t *a = c->param.agent;

	int tile = -1;
	for_each_entry(resource_t, r, &a->view->resources) {
		if (agent_is_owner(a, r)) {
			if (tile == -1) {
				tile = r->tile;
			} else if (tile != r->tile) {
				return INFINITY;
			}
		}
	}

	return 0;
}

double nec_re(constraint_t *c) {
	agent_t *a = c->param.agent;
	int n, m;
	n = c->param.args[0].number;
	m = c->param.args[1].number;

	int i = 0;
	for_each_entry(resource_t, r, &a->view->resources) {
		if (agent_is_owner(a, r)) {
			i++;
		}
	}

	if (i >= n && i <= m) {
		return 0;
	} else if (i < n) {
		return n - i;
	} else {
		return i - m;
	}
}

double type(constraint_t *c) {
	agent_t *a = c->param.agent;
	char *t = c->param.args[0].string;
	int n, m;
	n = c->param.args[1].number;
	m = c->param.args[2].number;

	int i = 0;
	for_each_entry(resource_t, r, &a->view->resources) {
		if (agent_is_owner(a, r) && !strcmp(r->type, t)) {
			i++;
		}
	}

	if (i >= n && i <= m) {
		return 0;
	} else if (i < n) {
		return n - i;
	} else {
		return i - m;
	}
}

double share(constraint_t *c) {
	agent_t *a = c->param.agent;
	int b = c->param.neighbors[0];

	for_each_entry(resource_t, r, &a->view->resources) {
		resource_t *_r = view_get_resource(a->agent_view[b], r->index);

		if (_r && agent_is_owner(a, r) && resource_get_owner(_r) == b) {
			return INFINITY;
		}
	}

	return 0;
}

double prefer_free(constraint_t *c) {
	agent_t *a = c->param.agent;
	int b = c->param.neighbors[0];

	double n = 0;

	for_each_entry(resource_t, r, &a->view->resources) {
		resource_t *_r = view_get_resource(a->agent_view[b], r->index);

		if (_r && agent_is_owner(a, r) && resource_get_owner(_r) == b) {
			n++;
		}
	}

	return n / 2;
}

// TODO: well, what about n == 0 and n == 1?
double _downey(double A, double sigma, double n) {
	double S = -1;

	if (n == 1) {
		S = 0;
	}

	if (sigma < 1) {
		if (1 < n && n <= A) {
			S = (n * A) / (A + (sigma / (2 * (n - 1))));
		} else if (A <= n && n <= 2 * A - 1) {
			S = (n * A) / (sigma * (A - 1/2) + n * (1 - sigma/2));
		} else if (n >= 2 * A - 1) {
			S = A;
		}
	} else {
		if (1 < n && n <= A + A * sigma - sigma) {
			S = (n * A * (sigma + 1)) / (sigma * (n + A - 1) + A);
		} else if (n >= A + A * sigma - sigma) {
			S = A;
		}
	}

	return S;
}

double downey(constraint_t *c) {
	agent_t *a = c->param.agent;
	int b = c->param.neighbors[0];
	double a_A = c->param.args[0].number;
	double a_sigma = c->param.args[1].number;
	double b_A = c->param.args[2].number;
	double b_sigma = c->param.args[3].number;

	if (!agent_has_conflicting_view(a, b)) {
		return 0;
	}

	double d_A = _downey(a_A, a_sigma, view_count_resources(a->view, a->id));
	double d_B = _downey(b_A, b_sigma, view_count_resources(a->agent_view[b], b));

	if (d_A > d_B) {
		return 0;
	} else {
		return INFINITY;
	}
}

double speedup(constraint_t *c) {
	agent_t *a = c->param.agent;
	double A = c->param.args[0].number;
	double sigma = c->param.args[1].number;

	double S = _downey(A, sigma, view_count_resources(a->view, a->id));

	if (S != 0) {
		return 1 / S;
	} else {
		return 1;
	}
}

double and(constraint_t *c) {
	constraint_t *c1 = c->param.args[0].constraint;
	constraint_t *c2 = c->param.args[1].constraint;

	return fmax(c1->eval(c1), c2->eval(c2));
}

double or(constraint_t *c) {
	constraint_t *c1 = c->param.args[0].constraint;
	constraint_t *c2 = c->param.args[1].constraint;

	return fmin(c1->eval(c1), c2->eval(c2));
}

double nop(constraint_t *c) {
	return 0;
}

void register_native_constraints() {
	register_native_constraint("TILE", tile);
	register_native_constraint("NEC_RE", nec_re);
	register_native_constraint("TYPE", type);
	register_native_constraint("SHARE", share);
	register_native_constraint("PREFER_FREE", prefer_free);
	register_native_constraint("DOWNEY", downey);
	register_native_constraint("SPEEDUP", speedup);
	register_native_constraint("AND", and);
	register_native_constraint("OR", or);
	register_native_constraint("NOP", nop);
}

