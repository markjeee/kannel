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
 * test_hmac.c - calculates the HMAC-SHA1 hash value
 * 
 * This algorithm is used in the OTA Prov architecture for bootstrap
 * securtiy by means of a shared secret.
 * 
 * References:
 *   - WAP-184-PROVBOOT-20010314a.pdf (Provisioning Bootstrap), WAP Forum 
 *   - HMAC: Keyed-Hashing for Message Authentication”, Krawczyk, H., 
 *     Bellare, M., and Canetti, R., RFC 2104
 *
 * Stipe Tolj <stolj@wapme.de>
 */

#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "gwlib/gwlib.h"

#ifdef HAVE_LIBSSL
#include <openssl/hmac.h>
#endif

#ifndef EVP_MAX_MD_SIZE
#define EVP_MAX_MD_SIZE 1
#endif

int main(int argc, char **argv)
{
    Octstr *data, *filename, *mac, *key;
    unsigned char macbuf[EVP_MAX_MD_SIZE], *p;
    int mac_len;
#ifdef HAVE_LIBSSL
    HMAC_CTX ctx;
#endif

    gwlib_init();

    get_and_set_debugs(argc, argv, NULL);

    if (argc < 3)
        panic(0, "Syntax: %s <key> <file>\n", argv[0]);
  
    key = octstr_create(argv[1]);    
    filename = octstr_create(argv[2]);
    data = octstr_read_file(octstr_get_cstr(filename));

    if (data == NULL)
        panic(0, "Cannot read file.");

    debug("",0,"Dumping file `%s':", octstr_get_cstr(filename));
    octstr_dump(data, 0);

#ifdef HAVE_LIBSSL
    HMAC_Init(&ctx, octstr_get_cstr(key), octstr_len(key), EVP_sha1());
    p = HMAC(EVP_sha1(), octstr_get_cstr(key), octstr_len(key), 
         octstr_get_cstr(data), octstr_len(data), 
         macbuf, &mac_len);
    HMAC_cleanup(&ctx);
#else
    macbuf[0] = 0;
    mac_len = 0;
    p = macbuf;
    warning(0, "No SSL support. Can't calculate HMAC value.");
#endif
    
    mac = octstr_create_from_data(p, mac_len);
    octstr_binary_to_hex(mac, 0);
    
    debug("",0,"HMAC of file `%s' and key `%s' is:", 
          octstr_get_cstr(filename), octstr_get_cstr(key));
    octstr_dump(mac, 0);      

    octstr_destroy(data);
    octstr_destroy(mac);
    octstr_destroy(key);
    gwlib_shutdown();
    return 0;
}
