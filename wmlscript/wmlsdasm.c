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
 * wmlsdasm.c
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 2000 WAPIT OY LTD.
 *		 All rights reserved.
 *
 * Disassembler for WMLScript byte-code.
 *
 */

#include "wsint.h"
#include "gwlib/gwlib.h"

#include <sys/stat.h>

/* TODO:
     - print pragmas
     - option to disassemble only a named external function
     - print constants in assembler => wsasm.c */

/********************* Prototypes for static functions ******************/

/* Print usage message to the stdout. */
static void usage(void);

/* Lookup the name of the function `index' from the function names
   section of the byte-code structure `bc'.  The function returns NULL
   if the function is internal. */
const char *lookup_function(WsBc *bc, WsUInt8 index);

/********************* Static variables *********************************/

/* The name of the compiler program. */
static char *program;

/********************* Global functions *********************************/

int main(int argc, char *argv[])
{
    int i;
    int opt;
    WsBool print_constants = WS_FALSE;
    WsBool print_function_names = WS_FALSE;
    WsBool print_functions = WS_FALSE;
    WsUInt8 k;
    WsUInt16 j;

    program = strrchr(argv[0], '/');
    if (program)
        program++;
    else
        program = argv[0];

    /* Process command line arguments. */
    while ((opt = getopt(argc, argv, "cfnh")) != EOF) {
        switch (opt) {
        case 'c':
            print_constants = WS_TRUE;
            break;

        case 'f':
            print_functions = WS_TRUE;
            break;

        case 'n':
            print_function_names = WS_TRUE;
            break;

        case 'h':
            usage();
            exit(0);
            break;

        case '?':
            printf("Try `%s -h' for a complete list of options.\n",
                   program);
            exit(1);
        }
    }

    for (i = optind; i < argc; i++) {
        FILE *fp;
        struct stat stat_st;
        unsigned char *data;
        size_t data_len;
        WsBc *bc;

        if (stat(argv[i], &stat_st) < 0) {
            fprintf(stderr, "%s: could not access `%s': %s\n",
                    program, argv[i], strerror(errno));
            exit(1);
        }
        data_len = stat_st.st_size;

        data = ws_malloc(data_len);
        if (data == NULL) {
            fprintf(stderr, "%s: out of memory: %s\n",
                    program, strerror(errno));
            exit(1);
        }

        fp = fopen(argv[i], "rb");
        if (fp == NULL) {
            fprintf(stderr, "%s: could not open input file `%s': %s\n",
                    program, argv[i], strerror(errno));
            exit(1);
        }

        if (fread(data, 1, data_len, fp) != data_len) {
            fprintf(stderr, "%s: could not read file `%s': %s\n",
                    program, argv[i], strerror(errno));
            exit(1);
        }
        fclose(fp);

        /* Decode byte-code. */
        bc = ws_bc_decode(data, data_len);
        if (bc == NULL) {
            fprintf(stderr, "%s: invalid byte-code file `%s'\n",
                    program, argv[i]);
            continue;
        }

        /* Print all requested data. */
        printf("\n%s:\t%lu bytes\n\n", argv[i], (unsigned long) data_len);

        if (print_constants) {
            printf("Disassembly of section Constants:\n\n");
            for (j = 0; j < bc->num_constants; j++) {
                printf("%4d:\t", j);
                switch (bc->constants[j].type) {
                case WS_BC_CONST_TYPE_INT:
                    printf("%ld\n", (long) bc->constants[j].u.v_int);
                    break;

                case WS_BC_CONST_TYPE_FLOAT32:
                    printf("%f\n", bc->constants[j].u.v_float);
                    break;

                case WS_BC_CONST_TYPE_FLOAT32_NAN:
                    printf("NaN\n");
                    break;

                case WS_BC_CONST_TYPE_FLOAT32_POSITIVE_INF:
                    printf("+infinity\n");
                    break;

                case WS_BC_CONST_TYPE_FLOAT32_NEGATIVE_INF:
                    printf("-infinity\n");
                    break;

                case WS_BC_CONST_TYPE_UTF8_STRING:
                    {
                        size_t pos = 0;
                        size_t column = 8;
                        unsigned long ch;

                        printf("\"");

                        while (ws_utf8_get_char(&bc->constants[j].u.v_string,
                                                &ch, &pos)) {
                            if (ch < ' ') {
                                printf("\\%02lx", ch);
                                column += 3;
                            } else if (ch <= 0xff) {
                                printf("%c", (unsigned char) ch);
                                column++;
                            } else {
                                printf("\\u%04lx", ch);
                                column += 5;
                            }
                            if (column >= 75) {
                                printf("\"\n\t\"");
                                column = 8;
                            }
                        }
                        printf("\"\n");
                    }
                    break;

                case WS_BC_CONST_TYPE_EMPTY_STRING:
                    printf("\"\"\n");
                    break;
                }
            }
        }

        if (print_function_names) {
            printf("Disassembly of section Function names:\n\n");
            for (k = 0; k < bc->num_function_names; k++)
                printf("  %-40.40s%8d\n",
                       bc->function_names[k].name,
                       bc->function_names[k].index);
        }

        if (print_functions) {
            WsCompilerPtr compiler = ws_create(NULL);

            printf("Disassembly of section Functions:\n");

            for (k = 0; k < bc->num_functions; k++) {
                const char *name = lookup_function(bc, k);

                printf("\nFunction %u", (unsigned int) k);

                if (name)
                    printf(" <%s>", name);

                printf(":\n");

                ws_asm_dasm(compiler, bc->functions[k].code,
                            bc->functions[k].code_size);
            }

            ws_destroy(compiler);
        }

        if (!print_constants && !print_function_names && !print_functions) {
            printf("Sections:\n\
                   Name\t\t   Items\n\
                   Constants\t%8d\n\
                   Pragmas\t%8d\n\
                   Function names\t%8d\n\
                   Functions\t%8d\n",
                   bc->num_constants,
                   bc->num_pragmas,
                   bc->num_function_names,
                   bc->num_functions);
        }

        ws_bc_free(bc);
    }

    return 0;
}

/********************* Static functions *********************************/

static void usage(void)
{
    printf("Usage: %s OPTION... FILE...\n\
           \n\
           -c            print constants\n\
           -f            disassemble functions\n\
           -n            print function names\n\
           -h            print this help message and exit successfully\n\
           \n",
           program);
}


const char *lookup_function(WsBc *bc, WsUInt8 index)
{
    WsUInt8 i;

    for (i = 0; i < bc->num_function_names; i++)
        if (bc->function_names[i].index == index)
            return bc->function_names[i].name;

    return NULL;
}
