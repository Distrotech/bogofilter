/* find_home_user.c -- library function to figure out the home dir of given user */

/* (C) 2002 by Matthias Andree <matthias.andree@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details, it is in the file named
 * COPYING.
 */

/* $Id$ */

#include <stdlib.h>

#include "system.h"
#include <pwd.h>
#include <sys/types.h>

#include "find_home.h"

/* This function will try to figure out the home directory of the user
 * whose name is given as argument.
 *
 * This function returns NULL in case of failure.
 */
const char *find_home_user(const char *username) {
    struct passwd *pw;

    pw = getpwnam(username);
    if (pw != NULL) {
	return pw -> pw_dir;
    }
    return NULL;
}
