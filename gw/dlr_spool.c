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
 * gw/dlr_spool.c
 *
 * Implementation of handling delivery reports (DLRs) in a spool directory.
 * This ensures we have disk persistence of the temporary DLR data, but also
 * doesn't force users to use third party RDMS systems for the storage.
 *
 * Stipe Tolj <stolj at kannel dot org>
 */

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>

#include "gwlib/gwlib.h"
#include "dlr_p.h"
#include "sms.h"
#include "bb_store.h"

#ifdef HAVE_NFTW
#include <ftw.h>
#endif

/* some standard constant for the hash functions */
#define MD5_DIGEST_LEN      32
#define SHA1_DIGEST_LEN     40
#ifdef HAVE_LIBSSL
# define OUR_DIGEST_LEN		SHA1_DIGEST_LEN
#else
# define OUR_DIGEST_LEN		MD5_DIGEST_LEN
#endif

/* how much sub-directories will we allow? */
#define MAX_DIRS 100

/*
 * Define this macro in order to get verified counter
 * values while start-up. This will decrease start-up performance,
 * especially for large scale DLR spools
 */
/*
#define VERIFIED 1
*/

/*
 * Our DLR spool location.
 */
static Octstr *spool_dir = NULL;

/*
 * Internal counter keeping track of how many items we have.
 */
static Counter *counter;


/********************************************************************
 * Hashing functions.
 */

/*
 * Calculates a SHA1 hash digest, if openssl library was available
 * on the system, or a less secure MD5 hash digest.
 */
static Octstr *our_hash_func(Octstr *os)
{
#ifdef HAVE_LIBSSL
    /* use openssl's SHA1 */
    EVP_MD_CTX mdctx;
    const EVP_MD *md;
    unsigned char md_value[EVP_MAX_MD_SIZE];
    unsigned int md_len;
    Octstr *ret;

    md = EVP_get_digestbyname("sha1");

    EVP_MD_CTX_init(&mdctx);
    EVP_DigestInit_ex(&mdctx, md, NULL);
    EVP_DigestUpdate(&mdctx, octstr_get_cstr(os), octstr_len(os));
    EVP_DigestFinal_ex(&mdctx, md_value, &md_len);
    EVP_MD_CTX_cleanup(&mdctx);

    ret = octstr_create_from_data((char*) md_value, md_len);
    octstr_binary_to_hex(ret, 0);
    return ret;
#else
    /* fallback to our own MD5 if we don't have openssl available */
    return md5digest(os);
#endif
}


/********************************************************************
 * Disk IO operations.
 */

/*
 * This callback function for use with for_each_file() will really
 * try to load the DLR message file from the DLR spool and try to
 * unpack it. This ensures we get a real accurate counter value at
 * startup time.
 */
#ifdef VERIFIED
#ifdef HAVE_NFTW
static int verified_file(const char *filename, const struct stat *sb, int tflag, struct FTW *ftwbuf)
{
    Octstr *os;
    Msg *msg;

    /* we need to check here if we have a regular file. */
    if (tflag != FTW_F)
    	return 0;
#else
static int verified_file(const char *filename, const struct stat *sb, int tflag, void *ftwbuf)
{
	Octstr *os;
	Msg *msg;
#endif

    if ((os = octstr_read_file(filename)) == NULL) {
    	return -1;
    }

    if ((msg = store_msg_unpack(os)) == NULL) {
        error(0, "Could not unpack DLR message `%s'", filename);
    	octstr_destroy(os);
    	return -1;
    }

    /* we could load and unpack, so this is verified */
    counter_increase(counter);
    octstr_destroy(os);
    msg_destroy(msg);

    return 0;
}
#endif


/*
 * This callback function for use with for_each_file() will be more
 * optimistic and simply account and file occurrences, without trying
 * to load and unpack the DLR message. This is less accurate, but
 * faster for very large DLR spools.
 */
#ifdef HAVE_NFTW
static int non_verified_file(const char *filename, const struct stat *sb, int tflag, struct FTW *ftwbuf)
{
    /* we need to check here if we have a regular file. */
    if (tflag != FTW_F)
    	return 0;
#else
static int non_verified_file(const char *filename, const struct stat *sb, int tflag, void *ftwbuf)
{
#endif

    counter_increase(counter);

    return 0;
}

/*
 * This callback function for use with for_each_file() will unlink
 * the file, and hence removing all regular files within the DLR spool.
 */
#ifdef HAVE_NFTW
static int unlink_file(const char *filename, const struct stat *sb, int tflag, struct FTW *ftwbuf)
{
    /* we need to check here if we have a regular file. */
    if (tflag != FTW_F)
    	return 0;
#else
static int unlink_file(const char *filename, const struct stat *sb, int tflag, void *ftwbuf)
{
#endif

    /* remove the file from the file system */
    if (unlink(filename) == -1) {
        error(errno, "Could not unlink file `%s'.", filename);
    }

    return 0;
}


/*
 * The function traverses a directory structure and calls a callback
 * function for each regular file within that directory structure.
 */
#ifdef HAVE_NFTW
static int for_each_file(const Octstr *dir_s, int ignore_err,
		                 int(*cb)(const char *, const struct stat *, int, struct FTW *))
{
	 int ret;

	 ret = nftw(octstr_get_cstr(dir_s), cb, 20, FTW_PHYS);

	 return ret;
}
#else
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
        } else if (S_ISREG(stat.st_mode) && cb != NULL)
            cb(octstr_get_cstr(filename), &stat, 0, NULL);
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
#endif


static Octstr *get_msg_filename(const Octstr *dir_s, const Octstr *hash, const Octstr *dst)
{
    Octstr *ret;
    DIR *dir;
    struct dirent *ent;

    if ((dir = opendir(octstr_get_cstr(dir_s))) == NULL) {
        error(errno, "Could not open directory `%s'", octstr_get_cstr(dir_s));
        return NULL;
    }

    while ((ent = readdir(dir)) != NULL) {
    	Octstr *fname = octstr_create((char*)ent->d_name);

    	if (octstr_ncompare(fname, hash, OUR_DIGEST_LEN) == 0) {
    		Octstr *addr;
    		long addr_len, pos;

    		/* this is a candidate */
    		if (dst == NULL)
    			goto found;

    		/* check for the destination address suffix part */
    		if ((addr_len = (octstr_len(fname) - OUR_DIGEST_LEN)) < 0 ||
    				(pos = (addr_len - octstr_len(dst))) < 0) {
    			octstr_destroy(fname);
    			continue;
    		}
    		addr = octstr_copy(fname, OUR_DIGEST_LEN, addr_len);

    		/* if not found, then bail out*/
    		if (octstr_search(addr, dst, pos) == -1) {
    			octstr_destroy(addr);
    			octstr_destroy(fname);
    			continue;
    		}
    		octstr_destroy(addr);
found:
			/* found it */
			closedir(dir);
			ret = octstr_format("%S/%S", dir_s, fname);
			octstr_destroy(fname);
			return ret;
    	}
    	octstr_destroy(fname);
    }
    closedir(dir);

    return NULL;
}

static Octstr *get_msg_surrogate(const Octstr *dir_s, const Octstr *hash,
		                         const Octstr *dst, Octstr **filename)
{
    /* get our msg filename */
    if ((*filename = get_msg_filename(dir_s, hash, dst)) == NULL)
    	return NULL;

	return octstr_read_file(octstr_get_cstr(*filename));
}


/********************************************************************
 * Implementation of the DLR handle functions.
 */

/*
 * Adds a struct dlr_entry to the spool directory.
 */
static void dlr_spool_add(struct dlr_entry *dlr)
{
	Msg *msg;
	Octstr *os, *hash, *dir, *filename;
    int fd;
    size_t wrc;

#define MAP(to, from) \
	to = from; \
	from = NULL;

	/* create a common message structure to contain our values */
	msg = msg_create(sms);
	msg->sms.sms_type = report_mt;
	MAP(msg->sms.smsc_id, dlr->smsc);
	MAP(msg->sms.foreign_id, dlr->timestamp);
	MAP(msg->sms.sender, dlr->source);
	MAP(msg->sms.receiver, dlr->destination);
	MAP(msg->sms.service, dlr->service);
	MAP(msg->sms.dlr_url, dlr->url);
	MAP(msg->sms.boxc_id, dlr->boxc_id);
	msg->sms.dlr_mask = dlr->mask;

	/* we got all values, destroy the structure now */
	dlr_entry_destroy(dlr);

	/* create hash value */
	os = octstr_duplicate(msg->sms.smsc_id);
	octstr_append(os, msg->sms.foreign_id);
	hash = our_hash_func(os);
	octstr_destroy(os);

	/* target directory */
    dir = octstr_format("%S/%ld", spool_dir, octstr_hash_key(hash) % MAX_DIRS);
    if (mkdir(octstr_get_cstr(dir), S_IRUSR|S_IWUSR|S_IXUSR) == -1 && errno != EEXIST) {
        error(errno, "Could not create directory `%s'.", octstr_get_cstr(dir));
        octstr_destroy(dir);
        octstr_destroy(hash);
        return;
    }

    /*
     * Now also add the hex value of the destination.
     * This will be the part we look later into while
     * DLR resolving.
     */
    os = octstr_duplicate(msg->sms.receiver);
    octstr_binary_to_hex(os, 0);
    octstr_append(hash, os);
    octstr_destroy(os);

    /* target file */
    filename = octstr_format("%S/%S", dir, hash);
    octstr_destroy(dir);
    octstr_destroy(hash);
    if ((fd = open(octstr_get_cstr(filename), O_CREAT|O_EXCL|O_WRONLY, S_IRUSR|S_IWUSR)) == -1) {
        error(errno, "Could not open file `%s'.", octstr_get_cstr(filename));
        octstr_destroy(filename);
        return;
    }

    /* pack and write content to file */
    os = store_msg_pack(msg);
    msg_destroy(msg);
    for (wrc = 0; wrc < octstr_len(os); ) {
        size_t rc = write(fd, octstr_get_cstr(os) + wrc, octstr_len(os) - wrc);
        if (rc == -1) {
            /* remove file */
            error(errno, "Could not write DLR message to `%s'.", octstr_get_cstr(filename));
            close(fd);
            if (unlink(octstr_get_cstr(filename)) == -1)
                error(errno, "Oops, Could not remove failed file `%s'.", octstr_get_cstr(filename));
            octstr_destroy(os);
            octstr_destroy(filename);
            return;
        }
        wrc += rc;
    }
    close(fd);
    counter_increase(counter);
    octstr_destroy(filename);
    octstr_destroy(os);
}


/*
 * Find matching entry in our spool and return the dlr_entry.
 */
static struct dlr_entry *dlr_spool_get(const Octstr *smsc, const Octstr *ts, const Octstr *dst)
{
    struct dlr_entry *ret = NULL;
    Octstr *os, *hash, *dir, *filename = NULL;
    Msg *msg;

    /* determine target dir and filename via hash */
    os = octstr_duplicate(smsc);
    octstr_append(os, ts);
	hash = our_hash_func(os);
	octstr_destroy(os);

	/* determine target dir */
	dir = octstr_format("%S/%ld", spool_dir, octstr_hash_key(hash) % MAX_DIRS);

	/* get content of msg surrogate */
	os = get_msg_surrogate(dir, hash, dst, &filename);
    octstr_destroy(dir);
    octstr_destroy(hash);

    /* if there was no content */
    if (os == NULL) {
        octstr_destroy(filename);
    	return NULL;
    }

    /* unpack */
    if ((msg = store_msg_unpack(os)) == NULL) {
    	octstr_destroy(os);
        error(0, "Could not unpack DLR message `%s'", octstr_get_cstr(filename));
        octstr_destroy(filename);
        return ret;
    }

    octstr_destroy(os);
    octstr_destroy(filename);

#define MAP(to, from) \
	to = from; \
	from = NULL;

    /* map values to a struct dlr_entry */
    ret = dlr_entry_create();
	MAP(ret->smsc, msg->sms.smsc_id);
	MAP(ret->timestamp, msg->sms.foreign_id);
	MAP(ret->source, msg->sms.sender);
	MAP(ret->destination, msg->sms.receiver);
	MAP(ret->service, msg->sms.service);
	MAP(ret->url, msg->sms.dlr_url);
	MAP(ret->boxc_id, msg->sms.boxc_id);
	ret->mask = msg->sms.dlr_mask;

	msg_destroy(msg);

    return ret;
}


/*
 * Remove matching entry from the spool.
 */
static void dlr_spool_remove(const Octstr *smsc, const Octstr *ts, const Octstr *dst)
{
    Octstr *os, *hash, *dir, *filename;

    /* determine target dir and filename via hash */
    os = octstr_duplicate(smsc);
    octstr_append(os, ts);
	hash = our_hash_func(os);
	octstr_destroy(os);

	/* determine target dir */
	dir = octstr_format("%S/%ld", spool_dir, octstr_hash_key(hash) % MAX_DIRS);

	/* get msg surrogate filename */
	filename = get_msg_filename(dir, hash, dst);
    octstr_destroy(dir);
    octstr_destroy(hash);

    /* if there was no filename, then we didn't find it */
    if (filename == NULL) {
    	return;
    }

    /* remove the file from the file system */
    if (unlink(octstr_get_cstr(filename)) == -1) {
        error(errno, "Could not unlink file `%s'.", octstr_get_cstr(filename));
        octstr_destroy(filename);
        return;
    }

    counter_decrease(counter);
    octstr_destroy(filename);
}


/*
 * Destroy data structures of the module.
 */
static void dlr_spool_shutdown()
{
	counter_destroy(counter);
	octstr_destroy(spool_dir);
}


/*
 * Get count of DLR messages within the spool.
 */
static long dlr_spool_messages(void)
{
    return counter_value(counter);
}


/*
 * Flush all DLR messages out of the spool, removing all.
 */
static void dlr_spool_flush(void)
{
    for_each_file(spool_dir, 1, unlink_file);
    counter_set(counter, 0);
}


/********************************************************************
 * DLR storage handle definition and init function.
 */

static struct dlr_storage handles = {
    .type = "spool",
    .dlr_add = dlr_spool_add,
    .dlr_get = dlr_spool_get,
    .dlr_remove = dlr_spool_remove,
    .dlr_shutdown = dlr_spool_shutdown,
    .dlr_messages = dlr_spool_messages,
    .dlr_flush = dlr_spool_flush
};


/*
 * Initialize dlr_waiting_list and return out storage handles.
 */
struct dlr_storage *dlr_init_spool(Cfg *cfg)
{
	CfgGroup *grp;

    if (!(grp = cfg_get_single_group(cfg, octstr_imm("core"))))
        panic(0, "DLR: spool: group 'core' is not specified!");

    if (!(spool_dir = cfg_get(grp, octstr_imm("dlr-spool"))))
   	    panic(0, "DLR: spool: directive 'dlr-spool' is not specified!");

#ifdef HAVE_LIBSSL
    OpenSSL_add_all_digests();
#endif

    counter = counter_create();

    /* we need to traverse the DLR spool to determine how
     * many entries we have. */
#ifdef VERIFIED
    for_each_file(spool_dir, 1, verified_file);
#else
    for_each_file(spool_dir, 1, non_verified_file);
#endif

    return &handles;
}
