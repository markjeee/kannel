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
 * parse.c - implement parse.h interface
 *
 * Richard Braakman
 */

#include "gwlib/gwlib.h"

struct context
{
    Octstr *data;
    long pos;
    long limit;
    List *limit_stack;
    int error;
};

ParseContext *parse_context_create(Octstr *str)
{
    ParseContext *result;

    result = gw_malloc(sizeof(*result));
    result->data = str;
    result->pos = 0;
    result->limit = octstr_len(str);
    result->limit_stack = NULL;
    result->error = 0;

    return result;
}

void parse_context_destroy(ParseContext *context)
{
    gw_assert(context != NULL);

    if (context->limit_stack) {
        while (gwlist_len(context->limit_stack) > 0)
            gw_free(gwlist_extract_first(context->limit_stack));
        gwlist_destroy(context->limit_stack, NULL);
    }
    gw_free(context);
}

int parse_error(ParseContext *context)
{
    gw_assert(context != NULL);

    return context->error;
}

void parse_clear_error(ParseContext *context)
{
    gw_assert(context != NULL);

    context->error = 0;
}

void parse_set_error(ParseContext *context)
{
    gw_assert(context != NULL);

    context->error = 1;
}

int parse_limit(ParseContext *context, long length)
{
    long *elem;

    gw_assert(context != NULL);

    if (context->pos + length > context->limit) {
        context->error = 1;
        return -1;
    }

    if (context->limit_stack == NULL)
        context->limit_stack = gwlist_create();

    elem = gw_malloc(sizeof(*elem));
    *elem = context->limit;
    gwlist_insert(context->limit_stack, 0, elem);
    context->limit = context->pos + length;
    return 0;
}

int parse_pop_limit(ParseContext *context)
{
    long *elem;

    gw_assert(context != NULL);

    if (context->limit_stack == NULL || gwlist_len(context->limit_stack) == 0) {
        context->error = 1;
        return -1;
    }

    elem = gwlist_extract_first(context->limit_stack);
    context->limit = *elem;
    gw_free(elem);
    return 0;
}

long parse_octets_left(ParseContext *context)
{
    gw_assert(context != NULL);

    return context->limit - context->pos;
}

int parse_skip(ParseContext *context, long count)
{
    gw_assert(context != NULL);

    if (context->pos + count > context->limit) {
        context->pos = context->limit;
        context->error = 1;
        return -1;
    }

    context->pos += count;
    return 0;
}

void parse_skip_to_limit(ParseContext *context)
{
    gw_assert(context != NULL);

    context->pos = context->limit;
}

int parse_skip_to(ParseContext *context, long pos)
{
    gw_assert(context != NULL);

    if (pos < 0) {
        context->error = 1;
        return -1;
    }

    if (pos > context->limit) {
        context->pos = context->limit;
        context->error = 1;
        return -1;
    }

    context->pos = pos;
    return 0;
}

int parse_peek_char(ParseContext *context)
{
    gw_assert(context != NULL);

    if (context->pos == context->limit) {
        context->error = 1;
        return -1;
    }

    return octstr_get_char(context->data, context->pos);
}

int parse_get_char(ParseContext *context)
{
    gw_assert(context != NULL);

    if (context->pos == context->limit) {
        context->error = 1;
        return -1;
    }

    return octstr_get_char(context->data, context->pos++);
}

Octstr *parse_get_octets(ParseContext *context, long length)
{
    Octstr *result;

    gw_assert(context != NULL);

    if (context->pos + length > context->limit) {
        context->error = 1;
        return NULL;
    }

    result = octstr_copy(context->data, context->pos, length);
    context->pos += length;
    return result;
}

unsigned long parse_get_uintvar(ParseContext *context)
{
    long pos;
    unsigned long value;

    gw_assert(context != NULL);

    pos = octstr_extract_uintvar(context->data, &value, context->pos);
    if (pos < 0 || pos > context->limit) {
        context->error = 1;
        return 0;
    }

    context->pos = pos;
    return value;
}

Octstr *parse_get_nul_string(ParseContext *context)
{
    Octstr *result;
    long pos;

    gw_assert(context != NULL);

    pos = octstr_search_char(context->data, 0, context->pos);
    if (pos < 0 || pos >= context->limit) {
        context->error = 1;
        return NULL;
    }

    result = octstr_copy(context->data, context->pos, pos - context->pos);
    context->pos = pos + 1;

    return result;
}

Octstr *parse_get_line(ParseContext *context)
{
    Octstr *result;
    long pos;

    gw_assert(context != NULL);

    pos = octstr_search_char(context->data, '\n', context->pos);
    if (pos < 0 || pos >= context->limit) {
        context->error = 1;
        return NULL;
    }
    
    result = octstr_copy(context->data, context->pos, pos - context->pos);
    context->pos = pos + 1;

    octstr_strip_crlfs(result);

    return result;
}

Octstr *parse_get_seperated_block(ParseContext *context, Octstr *seperator)
{
    Octstr *result;
    long spos, epos;

    gw_assert(context != NULL);
    gw_assert(seperator != NULL);

    spos = octstr_search(context->data, seperator, context->pos);
    if (spos < 0 || spos >= context->limit) {
        context->error = 1;
        return NULL;
    }
    epos = octstr_search(context->data, seperator, spos + octstr_len(seperator));
    if (epos < 0 || epos >= context->limit) {
        context->error = 1;
        return NULL;
    }

    spos = spos + octstr_len(seperator);
    result = octstr_copy(context->data, spos, epos - spos);
    context->pos = epos;

    return result;
}

Octstr *parse_get_rest(ParseContext *context)
{
    Octstr *rest;
    
    gw_assert(context != NULL);
    
    octstr_delete(context->data, 0, context->pos);
    rest = octstr_duplicate(context->data);   
    
    return rest;   
}
