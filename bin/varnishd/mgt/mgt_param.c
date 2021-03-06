/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "config.h"

#include <grp.h>
#include <limits.h>
#include <math.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mgt/mgt.h"
#include "common/heritage.h"
#include "common/params.h"

#include "mgt/mgt_param.h"
#include "waiter/waiter.h"
#include "vav.h"
#include "vcli.h"
#include "vcli_common.h"
#include "vcli_priv.h"
#include "vnum.h"
#include "vss.h"

#include "mgt_cli.h"

#define MAGIC_INIT_STRING	"\001"
struct params mgt_param;
static int nparspec;
static struct parspec const ** parspecs;
static int margin;

/*--------------------------------------------------------------------*/

static const struct parspec *
mcf_findpar(const char *name)
{
	int i;

	for (i = 0; i < nparspec; i++)
		if (!strcmp(parspecs[i]->name, name))
			return (parspecs[i]);
	return (NULL);
}

/*--------------------------------------------------------------------*/

static void
tweak_generic_timeout(struct cli *cli, volatile unsigned *dst, const char *arg)
{
	unsigned u;

	if (arg != NULL) {
		u = strtoul(arg, NULL, 0);
		if (u == 0) {
			VCLI_Out(cli, "Timeout must be greater than zero\n");
			VCLI_SetResult(cli, CLIS_PARAM);
			return;
		}
		*dst = u;
	} else
		VCLI_Out(cli, "%u", *dst);
}

/*--------------------------------------------------------------------*/

static void
tweak_timeout(struct cli *cli, const struct parspec *par, const char *arg)
{
	volatile unsigned *dest;

	dest = par->priv;
	tweak_generic_timeout(cli, dest, arg);
}

/*--------------------------------------------------------------------*/

static int
tweak_generic_timeout_double(struct cli *cli, volatile double *dest,
    const char *arg, double min, double max)
{
	double u;
	char *p;

	if (arg != NULL) {
		p = NULL;
		u = strtod(arg, &p);
		if (*arg == '\0' || *p != '\0') {
			VCLI_Out(cli, "Not a number(%s)\n", arg);
			VCLI_SetResult(cli, CLIS_PARAM);
			return (-1);
		}
		if (u < min) {
			VCLI_Out(cli,
			    "Timeout must be greater or equal to %.g\n", min);
			VCLI_SetResult(cli, CLIS_PARAM);
			return (-1);
		}
		if (u > max) {
			VCLI_Out(cli,
			    "Timeout must be less than or equal to %.g\n", max);
			VCLI_SetResult(cli, CLIS_PARAM);
			return (-1);
		}
		*dest = u;
	} else
		VCLI_Out(cli, "%.6f", *dest);
	return (0);
}

void
tweak_timeout_double(struct cli *cli, const struct parspec *par,
    const char *arg)
{
	volatile double *dest;

	dest = par->priv;
	(void)tweak_generic_timeout_double(cli, dest, arg, par->min, par->max);
}

/*--------------------------------------------------------------------*/

static void
tweak_generic_double(struct cli *cli, const struct parspec *par,
    const char *arg)
{
	volatile double *dest;
	char *p;
	double u;

	dest = par->priv;
	if (arg != NULL) {
		p = NULL;
		u = strtod(arg, &p);
		if (*p != '\0') {
			VCLI_Out(cli,
			    "Not a number (%s)\n", arg);
			VCLI_SetResult(cli, CLIS_PARAM);
			return;
		}
		if (u < par->min) {
			VCLI_Out(cli,
			    "Must be greater or equal to %.g\n",
				 par->min);
			VCLI_SetResult(cli, CLIS_PARAM);
			return;
		}
		if (u > par->max) {
			VCLI_Out(cli,
			    "Must be less than or equal to %.g\n",
				 par->max);
			VCLI_SetResult(cli, CLIS_PARAM);
			return;
		}
		*dest = u;
	} else
		VCLI_Out(cli, "%f", *dest);
}

/*--------------------------------------------------------------------*/

static void
tweak_generic_bool(struct cli *cli, volatile unsigned *dest, const char *arg)
{
	if (arg != NULL) {
		if (!strcasecmp(arg, "off"))
			*dest = 0;
		else if (!strcasecmp(arg, "disable"))
			*dest = 0;
		else if (!strcasecmp(arg, "no"))
			*dest = 0;
		else if (!strcasecmp(arg, "false"))
			*dest = 0;
		else if (!strcasecmp(arg, "on"))
			*dest = 1;
		else if (!strcasecmp(arg, "enable"))
			*dest = 1;
		else if (!strcasecmp(arg, "yes"))
			*dest = 1;
		else if (!strcasecmp(arg, "true"))
			*dest = 1;
		else {
			VCLI_Out(cli, "use \"on\" or \"off\"\n");
			VCLI_SetResult(cli, CLIS_PARAM);
			return;
		}
	} else
		VCLI_Out(cli, *dest ? "on" : "off");
}

/*--------------------------------------------------------------------*/

static void
tweak_bool(struct cli *cli, const struct parspec *par, const char *arg)
{
	volatile unsigned *dest;

	dest = par->priv;
	tweak_generic_bool(cli, dest, arg);
}

/*--------------------------------------------------------------------*/

int
tweak_generic_uint(struct cli *cli, volatile unsigned *dest, const char *arg,
    unsigned min, unsigned max)
{
	unsigned u;
	char *p;

	if (arg != NULL) {
		p = NULL;
		if (!strcasecmp(arg, "unlimited"))
			u = UINT_MAX;
		else {
			u = strtoul(arg, &p, 0);
			if (*arg == '\0' || *p != '\0') {
				VCLI_Out(cli, "Not a number (%s)\n", arg);
				VCLI_SetResult(cli, CLIS_PARAM);
				return (-1);
			}
		}
		if (u < min) {
			VCLI_Out(cli, "Must be at least %u\n", min);
			VCLI_SetResult(cli, CLIS_PARAM);
			return (-1);
		}
		if (u > max) {
			VCLI_Out(cli, "Must be no more than %u\n", max);
			VCLI_SetResult(cli, CLIS_PARAM);
			return (-1);
		}
		*dest = u;
	} else if (*dest == UINT_MAX) {
		VCLI_Out(cli, "unlimited");
	} else {
		VCLI_Out(cli, "%u", *dest);
	}
	return (0);
}

/*--------------------------------------------------------------------*/

void
tweak_uint(struct cli *cli, const struct parspec *par, const char *arg)
{
	volatile unsigned *dest;

	dest = par->priv;
	(void)tweak_generic_uint(cli, dest, arg,
	    (uint)par->min, (uint)par->max);
}

/*--------------------------------------------------------------------*/

static void
fmt_bytes(struct cli *cli, uintmax_t t)
{
	const char *p;

	if (t & 0xff) {
		VCLI_Out(cli, "%jub", t);
		return;
	}
	for (p = "kMGTPEZY"; *p; p++) {
		if (t & 0x300) {
			VCLI_Out(cli, "%.2f%c", t / 1024.0, *p);
			return;
		}
		t /= 1024;
		if (t & 0x0ff) {
			VCLI_Out(cli, "%ju%c", t, *p);
			return;
		}
	}
	VCLI_Out(cli, "(bogus number)");
}

static void
tweak_generic_bytes(struct cli *cli, volatile ssize_t *dest, const char *arg,
    double min, double max)
{
	uintmax_t r;
	const char *p;

	if (arg != NULL) {
		p = VNUM_2bytes(arg, &r, 0);
		if (p != NULL) {
			VCLI_Out(cli, "Could not convert to bytes.\n");
			VCLI_Out(cli, "%s\n", p);
			VCLI_Out(cli,
			    "  Try something like '80k' or '120M'\n");
			VCLI_SetResult(cli, CLIS_PARAM);
			return;
		}
		if ((uintmax_t)((ssize_t)r) != r) {
			fmt_bytes(cli, r);
			VCLI_Out(cli, " is too large for this architecture.\n");
			VCLI_SetResult(cli, CLIS_PARAM);
			return;
		}
		if (max != 0. && r > max) {
			VCLI_Out(cli, "Must be no more than ");
			fmt_bytes(cli, (uintmax_t)max);
			VCLI_Out(cli, "\n");
			VCLI_SetResult(cli, CLIS_PARAM);
			return;
		}
		if (r < min) {
			VCLI_Out(cli, "Must be at least ");
			fmt_bytes(cli, (uintmax_t)min);
			VCLI_Out(cli, "\n");
			VCLI_SetResult(cli, CLIS_PARAM);
			return;
		}
		*dest = r;
	} else {
		fmt_bytes(cli, *dest);
	}
}

/*--------------------------------------------------------------------*/

void
tweak_bytes(struct cli *cli, const struct parspec *par, const char *arg)
{
	volatile ssize_t *dest;

	assert(par->min >= 0);
	dest = par->priv;
	tweak_generic_bytes(cli, dest, arg, par->min, par->max);
}


/*--------------------------------------------------------------------*/

static void
tweak_bytes_u(struct cli *cli, const struct parspec *par, const char *arg)
{
	volatile unsigned *d1;
	volatile ssize_t dest;

	assert(par->max <= UINT_MAX);
	assert(par->min >= 0);
	d1 = par->priv;
	dest = *d1;
	tweak_generic_bytes(cli, &dest, arg, par->min, par->max);
	*d1 = dest;
}

/*--------------------------------------------------------------------
 * XXX: slightly magic.  We want to initialize to "nobody" (XXX: shouldn't
 * XXX: that be something autocrap found for us ?) but we don't want to
 * XXX: fail initialization if that user doesn't exists, even though we
 * XXX: do want to fail it, in subsequent sets.
 * XXX: The magic init string is a hack for this.
 */

static void
tweak_user(struct cli *cli, const struct parspec *par, const char *arg)
{
	struct passwd *pw;
	struct group *gr;

	(void)par;
	if (arg != NULL) {
		if (!strcmp(arg, MAGIC_INIT_STRING)) {
			pw = getpwnam("nobody");
			if (pw == NULL) {
				mgt_param.uid = getuid();
				return;
			}
		} else
			pw = getpwnam(arg);
		if (pw == NULL) {
			VCLI_Out(cli, "Unknown user");
			VCLI_SetResult(cli, CLIS_PARAM);
			return;
		}
		REPLACE(mgt_param.user, pw->pw_name);
		mgt_param.uid = pw->pw_uid;
		mgt_param.gid = pw->pw_gid;

		/* set group to user's primary group */
		if ((gr = getgrgid(pw->pw_gid)) != NULL &&
		    (gr = getgrnam(gr->gr_name)) != NULL &&
		    gr->gr_gid == pw->pw_gid)
			REPLACE(mgt_param.group, gr->gr_name);
	} else if (mgt_param.user) {
		VCLI_Out(cli, "%s (%d)", mgt_param.user, (int)mgt_param.uid);
	} else {
		VCLI_Out(cli, "%d", (int)mgt_param.uid);
	}
}

/*--------------------------------------------------------------------
 * XXX: see comment for tweak_user, same thing here.
 */

static void
tweak_group(struct cli *cli, const struct parspec *par, const char *arg)
{
	struct group *gr;

	(void)par;
	if (arg != NULL) {
		if (!strcmp(arg, MAGIC_INIT_STRING)) {
			gr = getgrnam("nogroup");
			if (gr == NULL) {
				/* Only replace if tweak_user didn't */
				if (mgt_param.gid == 0)
					mgt_param.gid = getgid();
				return;
			}
		} else
			gr = getgrnam(arg);
		if (gr == NULL) {
			VCLI_Out(cli, "Unknown group");
			VCLI_SetResult(cli, CLIS_PARAM);
			return;
		}
		REPLACE(mgt_param.group, gr->gr_name);
		mgt_param.gid = gr->gr_gid;
	} else if (mgt_param.group) {
		VCLI_Out(cli, "%s (%d)", mgt_param.group, (int)mgt_param.gid);
	} else {
		VCLI_Out(cli, "%d", (int)mgt_param.gid);
	}
}

/*--------------------------------------------------------------------*/

static void
clean_listen_sock_head(struct listen_sock_head *lsh)
{
	struct listen_sock *ls, *ls2;

	VTAILQ_FOREACH_SAFE(ls, lsh, list, ls2) {
		CHECK_OBJ_NOTNULL(ls, LISTEN_SOCK_MAGIC);
		VTAILQ_REMOVE(lsh, ls, list);
		free(ls->name);
		free(ls->addr);
		FREE_OBJ(ls);
	}
}

static void
tweak_listen_address(struct cli *cli, const struct parspec *par,
    const char *arg)
{
	char **av;
	int i;
	struct listen_sock		*ls;
	struct listen_sock_head		lsh;

	(void)par;
	if (arg == NULL) {
		VCLI_Quote(cli, mgt_param.listen_address);
		return;
	}

	av = VAV_Parse(arg, NULL, ARGV_COMMA);
	if (av == NULL) {
		VCLI_Out(cli, "Parse error: out of memory");
		VCLI_SetResult(cli, CLIS_PARAM);
		return;
	}
	if (av[0] != NULL) {
		VCLI_Out(cli, "Parse error: %s", av[0]);
		VCLI_SetResult(cli, CLIS_PARAM);
		VAV_Free(av);
		return;
	}
	if (av[1] == NULL) {
		VCLI_Out(cli, "Empty listen address");
		VCLI_SetResult(cli, CLIS_PARAM);
		VAV_Free(av);
		return;
	}
	VTAILQ_INIT(&lsh);
	for (i = 1; av[i] != NULL; i++) {
		struct vss_addr **ta;
		int j, n;

		n = VSS_resolve(av[i], "http", &ta);
		if (n == 0) {
			VCLI_Out(cli, "Invalid listen address ");
			VCLI_Quote(cli, av[i]);
			VCLI_SetResult(cli, CLIS_PARAM);
			break;
		}
		for (j = 0; j < n; ++j) {
			ALLOC_OBJ(ls, LISTEN_SOCK_MAGIC);
			AN(ls);
			ls->sock = -1;
			ls->addr = ta[j];
			ls->name = strdup(av[i]);
			AN(ls->name);
			VTAILQ_INSERT_TAIL(&lsh, ls, list);
		}
		free(ta);
	}
	VAV_Free(av);
	if (cli != NULL && cli->result != CLIS_OK) {
		clean_listen_sock_head(&lsh);
		return;
	}

	REPLACE(mgt_param.listen_address, arg);

	clean_listen_sock_head(&heritage.socks);
	heritage.nsocks = 0;

	while (!VTAILQ_EMPTY(&lsh)) {
		ls = VTAILQ_FIRST(&lsh);
		VTAILQ_REMOVE(&lsh, ls, list);
		CHECK_OBJ_NOTNULL(ls, LISTEN_SOCK_MAGIC);
		VTAILQ_INSERT_TAIL(&heritage.socks, ls, list);
		heritage.nsocks++;
	}
}

/*--------------------------------------------------------------------*/

static void
tweak_string(struct cli *cli, const struct parspec *par, const char *arg)
{
	char **p = TRUST_ME(par->priv);

	AN(p);
	/* XXX should have tweak_generic_string */
	if (arg == NULL) {
		VCLI_Quote(cli, *p);
	} else {
		REPLACE(*p, arg);
	}
}

/*--------------------------------------------------------------------*/

static void
tweak_waiter(struct cli *cli, const struct parspec *par, const char *arg)
{

	/* XXX should have tweak_generic_string */
	(void)par;
	WAIT_tweak_waiter(cli, arg);
}

/*--------------------------------------------------------------------*/

static void
tweak_poolparam(struct cli *cli, const struct parspec *par, const char *arg)
{
	volatile struct poolparam *pp, px;
	char **av;

	pp = par->priv;
	if (arg == NULL) {
		VCLI_Out(cli, "%u,%u,%g",
		    pp->min_pool, pp->max_pool, pp->max_age);
	} else {
		av = VAV_Parse(arg, NULL, ARGV_COMMA);
		do {
			if (av[0] != NULL) {
				VCLI_Out(cli, "Parse error: %s", av[0]);
				VCLI_SetResult(cli, CLIS_PARAM);
				break;
			}
			if (av[1] == NULL || av[2] == NULL || av[3] == NULL) {
				VCLI_Out(cli,
				    "Three fields required:"
				    " min_pool, max_pool and max_age\n");
				VCLI_SetResult(cli, CLIS_PARAM);
				break;
			}
			px = *pp;
			if (tweak_generic_uint(cli, &px.min_pool, av[1],
			    (uint)par->min, (uint)par->max))
				break;
			if (tweak_generic_uint(cli, &px.max_pool, av[2],
			    (uint)par->min, (uint)par->max))
				break;
			if (tweak_generic_timeout_double(cli, &px.max_age,
			    av[3], 0, 1e6))
				break;
			if (px.min_pool > px.max_pool) {
				VCLI_Out(cli,
				    "min_pool cannot be larger"
				    " than max_pool\n");
				VCLI_SetResult(cli, CLIS_PARAM);
				break;
			}
			*pp = px;
		} while(0);
	}
}

/*--------------------------------------------------------------------*/

/*
 * Make sure to end all lines with either a space or newline of the
 * formatting will go haywire.
 */

#define DELAYED_EFFECT_TEXT \
	"\nNB: This parameter may take quite some time to take (full) effect."

#define MUST_RESTART_TEXT \
	"\nNB: This parameter will not take any effect until the " \
	"child process has been restarted."

#define MUST_RELOAD_TEXT \
	"\nNB: This parameter will not take any effect until the " \
	"VCL programs have been reloaded."

#define EXPERIMENTAL_TEXT \
	"\nNB: We do not know yet if it is a good idea to change " \
	"this parameter, or if the default value is even sensible.  " \
	"Caution is advised, and feedback is most welcome."

#define WIZARD_TEXT \
	"\nNB: Do not change this parameter, unless a developer tell " \
	"you to do so."

#define PROTECTED_TEXT \
	"\nNB: This parameter is protected and can not be changed."

#define MEMPOOL_TEXT							\
	"The three numbers are:\n"					\
	"   min_pool -- minimum size of free pool.\n"			\
	"   max_pool -- maximum size of free pool.\n"			\
	"   max_age -- max age of free element.\n"

/*
 * Remember to update varnishd.1 whenever you add / remove a parameter or
 * change its default value.
 * XXX: we should generate the relevant section of varnishd.1 from here.
 */
static const struct parspec input_parspec[] = {
	{ "user", tweak_user, NULL, 0, 0,
		"The unprivileged user to run as.  Setting this will "
		"also set \"group\" to the specified user's primary group.",
		MUST_RESTART,
		MAGIC_INIT_STRING },
	{ "group", tweak_group, NULL, 0, 0,
		"The unprivileged group to run as.",
		MUST_RESTART,
		MAGIC_INIT_STRING },
	{ "default_ttl", tweak_timeout_double, &mgt_param.default_ttl,
		0, UINT_MAX,
		"The TTL assigned to objects if neither the backend nor "
		"the VCL code assigns one.\n"
		"Objects already cached will not be affected by changes "
		"made until they are fetched from the backend again.\n"
		"To force an immediate effect at the expense of a total "
		"flush of the cache use \"ban obj.http.date ~ .\"",
		0,
		"120", "seconds" },
	{ "workspace_client",
		tweak_bytes_u, &mgt_param.workspace_client, 3072, UINT_MAX,
		"Bytes of HTTP protocol workspace for clients HTTP req/resp."
		"  If larger than 4k, use a multiple of 4k for VM efficiency.",
		DELAYED_EFFECT,
		"64k", "bytes" },
	{ "workspace_backend",
		tweak_bytes_u, &mgt_param.workspace_backend, 1024, UINT_MAX,
		"Bytes of HTTP protocol workspace for backend HTTP req/resp."
		"  If larger than 4k, use a multiple of 4k for VM efficiency.",
		DELAYED_EFFECT,
		"64k", "bytes" },
	{ "workspace_thread",
		tweak_bytes_u, &mgt_param.workspace_thread, 256, 8192,
		"Bytes of auxillary workspace per thread.\n"
		"This workspace is used for certain temporary data structures"
		" during the operation of a worker thread.\n"
		"One use is for the io-vectors for writing requests and"
		" responses to sockets, having too little space will"
		" result in more writev(2) system calls, having too much"
		" just wastes the space.\n",
		DELAYED_EFFECT,
		"2048", "bytes" },
	{ "http_req_hdr_len",
		tweak_bytes_u, &mgt_param.http_req_hdr_len,
		40, UINT_MAX,
		"Maximum length of any HTTP client request header we will "
		"allow.  The limit is inclusive its continuation lines.\n",
		0,
		"8k", "bytes" },
	{ "http_req_size",
		tweak_bytes_u, &mgt_param.http_req_size,
		256, UINT_MAX,
		"Maximum number of bytes of HTTP client request we will deal "
		"with.  This is a limit on all bytes up to the double blank "
		"line which ends the HTTP request.\n"
		"The memory for the request is allocated from the client "
		"workspace (param: workspace_client) and this parameter limits "
		"how much of that the request is allowed to take up.",
		0,
		"32k", "bytes" },
	{ "http_resp_hdr_len",
		tweak_bytes_u, &mgt_param.http_resp_hdr_len,
		40, UINT_MAX,
		"Maximum length of any HTTP backend response header we will "
		"allow.  The limit is inclusive its continuation lines.\n",
		0,
		"8k", "bytes" },
	{ "http_resp_size",
		tweak_bytes_u, &mgt_param.http_resp_size,
		256, UINT_MAX,
		"Maximum number of bytes of HTTP backend resonse we will deal "
		"with.  This is a limit on all bytes up to the double blank "
		"line which ends the HTTP request.\n"
		"The memory for the request is allocated from the worker "
		"workspace (param: thread_pool_workspace) and this parameter "
		"limits how much of that the request is allowed to take up.",
		0,
		"32k", "bytes" },
	{ "http_max_hdr", tweak_uint, &mgt_param.http_max_hdr, 32, 65535,
		"Maximum number of HTTP headers we will deal with in "
		"client request or backend reponses.  "
		"Note that the first line occupies five header fields.\n"
		"This parameter does not influence storage consumption, "
		"objects allocate exact space for the headers they store.\n",
		0,
		"64", "header lines" },
	{ "vsl_buffer",
		tweak_bytes_u, &mgt_param.vsl_buffer, 1024, UINT_MAX,
		"Bytes of (req-/backend-)workspace dedicated to buffering"
		" VSL records.\n"
		"At a bare minimum, this must be longer than"
		" the longest HTTP header to be logged.\n"
		"Setting this too high costs memory, setting it too low"
		" will cause more VSL flushes and likely increase"
		" lock-contention on the VSL mutex.\n"
		"Minimum is 1k bytes.",
		0,
		"4k", "bytes" },
	{ "shm_reclen",
		tweak_bytes_u, &mgt_param.shm_reclen, 16, 65535,
		"Maximum number of bytes in SHM log record.\n"
		"Maximum is 65535 bytes.",
		0,
		"255", "bytes" },
	{ "default_grace", tweak_timeout_double, &mgt_param.default_grace,
		0, UINT_MAX,
		"Default grace period.  We will deliver an object "
		"this long after it has expired, provided another thread "
		"is attempting to get a new copy.\n"
		"Objects already cached will not be affected by changes "
		"made until they are fetched from the backend again.\n",
		DELAYED_EFFECT,
		"10", "seconds" },
	{ "default_keep", tweak_timeout_double, &mgt_param.default_keep,
		0, UINT_MAX,
		"Default keep period.  We will keep a useless object "
		"around this long, making it available for conditional "
		"backend fetches.  "
		"That means that the object will be removed from the "
		"cache at the end of ttl+grace+keep.",
		DELAYED_EFFECT,
		"0", "seconds" },
	{ "timeout_idle", tweak_timeout_double, &mgt_param.timeout_idle,
		0, UINT_MAX,
		"Idle timeout for client connections.\n"
		"A connection is considered idle, until we receive"
		" a non-white-space character on it.",
		0,
		"5", "seconds" },
	{ "timeout_req", tweak_timeout_double, &mgt_param.timeout_req,
		0, UINT_MAX,
		"Max time to receive clients request header, measured"
		" from first non-white-space character to double CRNL.",
		0,
		"2", "seconds" },
	{ "expiry_sleep", tweak_timeout_double, &mgt_param.expiry_sleep, 0, 60,
		"How long the expiry thread sleeps when there is nothing "
		"for it to do.\n",
		0,
		"1", "seconds" },
	{ "pipe_timeout", tweak_timeout, &mgt_param.pipe_timeout, 0, 0,
		"Idle timeout for PIPE sessions. "
		"If nothing have been received in either direction for "
		"this many seconds, the session is closed.\n",
		0,
		"60", "seconds" },
	{ "send_timeout", tweak_timeout, &mgt_param.send_timeout, 0, 0,
		"Send timeout for client connections. "
		"If the HTTP response hasn't been transmitted in this many\n"
                "seconds the session is closed. \n"
		"See setsockopt(2) under SO_SNDTIMEO for more information.",
		DELAYED_EFFECT,
		"600", "seconds" },
	{ "idle_send_timeout", tweak_timeout, &mgt_param.idle_send_timeout,
		0, 0,
		"Time to wait with no data sent. "
		"If no data has been transmitted in this many\n"
                "seconds the session is closed. \n"
		"See setsockopt(2) under SO_SNDTIMEO for more information.",
		DELAYED_EFFECT,
		"60", "seconds" },
	{ "auto_restart", tweak_bool, &mgt_param.auto_restart, 0, 0,
		"Restart child process automatically if it dies.\n",
		0,
		"on", "bool" },
	{ "nuke_limit",
		tweak_uint, &mgt_param.nuke_limit, 0, UINT_MAX,
		"Maximum number of objects we attempt to nuke in order"
		"to make space for a object body.",
		EXPERIMENTAL,
		"50", "allocations" },
	{ "fetch_chunksize",
		tweak_bytes,
		    &mgt_param.fetch_chunksize, 4 * 1024, UINT_MAX,
		"The default chunksize used by fetcher. "
		"This should be bigger than the majority of objects with "
		"short TTLs.\n"
		"Internal limits in the storage_file module makes increases "
		"above 128kb a dubious idea.",
		EXPERIMENTAL,
		"128k", "bytes" },
	{ "fetch_maxchunksize",
		tweak_bytes,
		    &mgt_param.fetch_maxchunksize, 64 * 1024, UINT_MAX,
		"The maximum chunksize we attempt to allocate from storage. "
		"Making this too large may cause delays and storage "
		"fragmentation.\n",
		EXPERIMENTAL,
		"256m", "bytes" },
	{ "accept_filter", tweak_bool, &mgt_param.accept_filter, 0, 0,
		"Enable kernel accept-filters, if supported by the kernel.",
		MUST_RESTART,
		"on", "bool" },
	{ "listen_address", tweak_listen_address, NULL, 0, 0,
		"Whitespace separated list of network endpoints where "
		"Varnish will accept requests.\n"
		"Possible formats: host, host:port, :port",
		MUST_RESTART,
		":80" },
	{ "listen_depth", tweak_uint, &mgt_param.listen_depth, 0, UINT_MAX,
		"Listen queue depth.",
		MUST_RESTART,
		"1024", "connections" },
	{ "cli_buffer",
		tweak_bytes_u, &mgt_param.cli_buffer, 4096, UINT_MAX,
		"Size of buffer for CLI command input."
		"\nYou may need to increase this if you have big VCL files "
		"and use the vcl.inline CLI command.\n"
		"NB: Must be specified with -p to have effect.\n",
		0,
		"8k", "bytes" },
	{ "cli_limit",
		tweak_bytes_u, &mgt_param.cli_limit, 128, 99999999,
		"Maximum size of CLI response.  If the response exceeds"
		" this limit, the reponse code will be 201 instead of"
		" 200 and the last line will indicate the truncation.",
		0,
		"48k", "bytes" },
	{ "cli_timeout", tweak_timeout, &mgt_param.cli_timeout, 0, 0,
		"Timeout for the childs replies to CLI requests from "
		"the mgt_param.",
		0,
		"10", "seconds" },
	{ "ping_interval", tweak_uint, &mgt_param.ping_interval, 0, UINT_MAX,
		"Interval between pings from parent to child.\n"
		"Zero will disable pinging entirely, which makes "
		"it possible to attach a debugger to the child.",
		MUST_RESTART,
		"3", "seconds" },
	{ "lru_interval", tweak_timeout, &mgt_param.lru_timeout, 0, 0,
		"Grace period before object moves on LRU list.\n"
		"Objects are only moved to the front of the LRU "
		"list if they have not been moved there already inside "
		"this timeout period.  This reduces the amount of lock "
		"operations necessary for LRU list access.",
		EXPERIMENTAL,
		"2", "seconds" },
	{ "cc_command", tweak_string, &mgt_cc_cmd, 0, 0,
		"Command used for compiling the C source code to a "
		"dlopen(3) loadable object.  Any occurrence of %s in "
		"the string will be replaced with the source file name, "
		"and %o will be replaced with the output file name.",
		MUST_RELOAD,
		VCC_CC , NULL },
	{ "max_restarts", tweak_uint, &mgt_param.max_restarts, 0, UINT_MAX,
		"Upper limit on how many times a request can restart."
		"\nBe aware that restarts are likely to cause a hit against "
		"the backend, so don't increase thoughtlessly.\n",
		0,
		"4", "restarts" },
	{ "esi_syntax",
		tweak_uint, &mgt_param.esi_syntax, 0, UINT_MAX,
		"Bitmap controlling ESI parsing code:\n"
		"  0x00000001 - Don't check if it looks like XML\n"
		"  0x00000002 - Ignore non-esi elements\n"
		"  0x00000004 - Emit parsing debug records\n"
		"  0x00000008 - Force-split parser input (debugging)\n"
		"\n"
		"Use 0x notation and do the bitor in your head :-)\n",
		0,
		"0", "bitmap" },
	{ "max_esi_depth",
		tweak_uint, &mgt_param.max_esi_depth, 0, UINT_MAX,
		"Maximum depth of esi:include processing.\n",
		0,
		"5", "levels" },
	{ "connect_timeout", tweak_timeout_double,
		&mgt_param.connect_timeout,0, UINT_MAX,
		"Default connection timeout for backend connections. "
		"We only try to connect to the backend for this many "
		"seconds before giving up. "
		"VCL can override this default value for each backend and "
		"backend request.",
		0,
		"0.7", "s" },
	{ "first_byte_timeout", tweak_timeout_double,
		&mgt_param.first_byte_timeout,0, UINT_MAX,
		"Default timeout for receiving first byte from backend. "
		"We only wait for this many seconds for the first "
		"byte before giving up. A value of 0 means it will never time "
		"out. "
		"VCL can override this default value for each backend and "
		"backend request. This parameter does not apply to pipe.",
		0,
		"60", "s" },
	{ "between_bytes_timeout", tweak_timeout_double,
		&mgt_param.between_bytes_timeout,0, UINT_MAX,
		"Default timeout between bytes when receiving data from "
		"backend. "
		"We only wait for this many seconds between bytes "
		"before giving up. A value of 0 means it will never time out. "
		"VCL can override this default value for each backend request "
		"and backend request. This parameter does not apply to pipe.",
		0,
		"60", "s" },
	{ "acceptor_sleep_max", tweak_timeout_double,
		&mgt_param.acceptor_sleep_max, 0,  10,
		"If we run out of resources, such as file descriptors or "
		"worker threads, the acceptor will sleep between accepts.\n"
		"This parameter limits how long it can sleep between "
		"attempts to accept new connections.",
		EXPERIMENTAL,
		"0.050", "s" },
	{ "acceptor_sleep_incr", tweak_timeout_double,
		&mgt_param.acceptor_sleep_incr, 0,  1,
		"If we run out of resources, such as file descriptors or "
		"worker threads, the acceptor will sleep between accepts.\n"
		"This parameter control how much longer we sleep, each time "
		"we fail to accept a new connection.",
		EXPERIMENTAL,
		"0.001", "s" },
	{ "acceptor_sleep_decay", tweak_generic_double,
		&mgt_param.acceptor_sleep_decay, 0,  1,
		"If we run out of resources, such as file descriptors or "
		"worker threads, the acceptor will sleep between accepts.\n"
		"This parameter (multiplicatively) reduce the sleep duration "
		"for each succesfull accept. (ie: 0.9 = reduce by 10%)",
		EXPERIMENTAL,
		"0.900", "" },
	{ "clock_skew", tweak_uint, &mgt_param.clock_skew, 0, UINT_MAX,
		"How much clockskew we are willing to accept between the "
		"backend and our own clock.",
		0,
		"10", "s" },
	{ "prefer_ipv6", tweak_bool, &mgt_param.prefer_ipv6, 0, 0,
		"Prefer IPv6 address when connecting to backends which "
		"have both IPv4 and IPv6 addresses.",
		0,
		"off", "bool" },
	{ "session_max", tweak_uint,
		&mgt_param.max_sess, 1000, UINT_MAX,
		"Maximum number of sessions we will allocate from one pool "
		"before just dropping connections.\n"
		"This is mostly an anti-DoS measure, and setting it plenty "
		"high should not hurt, as long as you have the memory for "
		"it.\n",
		0,
		"100000", "sessions" },
	{ "timeout_linger", tweak_timeout_double, &mgt_param.timeout_linger,
		0, UINT_MAX,
		"How long time the workerthread lingers on an idle session "
		"before handing it over to the waiter.\n"
		"When sessions are reused, as much as half of all reuses "
		"happen within the first 100 msec of the previous request "
		"completing.\n"
		"Setting this too high results in worker threads not doing "
		"anything for their keep, setting it too low just means that "
		"more sessions take a detour around the waiter.",
		EXPERIMENTAL,
		"0.050", "seconds" },
	{ "log_local_address", tweak_bool, &mgt_param.log_local_addr, 0, 0,
		"Log the local address on the TCP connection in the "
		"SessionOpen VSL record.\n"
		"Disabling this saves a getsockname(2) system call "
		"per TCP connection.\n",
		0,
		"on", "bool" },
	{ "waiter", tweak_waiter, NULL, 0, 0,
		"Select the waiter kernel interface.\n",
		WIZARD | MUST_RESTART,
		WAITER_DEFAULT, NULL },
	{ "ban_dups", tweak_bool, &mgt_param.ban_dups, 0, 0,
		"Detect and eliminate duplicate bans.\n",
		0,
		"on", "bool" },
	{ "syslog_cli_traffic", tweak_bool, &mgt_param.syslog_cli_traffic, 0, 0,
		"Log all CLI traffic to syslog(LOG_INFO).\n",
		0,
		"on", "bool" },
	{ "ban_lurker_sleep", tweak_timeout_double,
		&mgt_param.ban_lurker_sleep, 0, UINT_MAX,
		"How long time does the ban lurker thread sleeps between "
		"successful attempts to push the last item up the ban "
		" list.  It always sleeps a second when nothing can be done.\n"
		"A value of zero disables the ban lurker.",
		0,
		"0.01", "s" },
	{ "saintmode_threshold", tweak_uint,
		&mgt_param.saintmode_threshold, 0, UINT_MAX,
		"The maximum number of objects held off by saint mode before "
		"no further will be made to the backend until one times out.  "
		"A value of 0 disables saintmode.",
		EXPERIMENTAL,
		"10", "objects" },
	{ "http_range_support", tweak_bool, &mgt_param.http_range_support, 0, 0,
		"Enable support for HTTP Range headers.\n",
		0,
		"on", "bool" },
	{ "http_gzip_support", tweak_bool, &mgt_param.http_gzip_support, 0, 0,
		"Enable gzip support. When enabled Varnish will compress "
		"uncompressed objects before they are stored in the cache. "
		"If a client does not support gzip encoding Varnish will "
		"uncompress compressed objects on demand. Varnish will also "
		"rewrite the Accept-Encoding header of clients indicating "
		"support for gzip to:\n"
		"  Accept-Encoding: gzip\n\n"
		"Clients that do not support gzip will have their "
		"Accept-Encoding header removed. For more information on how "
		"gzip is implemented please see the chapter on gzip in the "
		"Varnish reference.",
		EXPERIMENTAL,
		"on", "bool" },
	{ "gzip_level", tweak_uint, &mgt_param.gzip_level, 0, 9,
		"Gzip compression level: 0=debug, 1=fast, 9=best",
		0,
		"6", ""},
	{ "gzip_memlevel", tweak_uint, &mgt_param.gzip_memlevel, 1, 9,
		"Gzip memory level 1=slow/least, 9=fast/most compression.\n"
		"Memory impact is 1=1k, 2=2k, ... 9=256k.",
		0,
		"8", ""},
	{ "gzip_buffer",
		tweak_bytes_u, &mgt_param.gzip_buffer,
	        2048, UINT_MAX,
		"Size of malloc buffer used for gzip processing.\n"
		"These buffers are used for in-transit data,"
		" for instance gunzip'ed data being sent to a client."
		"Making this space to small results in more overhead,"
		" writes to sockets etc, making it too big is probably"
		" just a waste of memory.",
		EXPERIMENTAL,
		"32k", "bytes" },
	{ "shortlived", tweak_timeout_double,
		&mgt_param.shortlived, 0, UINT_MAX,
		"Objects created with TTL shorter than this are always "
		"put in transient storage.\n",
		0,
		"10.0", "s" },
	{ "critbit_cooloff", tweak_timeout_double,
		&mgt_param.critbit_cooloff, 60, 254,
		"How long time the critbit hasher keeps deleted objheads "
		"on the cooloff list.\n",
		WIZARD,
		"180.0", "s" },
	{ "vcl_dir", tweak_string, &mgt_vcl_dir, 0, 0,
		"Directory from which relative VCL filenames (vcl.load and "
		"include) are opened.",
		0,
#ifdef VARNISH_VCL_DIR
		VARNISH_VCL_DIR,
#else
		".",
#endif
		NULL },
	{ "vmod_dir", tweak_string, &mgt_vmod_dir, 0, 0,
		"Directory where VCL modules are to be found.",
		0,
#ifdef VARNISH_VMOD_DIR
		VARNISH_VMOD_DIR,
#else
		".",
#endif
		NULL },

	{ "vcc_err_unref", tweak_bool, &mgt_vcc_err_unref, 0, 0,
		"Unreferenced VCL objects result in error.\n",
		0,
		"on", "bool" },

	{ "vcc_allow_inline_c", tweak_bool, &mgt_vcc_allow_inline_c, 0, 0,
		"Allow inline C code in VCL.\n",
		0,
		"on", "bool" },

	{ "vcc_unsafe_path", tweak_bool, &mgt_vcc_unsafe_path, 0, 0,
		"Allow '/' in vmod & include paths.\n"
		"Allow 'import ... from ...'.\n",
		0,
		"on", "bool" },

	{ "pcre_match_limit", tweak_uint,
		&mgt_param.vre_limits.match,
		1, UINT_MAX,
		"The limit for the  number of internal matching function"
		" calls in a pcre_exec() execution.",
		0,
		"10000", ""},

	{ "pcre_match_limit_recursion", tweak_uint,
		&mgt_param.vre_limits.match_recursion,
		1, UINT_MAX,
		"The limit for the  number of internal matching function"
		" recursions in a pcre_exec() execution.",
		0,
		"10000", ""},

	{ "vsl_space", tweak_bytes,
		&mgt_param.vsl_space, 1024*1024, 0,
		"The amount of space to allocate for the VSL fifo buffer"
		" in the VSM memory segment."
		"  If you make this too small, varnish{ncsa|log} etc will"
		" not be able to keep up."
		"  Making it too large just costs memory resources.",
		MUST_RESTART,
		"80M", "bytes"},

	{ "vsm_space", tweak_bytes,
		&mgt_param.vsm_space, 1024*1024, 0,
		"The amount of space to allocate for stats counters"
		" in the VSM memory segment."
		"  If you make this too small, some counters will be"
		" invisible."
		"  Making it too large just costs memory resources.",
		MUST_RESTART,
		"1M", "bytes"},

	{ "busyobj_worker_cache", tweak_bool,
		&mgt_param.bo_cache, 0, 0,
		"Cache free busyobj per worker thread."
		"Disable this if you have very high hitrates and want"
		"to save the memory of one busyobj per worker thread.",
		0,
		"false", ""},

	{ "pool_vbc", tweak_poolparam, &mgt_param.vbc_pool, 0, 10000,
		"Parameters for backend connection memory pool.\n"
		MEMPOOL_TEXT,
		0,
		"10,100,10", ""},

	{ "pool_req", tweak_poolparam, &mgt_param.req_pool, 0, 10000,
		"Parameters for per worker pool request memory pool.\n"
		MEMPOOL_TEXT,
		0,
		"10,100,10", ""},
	{ "pool_sess", tweak_poolparam, &mgt_param.sess_pool, 0, 10000,
		"Parameters for per worker pool session memory pool.\n"
		MEMPOOL_TEXT,
		0,
		"10,100,10", ""},
	{ "pool_vbo", tweak_poolparam, &mgt_param.vbo_pool, 0, 10000,
		"Parameters for backend object fetch memory pool.\n"
		MEMPOOL_TEXT,
		0,
		"10,100,10", ""},

	{ "obj_readonly", tweak_bool, &mgt_param.obj_readonly, 0, 0,
		"If set, we do not update obj.hits and obj.lastuse to"
		"avoid dirtying VM pages associated with cached objects.",
		0,
		"false", ""},

	{ NULL, NULL, NULL }
};

/*--------------------------------------------------------------------*/

#define WIDTH 76

static void
mcf_wrap(struct cli *cli, const char *text)
{
	const char *p, *q;

	/* Format text to COLUMNS width */
	for (p = text; *p != '\0'; ) {
		q = strchr(p, '\n');
		if (q == NULL)
			q = strchr(p, '\0');
		if (q > p + WIDTH - margin) {
			q = p + WIDTH - margin;
			while (q > p && *q != ' ')
				q--;
			AN(q);
		}
		VCLI_Out(cli, "%*s %.*s\n", margin, "", (int)(q - p), p);
		p = q;
		if (*p == ' ' || *p == '\n')
			p++;
	}
}

void
mcf_param_show(struct cli *cli, const char * const *av, void *priv)
{
	int i;
	const struct parspec *pp;
	int lfmt;

	(void)priv;
	if (av[2] == NULL || strcmp(av[2], "-l"))
		lfmt = 0;
	else
		lfmt = 1;
	for (i = 0; i < nparspec; i++) {
		pp = parspecs[i];
		if (av[2] != NULL && !lfmt && strcmp(pp->name, av[2]))
			continue;
		VCLI_Out(cli, "%-*s ", margin, pp->name);
		if (pp->func == NULL) {
			VCLI_Out(cli, "Not implemented.\n");
			if (av[2] != NULL && !lfmt)
				return;
			else
				continue;
		}
		pp->func(cli, pp, NULL);
		if (pp->units != NULL && *pp->units != '\0')
			VCLI_Out(cli, " [%s]\n", pp->units);
		else
			VCLI_Out(cli, "\n");
		if (av[2] != NULL) {
			VCLI_Out(cli, "%-*s Default is %s\n",
			    margin, "", pp->def);
			mcf_wrap(cli, pp->descr);
			if (pp->flags & DELAYED_EFFECT)
				mcf_wrap(cli, DELAYED_EFFECT_TEXT);
			if (pp->flags & EXPERIMENTAL)
				mcf_wrap(cli, EXPERIMENTAL_TEXT);
			if (pp->flags & MUST_RELOAD)
				mcf_wrap(cli, MUST_RELOAD_TEXT);
			if (pp->flags & MUST_RESTART)
				mcf_wrap(cli, MUST_RESTART_TEXT);
			if (pp->flags & WIZARD)
				mcf_wrap(cli, WIZARD_TEXT);
			if (pp->flags & PROTECTED)
				mcf_wrap(cli, PROTECTED_TEXT);
			if (!lfmt)
				return;
			else
				VCLI_Out(cli, "\n");
		}
	}
	if (av[2] != NULL && !lfmt) {
		VCLI_SetResult(cli, CLIS_PARAM);
		VCLI_Out(cli, "Unknown parameter \"%s\".", av[2]);
	}
}

/*--------------------------------------------------------------------
 * Mark paramters as protected
 */

void
MCF_ParamProtect(struct cli *cli, const char *args)
{
	char **av;
	struct parspec *pp;
	int i, j;

	av = VAV_Parse(args, NULL, ARGV_COMMA);
	if (av[0] != NULL) {
		VCLI_Out(cli, "Parse error: %s", av[0]);
		VCLI_SetResult(cli, CLIS_PARAM);
		VAV_Free(av);
		return;
	}
	for (i = 1; av[i] != NULL; i++) {
		for (j = 0; j < nparspec; j++)
			if (!strcmp(parspecs[j]->name, av[i]))
				break;
		if (j == nparspec) {
			VCLI_Out(cli, "Unknown parameter %s", av[i]);
			VCLI_SetResult(cli, CLIS_PARAM);
			VAV_Free(av);
			return;
		}
		pp = calloc(sizeof *pp, 1L);
		XXXAN(pp);
		memcpy(pp, parspecs[j], sizeof *pp);
		pp->flags |= PROTECTED;
		parspecs[j] = pp;
	}
	VAV_Free(av);
}

/*--------------------------------------------------------------------*/

void
MCF_ParamSet(struct cli *cli, const char *param, const char *val)
{
	const struct parspec *pp;

	pp = mcf_findpar(param);
	if (pp == NULL) {
		VCLI_SetResult(cli, CLIS_PARAM);
		VCLI_Out(cli, "Unknown parameter \"%s\".", param);
		return;
	}
	if (pp->flags & PROTECTED) {
		VCLI_SetResult(cli, CLIS_AUTH);
		VCLI_Out(cli, "parameter \"%s\" is protected.", param);
		return;
	}
	pp->func(cli, pp, val);

	if (cli->result == CLIS_OK && heritage.param != NULL)
		*heritage.param = mgt_param;

	if (cli->result != CLIS_OK) {
		VCLI_Out(cli, "(attempting to set param %s to %s)\n",
		    pp->name, val);
	} else if (child_pid >= 0 && pp->flags & MUST_RESTART) {
		VCLI_Out(cli, "Change will take effect"
		    " when child is restarted");
	} else if (pp->flags & MUST_RELOAD) {
		VCLI_Out(cli, "Change will take effect"
		    " when VCL script is reloaded");
	}
}


/*--------------------------------------------------------------------*/

void
mcf_param_set(struct cli *cli, const char * const *av, void *priv)
{

	(void)priv;
	MCF_ParamSet(cli, av[2], av[3]);
}

/*--------------------------------------------------------------------
 * Add a group of parameters to the global set and sort by name.
 */

static int
parspec_cmp(const void *a, const void *b)
{
	struct parspec * const * pa = a;
	struct parspec * const * pb = b;
	return (strcmp((*pa)->name, (*pb)->name));
}

static void
MCF_AddParams(const struct parspec *ps)
{
	const struct parspec *pp;
	int n;

	n = 0;
	for (pp = ps; pp->name != NULL; pp++) {
		if (mcf_findpar(pp->name) != NULL)
			fprintf(stderr, "Duplicate param: %s\n", pp->name);
		if (strlen(pp->name) + 1 > margin)
			margin = strlen(pp->name) + 1;
		n++;
	}
	parspecs = realloc(parspecs, (1L + nparspec + n) * sizeof *parspecs);
	XXXAN(parspecs);
	for (pp = ps; pp->name != NULL; pp++)
		parspecs[nparspec++] = pp;
	parspecs[nparspec] = NULL;
	qsort (parspecs, nparspec, sizeof parspecs[0], parspec_cmp);
}

/*--------------------------------------------------------------------
 * Set defaults for all parameters
 */

static void
MCF_SetDefaults(struct cli *cli)
{
	const struct parspec *pp;
	int i;

	for (i = 0; i < nparspec; i++) {
		pp = parspecs[i];
		if (cli != NULL)
			VCLI_Out(cli,
			    "Set Default for %s = %s\n", pp->name, pp->def);
		pp->func(cli, pp, pp->def);
		if (cli != NULL && cli->result != CLIS_OK)
			return;
	}
}

/*--------------------------------------------------------------------*/

void
MCF_ParamInit(struct cli *cli)
{

	MCF_AddParams(input_parspec);
	MCF_AddParams(WRK_parspec);
	MCF_AddParams(VSL_parspec);

	/* XXX: We do this twice, to get past any interdependencies */
	MCF_SetDefaults(NULL);
	MCF_SetDefaults(cli);
}

/*--------------------------------------------------------------------*/

void
MCF_DumpRstParam(void)
{
	const struct parspec *pp;
	const char *p, *q;
	int i;

	printf("\n.. The following is the autogenerated "
	    "output from varnishd -x dumprstparam\n\n");
	for (i = 0; i < nparspec; i++) {
		pp = parspecs[i];
		printf("%s\n", pp->name);
		if (pp->units != NULL && *pp->units != '\0')
			printf("\t- Units: %s\n", pp->units);
		printf("\t- Default: %s\n",
		    strcmp(pp->def,MAGIC_INIT_STRING) == 0 ? "magic" : pp->def);
		/*
		 * XXX: we should mark the params with one/two flags
		 * XXX: that say if ->min/->max are valid, so we
		 * XXX: can emit those also in help texts.
		 */
		if (pp->flags) {
			printf("\t- Flags: ");
			q = "";
			if (pp->flags & DELAYED_EFFECT) {
				printf("%sdelayed", q);
				q = ", ";
			}
			if (pp->flags & MUST_RESTART) {
				printf("%smust_restart", q);
				q = ", ";
			}
			if (pp->flags & MUST_RELOAD) {
				printf("%smust_reload", q);
				q = ", ";
			}
			if (pp->flags & EXPERIMENTAL) {
				printf("%sexperimental", q);
				q = ", ";
			}
			printf("\n");
		}
		printf("\n\t");
		for (p = pp->descr; *p; p++) {
			if (*p == '\n' && p[1] =='\0')
				break;
			if (*p == '\n' && p[1] =='\n') {
				printf("\n\n\t");
				p++;
			} else if (*p == '\n') {
				printf("\n\t");
			} else if (*p == ':' && p[1] == '\n') {
				/*
				 * Start of definition list,
				 * use RSTs code mode for this
				 */
				printf("::\n");
			} else {
				printf("%c", *p);
			}
		}
		printf("\n\n");
	}
	printf("\n");
}
