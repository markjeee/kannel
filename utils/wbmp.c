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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "gwlib/gwlib.h"
#include "wbmp.h"



/* create empty WBMP */

WBMP *wbmp_create_empty(void)
{
    WBMP *new;

    new = gw_malloc(sizeof(WBMP));
    memset(new, 0, sizeof(WBMP));
    return new;
}

/* delete a WBMP, freeing everything */

void wbmp_delete(WBMP *pic)
{
    gw_free(pic->ext_header_field);
    gw_free(pic->main_image);
    if (pic->animated_image) {
	int i;
	for(i=0; i < pic->animimg_count; i++)
	    gw_free(pic->animated_image[i]);
	gw_free(pic->animated_image);
    }
    gw_free(pic);
}


WBMP *wbmp_create(int type, int width, int height, Octet *data, int flags)
{
    WBMP *new;
    int i, siz;
    Octet val;
    
    new = wbmp_create_empty();

    new->type_field = type;
    if (type == 0) {
	new->fix_header_field = 0x00;
    } else {
	error(0, "WBMP type %d not supported", type);
	return NULL;
    }
    new->width = width;
    new->height = height;
    siz = (width+7)/8 * height;
    
    new->main_image = gw_malloc(siz);
    for(i=0; i < siz; i++) {
	if (flags & REVERSE) val = reverse_octet(data[i]);
	else val = data[i];
	if (flags & NEGATIVE) val = ~val;
	new->main_image[i] = val;
    }    
    return new;
}



/* create Octet stream from given WBMP */

int wbmp_create_stream(WBMP *pic, Octet **stream)
{
    Octet	tmp_w[30], tmp_h[30];
    int		wl, hl, pic_size;
    
    wl = write_variable_value(pic->width, tmp_w);
    hl = write_variable_value(pic->height, tmp_h);

    pic_size = ((pic->width+7)/8) * pic->height;

    if (pic->type_field != 0) {
	error(0, "Unknown WBMP type %d, cannot convert", pic->type_field);
	return -1;
    }
    *stream = gw_malloc(2+wl+hl+pic_size);
    sprintf(*stream, "%c%c", 0x00, 0x00); 
    memcpy(*stream+2, tmp_w, wl);
    memcpy(*stream+2+wl, tmp_h, hl);
    memcpy(*stream+2+wl+hl, pic->main_image, pic_size);

    debug("util", 0, "picture %d x %d, stream length %d",
	  pic->width, pic->height, 2+wl+hl+pic_size);
    
    return (2+wl+hl+pic_size);
}
