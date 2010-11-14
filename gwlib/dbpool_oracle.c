/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2010 Kannel Group  
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
 * dbpool_oracle.c - implement Oracle operations for generic database connection pool
 *
 * Note: Oracle 8i and 9i support tested. This implementation will not
 * work with Oracle 7 version. Please do not use Oracle 9i-rc2 OCI
 * libraries on Linux, due to strange memory problems. If you do not
 * believe me, just check it youself with valgrind ;)
 *
 * Alexander Malysh <a.malysh@centrium.de>
 * Robert Ga³ach <robert.galach@my.tenbit.pl>
 *      2004 Added support for binding variables.
 */

#ifdef HAVE_ORACLE

#include <oci.h>

/* forward decl. */
static int oracle_select(void *theconn, const Octstr *sql, List *binds, List **res);

struct ora_conn {
    /* environment handle */
    OCIEnv *envp;
    /* context handle */
    OCISvcCtx *svchp;
    /* error handle */
    OCIError *errhp;
};

/* This function prints the error */
static void oracle_checkerr(OCIError *errhp, sword status)
{
    text errbuf[512];
    sb4 errcode = 0;

    switch (status) {
        case OCI_SUCCESS:
            break;
        case OCI_SUCCESS_WITH_INFO:
            error(0, "Error - OCI_SUCCESS_WITH_INFO");
            break;
        case OCI_NEED_DATA:
            error(0, "Error - OCI_NEED_DATA");
            break;
        case OCI_NO_DATA:
            error(0, "Error - OCI_NODATA");
            break;
        case OCI_ERROR:
            if (errhp == NULL) break;
            (void) OCIErrorGet((dvoid *)errhp, (ub4) 1, (text *) NULL, &errcode,
                               errbuf, (ub4) sizeof(errbuf), OCI_HTYPE_ERROR);
            error(0, "Error - %.*s", 512, errbuf);
            break;
        case OCI_INVALID_HANDLE:
            error(0, "Error - OCI_INVALID_HANDLE");
            break;
        case OCI_STILL_EXECUTING:
            error(0, "Error - OCI_STILL_EXECUTE");
            break;
        case OCI_CONTINUE:
            error(0, "Error - OCI_CONTINUE");
            break;
        default:
            break;
    }
}


/*
 * Malloc callback function to get tracking of OCI allocs.
 */
static void *oracle_malloc(void *ctx, size_t size)
{
    void *ret = gw_malloc(size);
    debug("dbpool.oracle",0,"oracle_malloc called size=%ld @%08lx", (long) size, 
          (long) ret);
    return ret;
}


/*
 * Free callback function to get tracking of OCI allocs.
 */
static void oracle_free(void *ctx, void *ptr)
{
    debug("dbpool.oracle",0,"oracle_free called @%08lx", (long) ptr);
    gw_free(ptr);
}


/*
 * Realloc callback function to get tracking of OCI allocs.
 */
static void *oracle_realloc(void *ctx, void *ptr, size_t size)
{
    void *ret = gw_realloc(ptr, size);
    debug("dbpool.oracle",0,"oracle_realloc called size=%ld", (long) size);
    return ret;
}

static void* oracle_open_conn(const DBConf *db_conf)
{
    OracleConf *cfg = db_conf->oracle;
    sword errorcode = 0;
    text version[512];
    struct ora_conn *conn = gw_malloc(sizeof(struct ora_conn));

    gw_assert(conn != NULL);
    memset(conn, 0, sizeof(struct ora_conn));

    debug("dbpool.oracle",0,"oracle_open_conn called");

    /* init OCI environment */
    errorcode = OCIEnvCreate(&conn->envp,
                             OCI_THREADED|OCI_ENV_NO_MUTEX,
                             NULL,
                             oracle_malloc,
                             oracle_realloc,
                             oracle_free,
                             0,0);
    if (errorcode != OCI_SUCCESS) {
         oracle_checkerr(NULL, errorcode);
         error(0, "Got error while OCIEnvCreate %d", errorcode);
         gw_free(conn);
         return NULL;
    }

    debug("dbpool.oracle",0,"oci environment created");

    /* allocate error handle */
    errorcode = OCIHandleAlloc(conn->envp, (dvoid**) &conn->errhp, 
                               OCI_HTYPE_ERROR, 0, 0);
    if (errorcode != OCI_SUCCESS) {
        oracle_checkerr(NULL, errorcode);
        OCIHandleFree(conn->envp, OCI_HTYPE_ENV);
        gw_free(conn);
        return NULL;
    }

    debug("dbpool.oracle",0,"oci error handle allocated");

    /* open oracle user session */
    errorcode = OCILogon(conn->envp, conn->errhp, &conn->svchp,
                         (unsigned char*)octstr_get_cstr(cfg->username), octstr_len(cfg->username),
                         (unsigned char*)octstr_get_cstr(cfg->password), octstr_len(cfg->password),
                         (unsigned char*)octstr_get_cstr(cfg->tnsname), octstr_len(cfg->tnsname));

    if (errorcode != OCI_SUCCESS) {
        oracle_checkerr(conn->errhp, errorcode);
        OCIHandleFree(conn->errhp, OCI_HTYPE_ERROR);
        OCIHandleFree(conn->envp, OCI_HTYPE_ENV);
        gw_free(conn);
        return NULL;
    }

    debug("dbpool.oracle",0,"connected to database");

    errorcode = OCIServerVersion(conn->svchp, conn->errhp, version, 
                                 sizeof(version), OCI_HTYPE_SVCCTX);
    if (errorcode != OCI_SUCCESS) {
        oracle_checkerr(conn->errhp, errorcode);
    } else {
        info(0, "Connected to: %s", version);
    }

    return conn;
}


static void oracle_close_conn(void *theconn)
{
    struct ora_conn *conn = (struct ora_conn*) theconn;

    gw_assert(conn != NULL);

    if (conn->svchp != NULL)
        oracle_checkerr(conn->errhp, OCILogoff(conn->svchp, conn->errhp));

    OCIHandleFree(conn->errhp, OCI_HTYPE_ERROR);
    OCIHandleFree(conn->envp, OCI_HTYPE_ENV);
    /* OCITerminate(OCI_DEFAULT); */

    gw_free(conn);
}


static int oracle_check_conn(void *conn)
{
    Octstr *sql;
    List *res;
    int ret;

    /* TODO Check for appropriate OCI function */
    sql = octstr_create("SELECT 1 FROM DUAL");

    ret = oracle_select(conn, sql, NULL, &res);
    if (ret != -1 && gwlist_len(res) > 0) {
        List *row = gwlist_extract_first(res);
        gwlist_destroy(row, octstr_destroy_item);
    }
    if (ret != -1)
        gwlist_destroy(res, NULL);

    octstr_destroy(sql);

    return ret;
}


static void oracle_conf_destroy(DBConf *theconf)
{
    OracleConf *conf = theconf->oracle;

    octstr_destroy(conf->username);
    octstr_destroy(conf->password);
    octstr_destroy(conf->tnsname);

    gw_free(conf);
    gw_free(theconf);
}


static int oracle_select(void *theconn, const Octstr *sql, List *binds, List **res)
{
    List *row;
    OCIStmt *stmt;
    OCIParam *dparam;
    sword status;
    ub4 columns;
    ub4 i;
    struct data_s {
        text *data;
        ub2 size;
        sb2 ind;
        ub2 type;
    };
    struct data_s *data;
    struct ora_conn *conn = (struct ora_conn*) theconn;
    int binds_len = (binds ? gwlist_len(binds) : 0);

    *res = NULL;

    /* allocate statement handle */
    status = OCIHandleAlloc(conn->envp, (dvoid**)&stmt, OCI_HTYPE_STMT, 0,0);
    if (OCI_SUCCESS != status) {
        oracle_checkerr(conn->errhp, status);
        return -1;
    }
    /* prepare statement */
    status = OCIStmtPrepare(stmt, conn->errhp, (unsigned char*)octstr_get_cstr(sql), 
                            octstr_len(sql), OCI_NTV_SYNTAX, OCI_DEFAULT);
    if (OCI_SUCCESS != status) {
        oracle_checkerr(conn->errhp, status);
        OCIHandleFree(stmt, OCI_HTYPE_STMT);
        return -1;
    }

    /* bind variables */
    for (i = 0; i < binds_len; i++) {
        OCIBind *bndhp = NULL;
        Octstr *bind = gwlist_get(binds, i);
        status = OCIBindByPos(stmt, &bndhp, 
                              conn->errhp, (i+1), (dvoid *) octstr_get_cstr(bind),
                              (sword) octstr_len(bind)+1, SQLT_STR, (dvoid *) 0, (ub2 *)0,
                              (ub2 *)0, (ub4)0, (ub4 *)0, OCI_DEFAULT);
        if (OCI_SUCCESS != status) {
            oracle_checkerr(conn->errhp, status);
            OCIHandleFree(stmt, OCI_HTYPE_STMT);
            return -1;
        }
    }
    /* execute our statement */
    status = OCIStmtExecute(conn->svchp, stmt, conn->errhp, 0, 0, NULL, NULL, 
                            OCI_DEFAULT);
    if (OCI_SUCCESS != status && OCI_NO_DATA != status) {
        oracle_checkerr(conn->errhp, status);
        OCIHandleFree(stmt, OCI_HTYPE_STMT);
        return -1;
    }
    /* receive column count */
    status = OCIAttrGet(stmt, OCI_HTYPE_STMT, &columns, 0, OCI_ATTR_PARAM_COUNT, 
                        conn->errhp);
    if (status != OCI_SUCCESS) {
        oracle_checkerr(conn->errhp, status);
        OCIHandleFree(stmt, OCI_HTYPE_STMT);
        return -1;
    }

    debug("dbpool.oracle",0,"SQL has %d columns", columns);

    /* allocate array of pointers */
    debug("dbpool.oracle",0,"alloc size=%ld",sizeof(text*)*columns);
    data = gw_malloc(sizeof(struct data_s)*columns);

    debug("dbpool.oracle",0,"retrieve data_size");
    /* retrieve data size for every column and allocate it */
    for (i=0 ; i < columns; i++) {
        OCIDefine *defh;

        status = OCIParamGet(stmt, OCI_HTYPE_STMT, conn->errhp, 
                             (dvoid**) &dparam, i+1);
        if (status != OCI_SUCCESS) {
            oracle_checkerr(conn->errhp, status);
            columns = i;
            for (i = 0; i < columns; i++)
                gw_free(data[i].data);
            gw_free(data);
            OCIHandleFree(stmt, OCI_HTYPE_STMT);
            return -1;
        }

        status = OCIAttrGet(dparam, OCI_DTYPE_PARAM, (dvoid*) &data[i].size, 
                            0, OCI_ATTR_DATA_SIZE, conn->errhp);
        if (status != OCI_SUCCESS) {
            oracle_checkerr(conn->errhp, status);
            columns = i;
            for (i = 0; i < columns; i++)
                gw_free(data[i].data);
            gw_free(data);
            OCIHandleFree(stmt, OCI_HTYPE_STMT);
            return -1;
        }

        status = OCIAttrGet(dparam, OCI_DTYPE_PARAM, (dvoid*) &data[i].type, 
                            0, OCI_ATTR_DATA_TYPE, conn->errhp);
        if (status != OCI_SUCCESS) {
            oracle_checkerr(conn->errhp, status);
            columns = i;
            for (i = 0; i < columns; i++)
                gw_free(data[i].data);
            gw_free(data);
            OCIHandleFree(stmt, OCI_HTYPE_STMT);
            return -1;
        }

        /* convert all data types to C-Strings except DATE */
        if (data[i].type != SQLT_DAT) {
            data[i].size++; /* terminating zero */
            data[i].type = SQLT_STR;
        }

        debug("dbpool.oracle",0,"alloc size=%d", data[i].size);
        data[i].data = gw_malloc(data[i].size);

        /* bind allocated values to statement handle */
        status = OCIDefineByPos(stmt, &defh, conn->errhp, i+1, data[i].data, 
                                data[i].size, data[i].type, &data[i].ind, 
                                0, 0, OCI_DEFAULT);
        if (status != OCI_SUCCESS) {
            oracle_checkerr(conn->errhp, status);
            columns = i;
            for (i = 0; i <= columns; i++)
                gw_free(data[i].data);
            gw_free(data);
            OCIHandleFree(stmt, OCI_HTYPE_STMT);
            return -1;
        }
    }

    *res = gwlist_create();
    /* fetch data */
    while ((status = OCIStmtFetch(stmt, conn->errhp, 1, 
                                  OCI_FETCH_NEXT, OCI_DEFAULT)) == OCI_SUCCESS ||
            status == OCI_SUCCESS_WITH_INFO) {

        row = gwlist_create();
        for (i = 0; i < columns; i++) {
            if (data[i].data == NULL || data[i].ind == -1) {
                gwlist_insert(row, i, octstr_create(""));
            } else {
                gwlist_insert(row, i, octstr_create_from_data((const char*)data[i].data, data[i].size));
            }
            /* debug("dbpool.oracle",0,"inserted value = '%s'", 
                     octstr_get_cstr(gwlist_get(row,i))); */
        }
        gwlist_append(*res, row);
    }

    /* ignore OCI_NO_DATA error */
    if (status != OCI_NO_DATA) {
        List *row;
        oracle_checkerr(conn->errhp, status);
        for (i = 0; i < columns; i++)
            gw_free(data[i].data);
        gw_free(data);
        while ((row = gwlist_extract_first(*res)) != NULL)
            gwlist_destroy(row, octstr_destroy_item);
        gwlist_destroy(*res, NULL);
        *res = NULL;
        OCIHandleFree(stmt, OCI_HTYPE_STMT);
        return -1;
    }

    for (i = 0; i < columns; i++)
        gw_free(data[i].data);

    gw_free(data);
    OCIHandleFree(stmt, OCI_HTYPE_STMT);

    return 0;
}


static int oracle_update(void *theconn, const Octstr *sql, List *binds)
{
    OCIStmt *stmt;
    sword status;
    ub4 rows = 0, i;
    struct ora_conn *conn = (struct ora_conn*) theconn;
    int binds_len = (binds ? gwlist_len(binds) : 0);
    
    /* allocate statement handle */
    status = OCIHandleAlloc(conn->envp, (dvoid**)&stmt, OCI_HTYPE_STMT, 0,0);
    if (OCI_SUCCESS != status) {
        oracle_checkerr(conn->errhp, status);
        return -1;
    }
    debug("dbpool.oracle",0,"OCIStmt allocated");
    /* prepare statement */
    status = OCIStmtPrepare(stmt, conn->errhp, (unsigned char*)octstr_get_cstr(sql), 
                            octstr_len(sql), OCI_NTV_SYNTAX, OCI_DEFAULT);
    if (OCI_SUCCESS != status) {
        oracle_checkerr(conn->errhp, status);
        OCIHandleFree(stmt, OCI_HTYPE_STMT);
        return -1;
    }
    debug("dbpool.oracle",0,"OCIStmtPrepare done");
   
    /* bind variables */
    for (i = 0; i < binds_len; i++) {
        Octstr *bind = gwlist_get(binds, i);
        OCIBind *bndhp = NULL;
        status = OCIBindByPos(stmt, &bndhp, 
                              conn->errhp, (i+1), (dvoid *) octstr_get_cstr(bind),
                              (sword) octstr_len(bind)+1, SQLT_STR, (dvoid *) 0, (ub2 *)0,
                              (ub2 *)0, (ub4)0, (ub4 *)0, OCI_DEFAULT);
        if (OCI_SUCCESS != status) {
            oracle_checkerr(conn->errhp, status);
            OCIHandleFree(stmt, OCI_HTYPE_STMT);
            return -1;
        }
    }
    
    /* execute our statement */
    status = OCIStmtExecute(conn->svchp, stmt, conn->errhp, 1, 0, NULL, NULL, 
                            /*OCI_DEFAULT*/ OCI_COMMIT_ON_SUCCESS);
    if (OCI_SUCCESS != status && OCI_NO_DATA != status) {
        oracle_checkerr(conn->errhp, status);
        OCIHandleFree(stmt, OCI_HTYPE_STMT);
        return -1;
    }
    debug("dbpool.oracle",0,"OCIStmtExecute done");
    /* retrieve #rows processed so far */
    status = OCIAttrGet(stmt, OCI_HTYPE_STMT, &rows, 0, OCI_ATTR_ROW_COUNT, 
                        conn->errhp);
    if (status != OCI_SUCCESS) {
        oracle_checkerr(conn->errhp, status);
        /* we doesn't return error here, because sql is executed and commited already */
    }
    debug("dbpool.oracle",0,"rows processed = %d", rows);

    OCIHandleFree(stmt, OCI_HTYPE_STMT);
    
    return (int) rows;
}

static struct db_ops oracle_ops = {
    .open = oracle_open_conn,
    .close = oracle_close_conn,
    .check = oracle_check_conn,
    .conf_destroy = oracle_conf_destroy,
    .select = oracle_select,
    .update = oracle_update
};

#endif
