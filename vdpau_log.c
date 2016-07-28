/*
 * Copyright (c) 2015-2016 Andreas Baierl <ichgeh@imkreisrum.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <stdarg.h>
#include <stdio.h>
#include "vdpau_private.h"

int LOGLEVEL = LINFO;

#ifdef DEBUG
void VDPAU_LOG(int level, char *fmt, ...)
{
	if (level <= LOGLEVEL)
	{
		va_list ap;
		va_start(ap, fmt);
		fprintf(stderr, "[VDPAU] ");
		switch (level) {
		case LFATAL:
		    fprintf(stderr, "FATAL:   ");
		    break;
		case LERR:
		    fprintf(stderr, "ERROR:   ");
		    break;
		case LWARN:
		    fprintf(stderr, "WARNING: ");
		    break;
		case LINFO:
		    fprintf(stderr, "INFO:    ");
		    break;
		case LDBG:
		    fprintf(stderr, "DEBUG:   ");
		    break;
		case LALL:
		    fprintf(stderr, "DBG_ALL: ");
		    break;
		default:
		    fprintf(stderr, ":        ");
		    break;
		}
		vfprintf(stderr, fmt, ap);
		fprintf(stderr, "\n");
		va_end(ap);
	}
}
#else
void VDPAU_LOG(int level, char *fmt, ...)
{
	return;
}
#endif
