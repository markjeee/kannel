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
 * wml_tester.c - a simple program to test the WML-compiler module
 *
 * Tuomas Luttinen <tuo@wapit.com>
 */

#include <stdlib.h>
#include <unistd.h>

#include "gwlib/gwlib.h"
#include "gw/wml_compiler.h"


typedef enum { NORMAL_OUT, SOURCE_OUT, BINARY_OUT } output_t;

static void help(void) {
    info(0, "Usage: wml_tester [-hsbzr] [-n number] [-f file] "
	 "[-c charset] file.wml\n"
	 "where\n"
	 "  -h  this text\n"
	 "  -s  output also the WML source, cannot be used with b\n"
	 "  -b  output only the compiled binary, cannot be used with s\n"
	 "  -z  insert a '\\0'-character in the middle of the input\n"
     "  -r  run XML parser in relaxed mode to recover from errors\n"
	 "  -n number   the number of times the compiling is done\n"
	 "  -f file     direct the output into a file\n"
	 "  -c charset  character set as given by the http");
}


static void set_zero(Octstr *ostr)
{
    octstr_set_char(ostr, (1 + (int) (octstr_len(ostr) *gw_rand()/
				      (RAND_MAX+1.0))), '\0');
}


int main(int argc, char **argv)
{
    output_t outputti = NORMAL_OUT;
    FILE *fp = NULL;
    Octstr *output = NULL;
    Octstr *filename = NULL;
    Octstr *wml_text = NULL;
    Octstr *charset = NULL;
    Octstr *wml_binary = NULL;
    int i, ret = 0, opt, file = 0, zero = 0, numstatus = 0, wml_strict = 1;
    long num = 0;

    /* You can give an wml text file as an argument './wml_tester main.wml' */

    gwlib_init();

    while ((opt = getopt(argc, argv, "hsbzrn:f:c:")) != EOF) {
	switch (opt) {
	case 'h':
	    help();
	    exit(0);
	case 's':
	    if (outputti == NORMAL_OUT)
		outputti = SOURCE_OUT;
	    else {
		help();
		exit(0);
	    }
	    break;
	case 'b':
	    if (outputti == NORMAL_OUT)
		outputti = BINARY_OUT;
	    else {
		help();
		exit(0);
	    }
	    break;
	case 'z':
	    zero = 1;
	    break;
    case 'r':
        wml_strict = 0;
        break;
	case 'n':
	    numstatus = octstr_parse_long(&num, octstr_imm(optarg), 0, 0);
	    if (numstatus == -1) { 
		/* Error in the octstr_parse_long */
		error(num, "Error in the handling of argument to option n");
		help();
		panic(0, "Stopping.");
	    }
	    break;
	case 'f':
	    file = 1;
	    filename = octstr_create(optarg);
	    fp = fopen(optarg, "a");
	    if (fp == NULL)
		panic(0, "Couldn't open output file.");	
	    break;
	case 'c':
	    charset = octstr_create(optarg);
	    break;
	case '?':
	default:
	    error(0, "Invalid option %c", opt);
	    help();
	    panic(0, "Stopping.");
	}
    }

    if (optind >= argc) {
	error(0, "Missing arguments.");
	help();
	panic(0, "Stopping.");
    }

    if (outputti == BINARY_OUT)
	 log_set_output_level(GW_PANIC);
    wml_init(wml_strict);

    while (optind < argc) {
	wml_text = octstr_read_file(argv[optind]);
	if (wml_text == NULL)
	    panic(0, "Couldn't read WML source file.");

	if (zero)
	    set_zero(wml_text);

	for (i = 0; i <= num; i++) {
	    ret = wml_compile(wml_text, charset, &wml_binary, NULL);
	    if (i < num)
		octstr_destroy(wml_binary);
	}
	optind++;

	output = octstr_format("wml_compile returned: %d\n\n", ret);
    
	if (ret == 0) {
	    if (fp == NULL)
		fp = stdout;

	    if (outputti != BINARY_OUT) {
		if (outputti == SOURCE_OUT) {
		    octstr_insert(output, wml_text, octstr_len(output));
		    octstr_append_char(output, '\n');
		}

		octstr_append(output, octstr_imm(
		    "Here's the binary output: \n\n"));
		octstr_print(fp, output);
	    }

	    if (file && outputti != BINARY_OUT) {
		fclose(fp);
		log_open(octstr_get_cstr(filename), 0, GW_NON_EXCL);
		octstr_dump(wml_binary, 0);
		log_close_all();
		fp = fopen(octstr_get_cstr(filename), "a");
	    } else if (outputti != BINARY_OUT)
		octstr_dump(wml_binary, 0);
	    else 
		octstr_print(fp, wml_binary);

	    if (outputti != BINARY_OUT) {
		octstr_destroy(output);
		output = octstr_format("\n And as a text: \n\n");
		octstr_print(fp, output);
      
		octstr_pretty_print(fp, wml_binary);
		octstr_destroy(output);
		output = octstr_format("\n\n");
		octstr_print(fp, output);
	    }
	}

	octstr_destroy(wml_text);
	octstr_destroy(output);
	octstr_destroy(wml_binary);
    }
	
    if (file) {
	fclose(fp);
	octstr_destroy(filename);
    }
    
    if (charset != NULL)
	octstr_destroy(charset);

    wml_shutdown();
    gwlib_shutdown();
    
    return ret;
}
