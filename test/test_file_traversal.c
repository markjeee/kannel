/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2014 Kannel Group  
 * Copyright (c) 1998-2001 WapIT Ltd.   
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met: 
 * 
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer. 
 * 
 * 2. Redistributions in binary form must reproduce the above copyright 
 *    notice, this list of conditions and the following disclaimer in 
 *    the documentation and/or other materials provided with the 
 *    distribution. 
 * 
 * 3. The end-user documentation included with the redistribution, 
 *    if any, must include the following acknowledgment: 
 *       "This product includes software developed by the 
 *        Kannel Group (http://www.kannel.org/)." 
 *    Alternately, this acknowledgment may appear in the software itself, 
 *    if and wherever such third-party acknowledgments normally appear. 
 * 
 * 4. The names "Kannel" and "Kannel Group" must not be used to 
 *    endorse or promote products derived from this software without 
 *    prior written permission. For written permission, please  
 *    contact org@kannel.org. 
 * 
 * 5. Products derived from this software may not be called "Kannel", 
 *    nor may "Kannel" appear in their name, without prior written 
 *    permission of the Kannel Group. 
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES 
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
 * DISCLAIMED.  IN NO EVENT SHALL THE KANNEL GROUP OR ITS CONTRIBUTORS 
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,  
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT  
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR  
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,  
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE  
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,  
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 * ==================================================================== 
 * 
 * This software consists of voluntary contributions made by many 
 * individuals on behalf of the Kannel Group.  For more information on  
 * the Kannel Group, please see <http://www.kannel.org/>. 
 * 
 * Portions of this software are based upon software originally written at  
 * WapIT Ltd., Helsinki, Finland for the Kannel project.  
 */ 

/*
 * test_file_traversal.c - simple file traversal testing
 */

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>

#include "gwlib/gwlib.h"

#ifdef HAVE_NFTW
#include <ftw.h>
#endif


static Counter *counter;


/*
 * This callback function for use with for_each_file() will unlink
 * the file, and hence removing all regular files within the DLR spool.
 */
#ifdef HAVE_NFTW
static int count_file2(const char *filename, const struct stat *sb, int tflag, struct FTW *ftwbuf)
{
    /* we need to check here if we have a regular file. */
    if (tflag != FTW_F)
    	return 0;

    counter_increase(counter);

    return 0;
}
#endif


static int count_file(const char *filename, const struct stat *sb, int tflag, void *ftwbuf)
{
	counter_increase(counter);

    return 0;
}


/*
 * The function traverses a directory structure and calls a callback
 * function for each regular file within that directory structure.
 */
#ifdef HAVE_NFTW
static int for_each_file2(const Octstr *dir_s, int ignore_err,
		                  int(*cb)(const char *, const struct stat *, int, struct FTW *))
{
	 int ret;

	 ret = nftw(octstr_get_cstr(dir_s), cb, 20, FTW_PHYS);

	 return ret;
}
#endif

static int for_each_file(const Octstr *dir_s, int ignore_err,
		                 int(*cb)(const char *, const struct stat *, int, void *))
{
    DIR *dir;
    struct dirent *ent;
    int ret = 0;
#ifndef _DIRENT_HAVE_D_TYPE
    struct stat stat;
#endif

    if ((dir = opendir(octstr_get_cstr(dir_s))) == NULL) {
        error(errno, "Could not open directory `%s'", octstr_get_cstr(dir_s));
        return -1;
    }
    while ((ent = readdir(dir)) != NULL) {
        Octstr *filename;
        if (!(strcmp((char*)ent->d_name, "." ) != 0 && strcmp((char*)ent->d_name, ".." ) != 0))
        	continue;
        filename = octstr_format("%S/%s", dir_s, ent->d_name);
#ifdef _DIRENT_HAVE_D_TYPE
        if (ent->d_type == DT_DIR && for_each_file(filename, ignore_err, cb) == -1) {
        	ret = -1;
        } else if (ent->d_type == DT_REG && cb != NULL) {
            cb(octstr_get_cstr(filename), NULL, 0, NULL);
        }
#else
        if (lstat(octstr_get_cstr(filename), &stat) == -1) {
            if (!ignore_err)
                error(errno, "Could not get stat for `%s'", octstr_get_cstr(filename));
            ret = -1;
        } else if (S_ISDIR(stat.st_mode) && for_each_file(filename, ignore_err, cb) == -1) {
            ret = -1;
        } else if (S_ISREG(stat.st_mode) && cb != NULL) {
            cb(octstr_get_cstr(filename), &stat, 0, NULL);
        }
#endif
        octstr_destroy(filename);
        if (ret == -1 && ignore_err)
            ret = 0;
        else if (ret == -1)
            break;
    }
    closedir(dir);

    return ret;
}


int main(int argc, char **argv)
{
	Octstr *os1;
	Octstr *os2;
	time_t start, diff;

    gwlib_init();

    os1 = octstr_create(argv[1]);
    os2 = octstr_create(argv[2]);

    counter = counter_create();
    start = time(NULL);
    for_each_file(os1, 1, count_file);
    diff = (time(NULL) - start);
    debug("",0,"file count: %ld in %lds", (long) counter_value(counter), (long) diff);

#ifdef HAVE_NFTW
    counter_set(counter, 0);
    start = time(NULL);
    for_each_file2(os2, 1, count_file2);
    diff = (time(NULL) - start);
    debug("",0,"file count: %ld in %lds", (long) counter_value(counter), (long) diff);
#endif

    counter_destroy(counter);
    octstr_destroy(os1);
    octstr_destroy(os2);
    gwlib_shutdown();
	return 0;
}
