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
 * dbpool_p.h - Database pool private header.
 *
 * Alexander Malysh <a.malysh@centrium.de>
 */

#ifndef DBPOOL_P_H
#define DBPOOL_P_H 1


struct db_ops {
    /*
     * Open db connection with given config params.
     * Config params are specificaly for each database type.
     * return NULL if error occurs ; established connection's pointer otherwise
     */
    void* (*open) (const DBConf *conf);
    /*
     * close given connection.
     */
    void (*close) (void *conn);
    /*
     * check if given connection still alive,
     * return -1 if not or error occurs ; 0 if all was fine
     * NOTE: this function is optional
     */
    int (*check) (void *conn);
    /*
     * Destroy specificaly configuration struct.
     */
    void (*conf_destroy) (DBConf *conf);
    /*
     * Database specific select.
     * Note: Result will be stored as follows:
     *           result is the list of rows each row will be stored also as list each column is stored as Octstr.
     * If someone has better idea please tell me ...
     *
     * @params conn - database specific connection; sql - sql statement ; 
     *         binds - list of Octstr values for binding holes in sql (NULL if no binds);
     *         result - result will be saved here
     * @return 0 if all was fine ; -1 otherwise
     */
    int (*select) (void *conn, const Octstr *sql, List *binds, List **result);
    /*
     * Database specific update/insert/delete.
     * @params conn - database specific connection ; sql - sql statement;
     *         binds - list of Octstr values for binding holes in sql (NULL if no binds);
     * @return #rows processed ; -1 if a error occurs
     */
    int (*update) (void *conn, const Octstr *sql, List *binds);
};

struct DBPool
{
    List *pool; /* queue representing the pool */
    unsigned int max_size; /* max #connections */
    unsigned int curr_size; /* current #connections */
    DBConf *conf; /* the database type specific configuration block */
    struct db_ops *db_ops; /* the database operations callbacks */
    enum db_type db_type; /* the type of database */
};


#endif

