// SPDX-License-Identifier: MIT
/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2018 Wim Taymans
 *
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifndef HAVE_GETRANDOM
ssize_t getrandom(void *buf, size_t buflen, unsigned int flags)
{
	ssize_t bytes;
	int fd;

	fd = open("/dev/urandom", O_CLOEXEC);
	if (fd < 0)
		return -1;

	bytes = read(fd, buf, buflen);
	close(fd);

	return bytes;
}
#endif
