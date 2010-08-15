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
 *
 * wshash.h
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 WAPIT OY LTD.
 *		 All rights reserved.
 *
 * A mapping from null-terminated strings to `void *' pointers.
 *
 */

#ifndef WSHASH_H
#define WSHASH_H

/********************* Types and definitions ****************************/

/* A hash handle. */
typedef struct WsHashRec *WsHashPtr;

/* A callback function of this type is called to free the data item
   `item' when the hash is destroyed, or a new mapping is set for the
   key of the item `item'.  The argument `context' is a user specified
   context data for the function. */
typedef void (*WsHashItemDestructor)(void *item, void *context);

/********************* Prototypes for global functions ******************/

/* Create a new hash table.  The argument `destructor' is a destructor
   function that is called once for each deleted item.  The argument
   `context' is passed as context data to the destructor function.
   The argument `destructor' can be NULL in which case the mapped
   items are not freed.  The function returns NULL if the creation
   failed (out of memory). */
WsHashPtr ws_hash_create(WsHashItemDestructor destructor, void *contex);

/* Destroy the hash `hash' and free all resources it has allocated.
   If the hash has a destructor function, it is called once for each
   mapped item. */
void ws_hash_destroy(WsHashPtr hash);

/* Add a mapping from the name `name' to the data `data'.  The
   function takes a copy of the name `name' but the data `data' is
   stored as-is.  The possible old data, stored for the name `name',
   will be freed with the destructor function.  The function returns
   WS_TRUE if the operatio was successful or WS_FALSE otherwise. */
WsBool ws_hash_put(WsHashPtr hash, const char *name, void *data);

/* Get the mapping of the name `name' from the hash `hash'. */
void *ws_hash_get(WsHashPtr hash, const char *name);

/* Clear the hash and free all individual items with the destructor
   function.  After this call, the hash `hash' does not contain any
   mappings. */
void ws_hash_clear(WsHashPtr hash);

#endif /* not WSHASH_H */
