/*
 * Copyright (c) 2007
 *                    Eino Tuominen <eino@utu.fi>
 *                    Antti Siira <antti@utu.fi>
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

#include "common.h"
#include "proto_sjsms.h"
#include "srvutils.h"
#include "worker.h"
#include "utils.h"

#include <libmilter/mfapi.h>

struct private_ctx_s {
	char *client_address;
	char *sender;
};	

#define MILTER_PRIVATE	((struct private_ctx_s *) smfi_getpriv(milter_ctx))

/* internal functions */
sfsistat mlfi_connect(SMFICTX *milter_ctx, char *hostname, _SOCK_ADDR *hostaddr);
sfsistat mlfi_envfrom(SMFICTX *milter_ctx, char **argv);
sfsistat mlfi_envrcpt(SMFICTX *milter_ctx, char **argv);
sfsistat mlfi_close(SMFICTX *milter_ctx);

struct smfiDesc grossfilter =
{
	"Gross",		/* filter name */
	SMFI_VERSION,		/* version code -- do not change */
	0,			/* flags */
	mlfi_connect,		/* connection info filter */
	NULL,			/* SMTP HELO command filter */
	mlfi_envfrom,		/* envelope sender filter */
	mlfi_envrcpt,		/* envelope recipient filter */
	NULL,			/* header filter */
	NULL,			/* end of header */
	NULL,			/* body block filter */
	NULL,			/* end of message */
	NULL,			/* message aborted */
	mlfi_close		/* connection cleanup */
};

sfsistat
mlfi_connect(SMFICTX *milter_ctx, char *hostname, _SOCK_ADDR *hostaddr)
{
	struct private_ctx_s *priv;
	struct sockaddr_in *client_saddr;
	char *caddr;

	logstr(GLOG_INSANE, "milter: connect");

	priv = Malloc(sizeof(*priv));
	bzero(priv, sizeof(*priv));

	client_saddr = (struct sockaddr_in *)hostaddr;
	if (client_saddr->sin_family != AF_INET) {
		/* not supported */
		return SMFIS_CONTINUE;
	}
	
	caddr = inet_ntoa(client_saddr->sin_addr);
	priv->client_address = strdup(caddr);
	smfi_setpriv(milter_ctx, priv);

	return SMFIS_CONTINUE;
}

sfsistat
mlfi_envfrom(SMFICTX *milter_ctx, char **argv)
{
	struct private_ctx_s *priv = MILTER_PRIVATE;

	logstr(GLOG_INSANE, "milter: envfrom");

	priv->sender = strdup(argv[0]);

	return SMFIS_CONTINUE;
}

sfsistat
mlfi_envrcpt(SMFICTX *milter_ctx, char **argv)
{
	struct private_ctx_s *priv = MILTER_PRIVATE;
	grey_tuple_t *tuple;
	int status;

	logstr(GLOG_INSANE, "milter: envrcpt");

	tuple = request_new();

	tuple->sender = strdup(priv->sender);
	tuple->recipient = strdup(argv[0]);
	tuple->client_address = strdup(priv->client_address);

	status = test_tuple(tuple, NULL);
	request_unlink(tuple);

	if (STATUS_GREY == status)
		return SMFIS_TEMPFAIL;

	return SMFIS_CONTINUE;
}

sfsistat
mlfi_close(SMFICTX *milter_ctx) 
{
	struct private_ctx_s *priv = MILTER_PRIVATE;

	logstr(GLOG_INSANE, "milter: close");

	if (priv) {
		if (priv->sender)
			free(priv->sender);
		if (priv->client_address)
			free(priv->client_address);
		free(priv);
		smfi_setpriv(milter_ctx, NULL);
	}

	return SMFIS_CONTINUE;
}

void
milter_init()
{
	int ret;
	
	ret = smfi_setconn(ctx->config.milter.listen);
	if (MI_FAILURE == ret)
		daemon_shutdown(1, "smfi_setconn failed");
	
	if (strncasecmp(ctx->config.milter.listen, "unix:", 5) == 0)
		unlink(ctx->config.milter.listen + 5);
	if (strncasecmp(ctx->config.milter.listen, "local:", 5) == 0)
		unlink(ctx->config.milter.listen + 6);

	ret = smfi_register(grossfilter);
	if (MI_FAILURE == ret)
		daemon_shutdown(1, "smfi_register failed");

	smfi_main();
}
