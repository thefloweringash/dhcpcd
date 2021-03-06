/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Privilege Separation for dhcpcd, BSD driver
 * Copyright (c) 2006-2020 Roy Marples <roy@marples.name>
 * All rights reserved

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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/ioctl.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "dhcpcd.h"
#include "logerr.h"
#include "privsep.h"

static ssize_t
ps_root_doioctldom(int domain, unsigned long req, void *data, size_t len)
{
	int s, err;

	s = socket(domain, SOCK_DGRAM, 0);
	if (s == -1)
		return -1;
	err = ioctl(s, req, data, len);
	close(s);
	return err;
}

#ifdef HAVE_PLEDGE
static ssize_t
ps_root_doindirectioctl(unsigned long req, void *data, size_t len)
{
	char *p = data;
	struct ifreq ifr = { .ifr_flags = 0 };
	ssize_t err;

	if (len < IFNAMSIZ) {
		errno = EINVAL;
		return -1;
	}

	strlcpy(ifr.ifr_name, p, IFNAMSIZ);
	ifr.ifr_data = p + IFNAMSIZ;
	err = ps_root_doioctldom(PF_INET, req, &ifr, sizeof(ifr));
	if (err != -1)
		memmove(data, ifr.ifr_data, len - IFNAMSIZ);
	return err;
}
#endif

static ssize_t
ps_root_doroute(void *data, size_t len)
{
	int s;
	ssize_t err;

	s = socket(PF_ROUTE, SOCK_RAW, 0);
	if (s != -1)
		err = write(s, data, len);
	else
		err = -1;
	if (s != -1)
		close(s);
	return err;
}

ssize_t
ps_root_os(struct ps_msghdr *psm, struct msghdr *msg)
{
	struct iovec *iov = msg->msg_iov;
	void *data = iov->iov_base;
	size_t len = iov->iov_len;

	switch (psm->ps_cmd) {
	case PS_IOCTLLINK:
		return ps_root_doioctldom(PF_LINK, psm->ps_flags, data, len);
	case PS_IOCTL6:
		return ps_root_doioctldom(PF_INET6, psm->ps_flags, data, len);
#ifdef HAVE_PLEDGE
	case PS_IOCTLINDIRECT:
		return ps_root_doindirectioctl(psm->ps_flags, data, len);
	case PS_IP6FORWARDING:
		return ip6_forwarding(NULL);
#endif
	case PS_ROUTE:
		return ps_root_doroute(data, len);
	default:
		errno = ENOTSUP;
		return -1;
	}
}

static ssize_t
ps_root_ioctldom(struct dhcpcd_ctx *ctx, uint16_t domain, unsigned long request,
    void *data, size_t len)
{

	if (ps_sendcmd(ctx, ctx->ps_root_fd, domain,
	    request, data, len) == -1)
		return -1;
	return ps_root_readerror(ctx, data, len);
}

ssize_t
ps_root_ioctllink(struct dhcpcd_ctx *ctx, unsigned long request,
    void *data, size_t len)
{

	return ps_root_ioctldom(ctx, PS_IOCTLLINK, request, data, len);
}

ssize_t
ps_root_ioctl6(struct dhcpcd_ctx *ctx, unsigned long request,
    void *data, size_t len)
{

	return ps_root_ioctldom(ctx, PS_IOCTL6, request, data, len);
}

#ifdef HAVE_PLEDGE
ssize_t
ps_root_indirectioctl(struct dhcpcd_ctx *ctx, unsigned long request,
    const char *ifname, void *data, size_t len)
{
	char buf[PS_BUFLEN];

	strlcpy(buf, ifname, IFNAMSIZ);
	memcpy(buf + IFNAMSIZ, data, len);
	if (ps_sendcmd(ctx, ctx->ps_root_fd, PS_IOCTLINDIRECT,
	    request, buf, IFNAMSIZ + len) == -1)
		return -1;
	return ps_root_readerror(ctx, data, len);
}

ssize_t
ps_root_ip6forwarding(struct dhcpcd_ctx *ctx)
{

	if (ps_sendcmd(ctx, ctx->ps_root_fd,
	    PS_IP6FORWARDING, 0, NULL, 0) == -1)
		return -1;
	return ps_root_readerror(ctx, NULL, 0);
}
#endif

ssize_t
ps_root_route(struct dhcpcd_ctx *ctx, void *data, size_t len)
{

	if (ps_sendcmd(ctx, ctx->ps_root_fd, PS_ROUTE, 0, data, len) == -1)
		return -1;
	return ps_root_readerror(ctx, data, len);
}
