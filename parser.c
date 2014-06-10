/*	$Id$ */
/*
 * Copyright (c) 2014 Kristaps Dzonsons <kristaps@kcons.eu>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <gsl/gsl_multifit.h>
#include <gsl/gsl_histogram.h>

#include "extern.h"

#define	BUFSZ 1024

/*
 * All possible input tokens, including some which are "virtual" tokens
 * in that they don't map to an exact character representation.
 */
enum	token {
	TOKEN_PAREN_OPEN,
	TOKEN_PAREN_CLOSE,
	TOKEN_ADD,
	TOKEN_SUB,
	TOKEN_MUL,
	TOKEN_DIV,
	TOKEN_EXP,
	TOKEN_END,
	TOKEN_NUMBER,
	TOKEN_SQRT,
	TOKEN_EXPF,
	TOKEN_P1,
	TOKEN_PN,
	TOKEN_N,
	TOKEN_ERROR,
	TOKEN_SKIP,
	TOKEN_POSITIVE,
	TOKEN_NEGATIVE,
	TOK__MAX
};

enum	arity {
	ARITY_NONE = 0,
	ARITY_ONE,
	ARITY_TWO
};

enum	assoc {
	ASSOC_NONE = 0,
	ASSOC_L,
	ASSOC_R,
};

struct	tok {
	char		 key; /* input identifier */
	int		 prec; /* precedence (if applicable) */
	int		 oper; /* is operator? */
	int		 func; /* is function? */
	enum assoc	 assoc; /* associativity */
	enum arity	 arity; /* n-ary-ess */
	enum htype	 map; /* map to htype (if applicable) */
};

static	const struct tok toks[TOK__MAX] = {
	{ '(',  -1, 0, 0, ASSOC_NONE, ARITY_NONE, HNODE__MAX }, /* TOKEN_PAREN_OPEN */
	{ ')',  -1, 0, 0, ASSOC_NONE, ARITY_NONE, HNODE__MAX }, /* TOKEN_PAREN_CLOSE */
	{ '+',  2, 1, 0, ASSOC_L, ARITY_TWO, HNODE_ADD }, /* TOKEN_ADD */
	{ '-',  2, 1, 0, ASSOC_L, ARITY_TWO, HNODE_SUB }, /* TOKEN_SUB */
	{ '*',  3, 1, 0, ASSOC_L, ARITY_TWO, HNODE_MUL }, /* TOKEN_MUL */
	{ '/',  3, 1, 0, ASSOC_L, ARITY_TWO, HNODE_DIV }, /* TOKEN_DIV */
	{ '^',  5, 1, 0, ASSOC_R, ARITY_TWO, HNODE_EXP }, /* TOKEN_EXP */
	{ '\0', -1, 0, 0, ASSOC_NONE, ARITY_NONE, HNODE__MAX }, /* TOKEN_END */
	{ '\0', -1, 0, 0, ASSOC_NONE, ARITY_NONE, HNODE_NUMBER }, /* TOKEN_NUMBER */
	{ '\0', -1, 0, 1, ASSOC_NONE, ARITY_NONE, HNODE_SQRT }, /* TOKEN_SQRT */
	{ '\0', -1, 0, 1, ASSOC_NONE, ARITY_NONE, HNODE_EXPF }, /* TOKEN_EXPF */
	{ 'x',  -1, 0, 0, ASSOC_NONE, ARITY_NONE, HNODE_P1 }, /* TOKEN_P1 */
	{ 'X',  -1, 0, 0, ASSOC_NONE, ARITY_NONE, HNODE_PN }, /* TOKEN_PN */
	{ 'n',  -1, 0, 0, ASSOC_NONE, ARITY_NONE, HNODE_N }, /* TOKEN_N */
	{ '\0', -1, 0, 0, ASSOC_NONE, ARITY_NONE, HNODE__MAX }, /* TOKEN_ERROR */
	{ ' ',  -1, 0, 0, ASSOC_NONE, ARITY_NONE, HNODE__MAX }, /* TOKEN_SKIP */
	{ '+',  4, 1, 0, ASSOC_R, ARITY_ONE, HNODE_POSITIVE }, /* TOKEN_POSITIVE */
	{ '-',  4, 1, 0, ASSOC_R, ARITY_ONE, HNODE_NEGATIVE }, /* TOKEN_NEGATIVE */
};

#if 0
static void
hnode_print(struct hnode **p)
{
	struct hnode	**pp;

	if (NULL == p)
		return;

	for (pp = p; NULL != *pp; pp++) {
		switch ((*pp)->type) {
		case (HNODE_VAR):
			putchar('v');
			break;
		case (HNODE_P1):
			putchar('x');
			break;
		case (HNODE_P2):
			putchar('y');
			break;
		case (HNODE_NUMBER):
			printf("%g", (*pp)->real);
			break;
		case (HNODE_SQRT):
			printf("sqrt");
			break;
		case (HNODE_ADD):
			putchar('+');
			break;
		case (HNODE_EXP):
			putchar('^');
			break;
		case (HNODE_SUB):
			putchar('-');
			break;
		case (HNODE_MUL):
			putchar('*');
			break;
		case (HNODE_DIV):
			putchar('/');
			break;
		case (HNODE_POSITIVE):
			printf("+'");
			break;
		case (HNODE_NEGATIVE):
			printf("-'");
			break;
		default:
			abort();
		}
		putchar(' ');
	}
	putchar('\n');
}
#endif

/*
 * Check if the current token (which has ARITY_ONE, we assume) is in
 * fact unary.
 * We do this by checking the previous token: if it's a binary operator
 * or an open parenthesis, then we're a unary operator.
 */
static int
check_unary(enum token lasttok)
{
	switch (lasttok) {
	case (TOK__MAX):
		/* FALLTHROUGH */
	case (TOKEN_PAREN_OPEN):
		/* FALLTHROUGH */
	case (TOKEN_ADD):
		/* FALLTHROUGH */
	case (TOKEN_SUB):
		/* FALLTHROUGH */
	case (TOKEN_MUL):
		/* FALLTHROUGH */
	case (TOKEN_DIV):
		/* FALLTHROUGH */
	case (TOKEN_EXP):
		return(1);
	default:
		break;
	}
	return(0);
}

/*
 * Check if the current token (which has ARITY_TWO, we assume) is in
 * fact binary.
 * We do this by checking the previous token: if it's an operand or a
 * close-paren (i.e., an operand), then we're a binary operator.
 */
static int
check_binary(enum token lasttok)
{
	switch (lasttok) {
	case (TOKEN_PAREN_CLOSE):
		/* FALLTHROUGH */
	case (TOKEN_NUMBER):
		/* FALLTHROUGH */
	case (TOKEN_P1):
		/* FALLTHROUGH */
	case (TOKEN_PN):
		/* FALLTHROUGH */
	case (TOKEN_N):
		return(1);
	default:
		break;
	}
	return(0);
}

/*
 * Extra a token from input.
 * If TOKEN_ERROR, something bad has happened in the attempt to do so.
 * Otherwise, this returns a valid token (possibly end of input).
 */
static enum token
tokenise(const char **v, char *buf, enum token lasttok)
{
	size_t		i, sz;

	/* Short-circuit this case. */
	if ('\0' == **v)
		return(TOKEN_END);

	/* Look for token in our predefined inputs. */
	for (i = 0; i < TOK__MAX; i++)
		if ('\0' != toks[i].key && **v == toks[i].key) {
			switch (toks[i].arity) {
			case (ARITY_NONE):
				(*v)++;
				return((enum token)i);
			case (ARITY_ONE):
				if ( ! check_unary(lasttok))
					continue;
				break;
			case (ARITY_TWO):
				if ( ! check_binary(lasttok))
					continue;
				break;
			}
			(*v)++;
			return((enum token)i);
		}

	/* See if we're a real number or identifier. */
	if (isdigit((int)**v) || '.' == **v) {
		sz = 0;
		for ( ; isdigit((int)**v) || '.' == **v; (*v)++) {
			assert(sz < BUFSZ);
			buf[sz++] = **v;
		}
		buf[sz] = '\0';
		return(TOKEN_NUMBER);
	} else if (isalpha((int)**v)) {
		sz = 0;
		for ( ; isalpha((int)**v); (*v)++) {
			assert(sz < BUFSZ);
			buf[sz++] = **v;
		}
		buf[sz] = '\0';
		if (0 == strcmp(buf, "sqrt"))
			return(TOKEN_SQRT);
		else if (0 == strcmp(buf, "exp"))
			return(TOKEN_EXPF);
	} 

	/* Eh... */
	return(TOKEN_ERROR);
}

/*
 * Queue something onto the output RPN queue.
 */
static void
enqueue(struct hnode ***q, size_t *qsz, enum token tok)
{

	*q = realloc(*q, ++(*qsz) * sizeof(struct hnode *));
	(*q)[*qsz - 1] = calloc(1, sizeof(struct hnode));
	(*q)[*qsz - 1]->type = toks[tok].map;
	assert(HNODE__MAX != toks[tok].map);
}

/*
 * Make sure that all operators and functions are matched with
 * arguments.
 * The Shunting-Yard algorithm itself doesn't do this, so we need to do
 * it now.
 */
static int
check(struct hnode **p)
{
	struct hnode	**pp;
	size_t		  ssz;

	for (ssz = 0, pp = p; NULL != *pp; pp++)
		switch ((*pp)->type) {
		case (HNODE_P1):
			/* FALLTHROUGH */
		case (HNODE_PN):
			/* FALLTHROUGH */
		case (HNODE_N):
			/* FALLTHROUGH */
		case (HNODE_NUMBER):
			ssz++;
			break;
		case (HNODE_POSITIVE):
			/* FALLTHROUGH */
		case (HNODE_NEGATIVE):
			/* FALLTHROUGH */
		case (HNODE_SQRT):
			/* FALLTHROUGH */
		case (HNODE_EXPF):
			if (0 == ssz)
				return(0);
			break;
		case (HNODE_ADD):
			/* FALLTHROUGH */
		case (HNODE_SUB):
			/* FALLTHROUGH */
		case (HNODE_MUL):
			/* FALLTHROUGH */
		case (HNODE_DIV):
			/* FALLTHROUGH */
		case (HNODE_EXP):
			if (ssz < 2)
				return(0);
			ssz--;
			break;
		default:
			abort();
		}

	return(1 == ssz);
}

/*
 * Dijkstra's Shunting-Yard algorithm for converting an infix-order
 * expression to prefix order.
 * This returns a NULL-terminated list of expressions to evaluate in
 * post-fix order.
 */
struct hnode **
hnode_parse(const char **v)
{
	enum token	  tok, lasttok;
	enum token	  stack[STACKSZ];
	char		  buf[BUFSZ + 1];
	struct hnode	**q;
	int		  found;
	size_t		  i, qsz, ssz;

	q = NULL;
	qsz = ssz = 0;
	lasttok = TOK__MAX;

	while (TOKEN_END != (tok = tokenise(v, buf, lasttok))) {
		switch (tok) {
		case (TOKEN_ERROR): 
			goto err;
		case (TOKEN_SKIP):
			break;
		case (TOKEN_P1):
			/* FALLTHROUGH */
		case (TOKEN_PN):
			/* FALLTHROUGH */
		case (TOKEN_N):
			enqueue(&q, &qsz, tok);
			break;
		case (TOKEN_NUMBER):
			enqueue(&q, &qsz, tok);
			q[qsz - 1]->real = atof(buf);
			break;
		case (TOKEN_SQRT):
			/* FALLTHROUGH */
		case (TOKEN_EXPF):
			/* FALLTHROUGH */
		case (TOKEN_PAREN_OPEN):
			assert(ssz < STACKSZ);
			stack[ssz++] = tok;
			break;
		case (TOKEN_PAREN_CLOSE):
			assert(ssz > 0);
			found = 0;
			do {
				if (TOKEN_PAREN_OPEN == stack[--ssz])
					found = 1;
				else
					enqueue(&q, &qsz, stack[ssz]);
			} while ( ! found && ssz > 0);
			assert(found);
			if (ssz > 0 && toks[stack[ssz - 1]].func)
				enqueue(&q, &qsz, stack[--ssz]);
			break;
		case (TOKEN_POSITIVE):
			/* FALLTHROUGH */
		case (TOKEN_NEGATIVE):
			/* FALLTHROUGH */
		case (TOKEN_SUB):
			/* FALLTHROUGH */
		case (TOKEN_MUL):
			/* FALLTHROUGH */
		case (TOKEN_DIV):
			/* FALLTHROUGH */
		case (TOKEN_EXP):
			/* FALLTHROUGH */
		case (TOKEN_ADD):
			assert(toks[tok].prec >= 0);
			assert(ASSOC_NONE != toks[tok].assoc);
			while (ssz > 0 && toks[stack[ssz - 1]].oper) {
				assert(toks[stack[ssz - 1]].prec >= 0);
				assert(ASSOC_NONE != 
					toks[stack[ssz - 1]].assoc);
				if ((ASSOC_L == toks[tok].assoc 
					 && toks[tok].prec == 
					 toks[stack[ssz - 1]].prec) || 
					 (toks[tok].prec < 
					  toks[stack[ssz - 1]].prec))
					enqueue(&q, &qsz, stack[--ssz]);
				else
					break;
			}
			assert(ssz < STACKSZ);
			stack[ssz++] = tok;
			break;
		default:
			abort();
		}
		if (TOKEN_SKIP != tok)
			lasttok = tok;
	}

	while (ssz > 0) {
		--ssz;
		if (TOKEN_PAREN_OPEN == stack[ssz])
			goto err;
		enqueue(&q, &qsz, stack[ssz]);
	} 

	q = realloc(q, ++qsz * sizeof(struct hnode *));
	q[qsz - 1] = NULL;

	if ( ! check(q))
		goto err;

#if 0
	hnode_print(q);
#endif
	return(q);
err:
	for (i = 0; i < qsz; i++)
		free(q[i]);
	free(q);
	return(NULL);
}

struct hnode **
hnode_copy(struct hnode **p)
{
	struct hnode	**pp;
	size_t		  sz;

	for (sz = 0, pp = p; NULL != *pp; pp++)
		sz++;

	pp = calloc(sz + 1, sizeof(struct hnode *));
	for (sz = 0; NULL != *p; p++, sz++) {
		pp[sz] = calloc(1, sizeof(struct hnode));
		pp[sz]->type = (*p)->type;
		pp[sz]->real = (*p)->real;
	}

	return(pp);
}

void
hnode_free(struct hnode **p)
{
	struct hnode	**pp;

	if (NULL == p)
		return;

	for (pp = p; NULL != *pp; pp++)
		free(*pp);

	free(p);
}

/*
 * Execute a function in prefix order given variables x (given player's
 * strategy), y (other player's strategy), and var (given player's
 * morality index).
 */
double
hnode_exec(const struct hnode *const *p, double x, double X, size_t n)
{
	double		  stack[STACKSZ];
	const struct hnode *const *pp;
	size_t		  ssz;
	double		  val;

	for (ssz = 0, pp = p; NULL != *pp; pp++)
		switch ((*pp)->type) {
		case (HNODE_P1):
			assert(ssz < STACKSZ);
			stack[ssz++] = x;
			break;
		case (HNODE_PN):
			assert(ssz < STACKSZ);
			stack[ssz++] = X;
			break;
		case (HNODE_N):
			assert(ssz < STACKSZ);
			stack[ssz++] = (double)n;
			break;
		case (HNODE_NUMBER):
			assert(ssz < STACKSZ);
			stack[ssz++] = (*pp)->real;
			break;
		case (HNODE_SQRT):
			assert(ssz > 0);
			val = sqrt(stack[--ssz]);
			stack[ssz++] = val;
			break;
		case (HNODE_EXPF):
			assert(ssz > 0);
			val = exp(stack[--ssz]);
			stack[ssz++] = val;
			break;
		case (HNODE_ADD):
			assert(ssz > 1);
			val = stack[ssz - 2] + stack[ssz - 1];
			ssz -= 2;
			stack[ssz++] = val;
			break;
		case (HNODE_SUB):
			assert(ssz > 1);
			val = stack[ssz - 2] - stack[ssz - 1];
			ssz -= 2;
			stack[ssz++] = val;
			break;
		case (HNODE_MUL):
			assert(ssz > 1);
			val = stack[ssz - 2] * stack[ssz - 1];
			ssz -= 2;
			stack[ssz++] = val;
			break;
		case (HNODE_DIV):
			assert(ssz > 1);
			val = stack[ssz - 2] / stack[ssz - 1];
			ssz -= 2;
			stack[ssz++] = val;
			break;
		case (HNODE_EXP):
			assert(ssz > 1);
			val = pow(stack[ssz - 2], stack[ssz - 1]);
			ssz -= 2;
			stack[ssz++] = val;
			break;
		case (HNODE_POSITIVE):
			assert(ssz > 0);
			break;
		case (HNODE_NEGATIVE):
			assert(ssz > 0);
			val = -(stack[--ssz]);
			stack[ssz++] = val;
			break;
		default:
			abort();
		}

	assert(1 == ssz);
	return(stack[0]);
}

static void
hnode_test_expect(double x, double X, 
	double n, const char *expf, double vexp)
{
	struct hnode	 **exp;
	double		   v;
	const char	  *expfp;

	expfp = expf;
	exp = hnode_parse((const char **)&expfp);
	assert(NULL != exp);
	v = hnode_exec
		((const struct hnode *const *)exp, x, X, n);
	g_debug("pi(x=%g, X=%g, n=%g) = %s = %g (want %g)", 
		x, X, n, expf, v, vexp);
	hnode_free(exp);
}

void
hnode_test(void)
{
	double	x, X, n;

	x = 10.0;
	X = 20.0;
	n = 2.0;
	hnode_test_expect(x, X, n, 
		"(1 - exp(-X)) - x", 
		(1.0 - exp(-X)) - x);
	hnode_test_expect(x, X, n, 
		"sqrt(1 / n * X) - 0.5 * x^2", 
		sqrt(1.0 / n * X) - 0.5 * pow(x, 2.0));
	hnode_test_expect(x, X, n, 
		"x - (X - x) * x - x^2",
		x - (X - x) * x - pow(x, 2.0));
	hnode_test_expect(x, X, n, 
		"x * (1 / X) - x",
		x * (1.0 / X) - x);
}
