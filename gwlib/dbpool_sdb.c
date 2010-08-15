/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2009 Kannel Group  
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
 * dbpool_sdbl.c - implement SDB operations for generic database connection pool
 *
 * Guillaume Cottenceau, 2004
 */

#ifdef HAVE_SDB
#include <sdb.h>

static void* sdb_open_conn(const DBConf *db_conf)
{
    SDBConf *conf = db_conf->sdb; /* make compiler happy */
    char *connection;

    /* sanity check */
    if (conf == NULL)
        return NULL;

    connection = sdb_open(octstr_get_cstr(conf->url));
    if (connection == NULL) {
        error(0, "SDB: could not connect to database");
        return NULL;
    }

    info(0, "SDB: Connected to %s.", octstr_get_cstr(conf->url));

    return connection;
}

static void sdb_close_conn(void *conn)
{
    if (conn == NULL)
        return;

    sdb_close(conn);
}

static int sdb_check_conn(void *conn)
{
    if (conn == NULL)
        return -1;

    /* nothing in SDB to check for the connection, always succeed */
    return 0;
}

static void sdb_conf_destroy(DBConf *db_conf)
{
    SDBConf *conf = db_conf->sdb;

    octstr_destroy(conf->url);

    gw_free(conf);
    gw_free(db_conf);
}

static struct db_ops sdb_ops = {
    .open = sdb_open_conn,
    .close = sdb_close_conn,
    .check = sdb_check_conn,
    .conf_destroy = sdb_conf_destroy
};

#endif /* HAVE_SDB */

