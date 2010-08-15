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

/* seebmp -- simple viewer for WBMP images (image/vnd.wap.wbmp) */

/*
 * 
 * Copyright (c) Richard Braakman <dark@xs4all.nl>
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 */


#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* The spec includes a multi-byte integer format.  For reasons of simplicity, we
 * only handle integers that end up fitting in 31 bits.  This limits picture
 * sizes to about 2 billion pixels width or height, which I think we can live
 * with. */
/* The function returns -1 in case of an error. */
static long get_mbi(FILE *infile) {
	int c;
	long result = 0;

	do {
		c = getc(infile);
		if (c < 0) return -1;
		result = (result << 7) | (c & 0x7f);
	} while (c & 0x80);

	return result;
}

/* This function is like get_mbi, but it ignores the value of the result.
 * So it's not limited to 31 bits, and it ends up skipping all bytes that
 * have their high bit set. */
/* The function returns 0 for success, or -1 in case of an error. */
static int skip_mbi(FILE *infile) {
	int c;

	do {
		c = getc(infile);
		if (c < 0) return -1;
	} while (c & 0x80);

	return 0;
}

static int show_image_from_file(char *bmpname, FILE *bmpfile,
			long width, long height) {
	long w, h;

	for (h = 0; h < height; h++) {
		/* w is incremented in its inner loop */
		for (w = 0; w < width; ) {
			int c;
			unsigned int bit;

			c = getc(bmpfile);
			if (c < 0) {
				perror(bmpname);
				return -1;
			}

			for (bit = 0x80; bit > 0 && w < width; bit >>= 1, w++) {
				putc((c & bit) ? '*' : ' ', stdout);
			}
		}
		putc('\n', stdout);
	}

	return 0;
}

/* These are global because that's easier.  In a real parser they would
 * be part of an image descriptor structure */
struct parm {
	struct parm *next;
	char *name;
	char *value;
};

struct parm *extparms = NULL;

static void clear_extparms(void) {
	while (extparms) {
		struct parm *tmp = extparms;
		extparms = extparms->next;
		free(tmp->name);
		free(tmp->value);
		free(tmp);
	}
}

/* Record a new parameter.  The name and value will be used directly,
 * not copied. */
static int new_extparm(char *name, char *value) {
	struct parm *new;
	struct parm *p;
	
	/* Construct a new node */
	new = (struct parm *)malloc(sizeof(struct parm));
	if (!new)
		return -1;

	new->name = name;
	new->value = value;
	new->next = NULL;

	/* Add it to the end of the list. */
	if (!extparms) {
		extparms = new;
	} else {
		p = extparms;
		while (p->next) { 
			p = p->next;
		}
		p->next = new;
	}

	return 0;
}

static void print_extparms(FILE *outfile) {
	struct parm *p;

	for (p = extparms; p; p = p->next) {
		fprintf(outfile, "%s=%s\n", p->name, p->value);
	}
}

static int parse_headers(FILE *bmpfile) {
	int c;
	int exttype;

	clear_extparms();

	c = getc(bmpfile);
	if (c < 0) return -1;
	if (!(c & 0x80)) {
		/* No extension headers follow */
		return 0;
	}
	exttype = (c >> 5) & 0x03;
	/* None of these headers do much at this time, but they
	 * might be meaningful with later specifications */
	switch (exttype) {
		case 0: 
			/* All we know of type 0 headers is that
			 * the high bit is a continuation bit.
			 * That makes them exactly like an MBI. */
			if (skip_mbi(bmpfile) < 0) return -1;
			break;
		case 1: case 2:
			/* We don't know what to do with these */
			return -1;
		case 3:
			/* A sequence of parameter/value combinations */
			do {
				int namelen, valuelen;
				char *name, *value;
				c = getc(bmpfile);
				if (c < 0) return -1;

				namelen = (c >> 4) & 0x07;
				name = malloc(namelen + 1);
				if (!name) return -1;
				if (fread(name, namelen, 1, bmpfile) < (size_t) namelen)
					return -1;

				valuelen = c & 0x0f;
				value = malloc(valuelen + 1);
				if (!value) { free(name); return -1; }
				if (fread(value, valuelen, 1, bmpfile) < (size_t) valuelen)
					return -1;
			
				new_extparm(name, value);
			} while (c & 0x80);
			break;
	}
	return 0;
}

/* Return 0 for success, < 0 for failure */
static int show_wbmp_from_file(char *bmpname, FILE *bmpfile) {
	long typefield;
	long width, height;

	typefield = get_mbi(bmpfile);
	if (typefield < 0) {
		perror(bmpname);
		return -1;
	}

	if (parse_headers(bmpfile) < 0) {
		fprintf(stderr, "%s: format error in headers\n", bmpname);
		return -1;
	}
	
	width = get_mbi(bmpfile);
	height = get_mbi(bmpfile);
	if (width < 0 || height < 0) {
		fprintf(stderr, "%s: error reading height and width\n",
			bmpname);
		return -1;
	}

	switch (typefield) {
		case 0:
			printf("%s, %ldx%ld B/W bitmap, no compression\n",
				bmpname, width, height);
			print_extparms(stdout);
			if (show_image_from_file(bmpname, bmpfile,
						width, height) < 0) {
				return -1;
			}
			break;
		default:
			fprintf(stderr, "%s: cannot handle level %ld wbmp\n",
				bmpname, typefield);
			return -1;
	}

	return 0;
}

int main(int argc, char *argv[]) {
	int i;
	/* 1 means an I/O error.  No other error values are used yet. */
	int exitvalue = 0;

	if (argc > 1) {
		for (i = 1; i < argc; i++) {
			FILE *bmpfile;

			bmpfile = fopen(argv[i], "r");
			if (bmpfile) {
				if (show_wbmp_from_file(argv[i], bmpfile) < 0) {
					/* We've already reported the error */
					exitvalue = 1;
				}
				if (fclose(bmpfile) < 0) {
					perror(argv[i]);
					exitvalue = 1;
				}
			} else {
				perror(argv[i]);
				exitvalue = 1;
			}
			if (i < argc - 1) {
				/* more files follow -- separate them */
				printf("\n");
			}
		}
	} else {
		/* No files specified -- read from standard input */
		if (show_wbmp_from_file("stdin", stdin)) {
			exitvalue = 1;
		}
	}

	return exitvalue;
}
