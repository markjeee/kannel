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
 * numhash.c
 *
 * NUMBER HASH functions
 *
 * functions to add a number to database/hash
 * and functions to retrieve them
 *
 * notes: read header file
 *
 * Kalle Marjola for project Kannel 1999-2000
 */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "gwlib/gwlib.h"
#include "numhash.h"


#define NUMHASH_AUTO_HASH -1

/* set of pre-calculated prime numbers for hash generation... */

static int primes[] = {
  101, 503, 1009, 2003, 3001, 5003, 7001,
  10007, 20011, 30011, 40009, 50021, 60013, 70001, 80021, 90001,
  100003, 150001, 200003, 300007, 400009, 500009, 600011, 700001,
  800011, 900001, 1000003, 1100009, 1200007, 1300021, 1400017, 1500007,
  1600033, 1700021, 1800017, 1900009, 2000003, 3000017, 4000037, 5000111,
  6000101, 7000127, 8000051, -1
};

struct numhash_table {
  struct numhash_number *numbers;
  long			number_total;
  long			table_size;
  struct numhash_number	**hash;
  long			hash_size;
}; /* Numhash */

struct numhash_number {
  long long		key;	/* (hopefully) unique key */
  struct numhash_number	*next;	/* next in hash table, if any */
};


struct nh_entry {
    Numhash		*hash;
    struct nh_entry	*next;
};
    
struct numhashes {
    struct nh_entry	*first;
    struct nh_entry 	*last;
}; /* Multitable */


static int	precision = 19;		/* the precision (last numbers) used */

/*
 * add new item (number) to hash table
 */
static int add_item(Numhash *table, struct numhash_number *nro)
{
    if (table->hash[nro->key % table->hash_size]) {    /* conflict */
	struct numhash_number *ptr = table->hash[nro->key % table->hash_size];

	if (ptr->key == nro->key)
	    goto duplicate;

	while(ptr->next) {
	    ptr = ptr->next;
	    if (ptr->key == nro->key)
		goto duplicate;
	}
	ptr->next = nro;				/* put as last of the linkage */
    } else
	table->hash[nro->key % table->hash_size] = nro;

    return 0;

duplicate:
    warning(0, "Duplicate number %lld!", nro->key);
    return -1;
}

/* Add a new number to number list and hash
 * Return 0 if all went ok, -1 if out of room
 */
static int numhash_add_number(Numhash *table, char *nro)
{
    struct numhash_number *newnro;

    if (table->number_total == table->table_size) {
	error(0, "Table limit %ld reached, cannot add %s!",
	      table->table_size, nro);
	return -1;
    }
    newnro =  &table->numbers[table->number_total++]; /* take the next free */

    newnro->key = numhash_get_char_key(nro);
    newnro->next = NULL;

    add_item(table, newnro);

    return 0;
}


/* Init the number table and hash table with given sizes
 */
static Numhash *numhash_init(int max_numbers, int hash_size)
{
    Numhash	*ntable = NULL;

    ntable = gw_malloc(sizeof(Numhash));

    if (hash_size > 0)
	ntable->hash_size = hash_size;
    else if (hash_size == NUMHASH_AUTO_HASH) {
	int i;
	for(i=0 ; primes[i] > 0; i++) {
	    ntable->hash_size = primes[i];
	    if (ntable->hash_size > max_numbers)
		break;
	}
    } else {
	gw_free(ntable);
	return NULL;
    }
    ntable->hash = gw_malloc(ntable->hash_size * sizeof(struct numhash_number *));
    memset(ntable->hash, 0, sizeof(struct numhash_number *) * ntable->hash_size);

    ntable->table_size = max_numbers;
    ntable->numbers = gw_malloc(ntable->table_size * sizeof(struct numhash_number));

    ntable->number_total = 0;

    /* set our accuracy according to the size of long int
     * Ok, we call this many times if we use multiple tables, but
     * that is not a problem...
     */
    if (sizeof(long long) >= 16)
        precision = 38;
    else if (sizeof(long long) >= 8)
        precision = 19;

    return ntable;
}



/*------------------------------------------------------
 * PUBLIC FUNCTIONS
 */



int numhash_find_number(Numhash *table, Octstr *nro)
{
    long long key = numhash_get_key(nro);
    if (key < 0)
        return key;

    return numhash_find_key(table, key);
}


int numhash_find_key(Numhash *table, long long key)
{
    struct numhash_number *ptr;

    ptr = table->hash[key % table->hash_size];
    while (ptr) {
	if (ptr->key == key) return 1;
	ptr = ptr->next;
    }
    return 0;	/* not found */
}



long long numhash_get_key(Octstr *nro)
{
    long long key;

    if (!nro) return -1;

    if (octstr_len(nro) > precision)
        key = strtoll(octstr_get_cstr(nro) + octstr_len(nro) - precision, (char**) NULL, 10);
    else
        key = strtoll(octstr_get_cstr(nro), (char**) NULL, 10);

    return key;
}


long long numhash_get_char_key(char *nro)
{
    int len;
    long long key;

    if (!nro) return -1;

    len = strlen(nro);

    if (len > precision)
        key = strtoll(nro + len - precision, (char**) NULL, 10);
    else
        key = strtoll(nro, (char**) NULL, 10);

    return key;
}


void numhash_destroy(Numhash *table)
{
    if (table == NULL)
	return;
    gw_free(table->numbers);
    gw_free(table->hash);
    gw_free(table);
}


double numhash_hash_fill(Numhash *table, int *longest)
{
    int i, l, max = 0, tot = 0;
    struct numhash_number *ptr;

    for (i=0; i < table->hash_size; i++)
	if (table->hash[i]) {
	    tot++;
	    ptr = table->hash[i];
	    for (l=0; ptr->next; ptr = ptr->next)
		l++;
	    if (l > max)
		max = l;
	}

    if (longest != NULL)
	*longest = max;

    return (double)(tot*100.0/(table->hash_size));
}


int numhash_size(Numhash *table)
{
    return table->number_total;
}


Numhash *numhash_create(const char *seek_url)
{
    int		loc, lines = 0;
    List	*request_headers, *reply_headers;
    Octstr	*url, *final_url, *reply_body;
    Octstr	*type, *charset;

    char *data, *ptr, numbuf[100];
    int		status;
    Numhash	*table;

    url = octstr_create(seek_url);
    request_headers = http_create_empty_headers();
    status = http_get_real(HTTP_METHOD_GET, url, request_headers, &final_url,
			    &reply_headers, &reply_body);
    octstr_destroy(url);
    octstr_destroy(final_url);
    http_destroy_headers(request_headers);

    if (status != HTTP_OK) {
	http_destroy_headers(reply_headers);
	octstr_destroy(reply_body);
	error(0, "Cannot load numhash!");
	return NULL;
    }
    http_header_get_content_type(reply_headers, &type, &charset);
    octstr_destroy(charset);
    http_destroy_headers(reply_headers);

    if (octstr_str_compare(type, "text/plain") != 0) {
        octstr_destroy(reply_body);
        error(0, "Strange content type <%s> for numhash - expecting 'text/plain'"
                 ", operatiom fails", octstr_get_cstr(type));
        octstr_destroy(type);
        return NULL;
    }
    octstr_destroy(type);

    ptr = data = octstr_get_cstr(reply_body);
    while(*ptr) {
	if (*ptr == '\n') lines++;
	ptr++;
    }
    debug("numhash", 0, "Total %d lines in %s", lines, seek_url);

    table = numhash_init(lines+10, NUMHASH_AUTO_HASH);  	/* automatic hash */

    /* now, parse the number information */

    lines = 0;

    while((ptr = strchr(data, '\n'))) {	/* each line is ended with linefeed */
	*ptr = '\0';
	while(*data != '\0' && isspace(*data))
	    data++;
	if (*data != '#') {
	    loc = 0;
	    while (*data != '\0') {
		if (isdigit(*data))
		    numbuf[loc++] = *data;
		else if (*data == ' ' || *data == '+' || *data == '-')
			;
		else break;
		data++;
	    }
	    if (loc) {
		numbuf[loc] = '\0';
		numhash_add_number(table, numbuf);
		lines++;
	    }
	    else
		warning(0, "Corrupted line '%s'", data);
	}
	data = ptr+1;	/* next row... */
    }
    octstr_destroy(reply_body);

    info(0, "Read from <%s> total of %ld numbers", seek_url, table->number_total);
    return table;
}

