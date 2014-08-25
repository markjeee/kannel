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
 * numhash.h - (telephone) number storing/hashing system
 *
 * Kalle Marjola 2000 for project Kannel
 *
 * !!! NOTE NOTE NOTE !!!
 *
 * Phone number precision is limited according to sizeof(long long)
 * in host machine. that is usually either 32 or 64 bits. In a
 * case of 32 bit longs, only last 19 digits are checked, otherwise
 * last 38 digits. This means that in some places several numbers
 * might map to same hash entry, and thus some caution is needed
 * specially with telephone number black lists
 *
 * USAGE:
 *  the system is not very dynamic; if you want to resize the table
 *  or hash, you must first nuke all old data and then recreate it
 *
 * MEMORY NEEDED:  (approximated)
 *
 * 2* (sizeof(long long)+sizeof(void *)) bytes per number
 */

#ifndef NUMHASH_H
#define NUMHASH_H

#include <stdio.h>

/* number hashing/seeking functions
 * all return -1 on error and write to general Kannel log
 *
 * these 2 first are only required if you want to add the numbers
 * by hand - otherwise use the last function instead
 *
 * use prime_hash if you want an automatically generated hash size
 */

typedef struct numhash_table Numhash;	


/* get numbers from 'url' and create a new database out of them
 * Return NULL if cannot open database or other error, error is logged
 *
 * Numbers to datafile are saved as follows:
 *  - one number per line
 *  - number might have white spaces, '+' and '-' signs
 *  - number is ended with ':' or end-of-line
 *  - there can be additional comment after ':'
 *
 * For example, all following ones are valid lines:
 *  040 1234
 *  +358 40 1234
 *  +358 40-1234 : Kalle Marjola
 */
Numhash *numhash_create(const char *url); 

/* destroy hash and all numbers in it */
void numhash_destroy(Numhash *table);

/* check if the number is in database, return 1 if found, 0 if not,
 * -1 on error */
int numhash_find_number(Numhash *table, Octstr *nro);
				      
/* if we already have the key */
int numhash_find_key(Numhash *table, long long key);

/* if we want to know the key */
long long numhash_get_key(Octstr *nro);
long long numhash_get_char_key(char *nro);


/* Return hash fill percent. If 'longest' != NULL, set as longest
 * trail in hash */
double numhash_hash_fill(Numhash *table, int *longest);

/* return number of numbers in hash */
int numhash_size(Numhash *table);

#endif
