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
#include "OTAbitmap.h"



/* create empty OTAbitmap */

OTAbitmap *OTAbitmap_create_empty(void)
{
    OTAbitmap *new;

    new = gw_malloc(sizeof(OTAbitmap));
    memset(new, 0, sizeof(OTAbitmap));
    return new;
}

void OTAbitmap_delete(OTAbitmap *pic)
{
    gw_free(pic->ext_fields);
    gw_free(pic->main_image);
    if (pic->animated_image) {
	int i;
	for(i=0; i < pic->animimg_count; i++)
	    gw_free(pic->animated_image[i]);
	gw_free(pic->animated_image);
    }
    gw_free(pic);
}

OTAbitmap *OTAbitmap_create(int width, int height, int depth,
			    Octet *data, int flags)
{
    OTAbitmap *new;
    int i, j, siz, osiz;
    Octet val;
    
    new = OTAbitmap_create_empty();

    if (width > 255 || height > 255)
	new->infofield = 0x10;		/* set bit */
    else
	new->infofield = 0x00;
    
    new->width = width;
    new->height = height;

    siz = (width * height + 7)/8;
    
    new->main_image = gw_malloc(siz);
    osiz = (width+7)/8 * height;
    for(i=j=0; i<osiz; i++, j+=8) {
	val = data[i];
	if (flags & REVERSE) val = reverse_octet(val);	
	if (flags & NEGATIVE) val = ~val;

	if (i > 0 && i % ((width+7)/8) == 0 && width % 8 > 0)
	    j -= 8 + width % 8;

	if (j % 8 == 0) {
	    new->main_image[j/8] = val;
	}
	else {
	    new->main_image[j/8] |= val >> (j % 8);
	    new->main_image[j/8 + 1] = val << (8 - j % 8);
	}	    
    }
    /* no palette nor animated images, yet */
    
    return new;
}



/* create Octet stream from given OTAbitmap */

int OTAbitmap_create_stream(OTAbitmap *pic, Octet **stream)
{
    Octet	tmp_header[10];
    int		hdr_len;
    int		pic_size;

    if (pic->infofield & 0x10) {
	sprintf(tmp_header, "%c%c%c%c%c%c", pic->infofield, pic->width/256,
		pic->width%256, pic->height/256, pic->height%256, pic->depth); 
	hdr_len = 6;
    } else {
	sprintf(tmp_header, "%c%c%c%c", pic->infofield,
		pic->width, pic->height, pic->depth);
	hdr_len = 4;
    }	
    
    pic_size = (pic->width * pic->height + 7)/8;

    *stream = gw_malloc(pic_size+pic_size);
    memcpy(*stream, tmp_header, hdr_len);
    memcpy(*stream + hdr_len, pic->main_image, pic_size);

    debug("util", 0, "picture %d x %d, stream length %d",
	  pic->width, pic->height, hdr_len + pic_size);
    
    return (hdr_len + pic_size);
}
