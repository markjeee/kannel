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
 * dict.h - lookup data structure using octet strings as keys
 *
 * A Dict is an abstract data structure that stores values, represented as
 * void pointers, and uses octet strings (Octstr) as keys. You can think
 * of it as an array indexed by octet strings.
 *
 * Lars Wirzenius
 */

#ifndef DICT_H
#define DICT_H

typedef struct Dict Dict;


/*
 * Create a Dict. `size_hint' gives an indication of how many different
 * keys will be in the Dict at the same time, at most. This is used for
 * performance optimization; things will work fine, though somewhat
 * slower, even if it the number is exceeded. `destroy_value' is a pointer
 * to a function that is called whenever a value stored in the Dict needs
 * to be destroyed. If `destroy_value' is NULL, then values are not
 * destroyed by the Dict, they are just discarded.
 */
Dict *dict_create(long size_hint, void (*destroy_value)(void *));


/*
 * Destroy a Dict and all values in it.
 */
void dict_destroy(Dict *dict);


/*
 * Put a new value into a Dict. If the same key existed already, the
 * old value is destroyed. If `value' is NULL, the old value is destroyed
 * and the key is removed from the Dict.
 */
void dict_put(Dict *dict, Octstr *key, void *value);

/*
 * Put a new value into a Dict. Return error, if the same key existed all-
 * ready.
 */

int dict_put_once(Dict *dict, Octstr *key, void *value);

/*
 * Look up a value in a Dict. If there is no value corresponding to a 
 * key, return NULL, otherwise return the value. The value will not
 * be removed from the Dict.
 */
void *dict_get(Dict *dict, Octstr *key);


/*
 * Remove a value from a Dict without destroying it.
 */
void *dict_remove(Dict *dict, Octstr *key);


/*
 * Return the number of keys which currently exist in the Dict.
 */
long dict_key_count(Dict *dict);


/*
 * Return a list of all the currently defined keys in the Dict. The
 * caller must destroy the list.
 */
List *dict_keys(Dict *dict);


#endif






