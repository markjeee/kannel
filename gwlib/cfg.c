/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2010 Kannel Group  
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
 * cfg.c - configuration file handling
 *
 * Lars Wirzenius
 */


#include "gwlib/gwlib.h"

/* for include dir */
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>

struct CfgGroup {
    Octstr *name;
    Dict *vars;
    Octstr *configfile; 
    long line; 
};


static CfgGroup *create_group(void)
{
    CfgGroup *grp;
    
    grp = gw_malloc(sizeof(*grp));
    grp->name = NULL;
    grp->vars = dict_create(64, octstr_destroy_item);
    grp->configfile = NULL; 
    grp->line = 0; 
    return grp;
}

static void destroy_group(void *arg)
{
    CfgGroup *grp;
    
    if (arg != NULL) {
	grp = arg;
	octstr_destroy(grp->name);
	octstr_destroy(grp->configfile); 
	dict_destroy(grp->vars);
	gw_free(grp);
    }
}


struct CfgLoc { 
    Octstr *filename; 
    long line_no; 
    Octstr *line; 
}; 


static CfgLoc *cfgloc_create(Octstr *filename) 
{ 
    CfgLoc *cfgloc; 
     
    cfgloc = gw_malloc(sizeof(*cfgloc)); 
    cfgloc->filename = octstr_duplicate(filename); 
    cfgloc->line_no = 0; 
    cfgloc->line = NULL; 
    return cfgloc; 
} 
 
 
static void cfgloc_destroy(CfgLoc *cfgloc) 
{ 
    if (cfgloc != NULL) { 
	octstr_destroy(cfgloc->filename); 
	octstr_destroy(cfgloc->line); 
	gw_free(cfgloc); 
    } 
} 

 
static void destroy_group_list(void *arg)
{
    gwlist_destroy(arg, destroy_group);
}


static void set_group_name(CfgGroup *grp, Octstr *name)
{
    octstr_destroy(grp->name);
    grp->name = octstr_duplicate(name);
}


struct Cfg {
    Octstr *filename;
    Dict *single_groups;
    Dict *multi_groups;
};


/********************************************************************
 * Section providing hooks to external modules to apply their specific
 * is_allowed_in_group() and is_single_group() with their own
 * foobar-cfg.def.
 */

static List *allowed_hooks;
static List *single_hooks;

static int core_is_allowed_in_group(Octstr *group, Octstr *variable)
{
    Octstr *groupstr;
    
    groupstr = octstr_imm("group");

    #define OCTSTR(name) \
    	if (octstr_compare(octstr_imm(#name), variable) == 0) \
	    return 1;
    #define SINGLE_GROUP(name, fields) \
    	if (octstr_compare(octstr_imm(#name), group) == 0) { \
	    if (octstr_compare(groupstr, variable) == 0) \
		return 1; \
	    fields \
	    return 0; \
	}
    #define MULTI_GROUP(name, fields) \
    	if (octstr_compare(octstr_imm(#name), group) == 0) { \
	    if (octstr_compare(groupstr, variable) == 0) \
		return 1; \
	    fields \
	    return 0; \
	}
    #include "cfg.def"

    /* unknown group identifier */
    return -1;
}


static int core_is_single_group(Octstr *query)
{
    #define OCTSTR(name)
    #define SINGLE_GROUP(name, fields) \
    	if (octstr_compare(octstr_imm(#name), query) == 0) \
	    return 1;
    #define MULTI_GROUP(name, fields) \
    	if (octstr_compare(octstr_imm(#name), query) == 0) \
	    return 0;
    #include "cfg.def"
    return 0;
}


static int is_allowed_in_group(Octstr *group, Octstr *variable)
{
    long i;
    int x, r = -1;

    for (i = 0; i < gwlist_len(allowed_hooks); ++i) {
        x = ((int(*)(Octstr *, Octstr *))
            gwlist_get(allowed_hooks, i))(group, variable);
        r = (x == -1 ? (r == -1 ? x : r) : (r == -1 ? x : r + x));
    }

    return r;
}


static int is_single_group(Octstr *query)
{
    long i;
    int r = 0;

    for (i = 0; i < gwlist_len(single_hooks); ++i) {
        r += ((int(*)(Octstr *))
            gwlist_get(single_hooks, i))(query);
    }

    return (r > 0);
}


void cfg_add_hooks(void *allowed, void *single)
{
    gwlist_append(allowed_hooks, allowed);
    gwlist_append(single_hooks, single);
}


static int add_group(Cfg *cfg, CfgGroup *grp)
{
    Octstr *groupname;
    Octstr *name;
    List *names;
    List *list;
    
    groupname = cfg_get(grp, octstr_imm("group"));
    if (groupname == NULL) {
        error(0, "Group does not contain variable 'group'.");
        return -1;
    }
    set_group_name(grp, groupname);

    names = dict_keys(grp->vars);

    while ((name = gwlist_extract_first(names)) != NULL) {
        int a = is_allowed_in_group(groupname, name);
        switch (a) {
            case 0:
                error(0, "Group '%s' may not contain field '%s'.",
                      octstr_get_cstr(groupname), octstr_get_cstr(name));
                octstr_destroy(name);
                octstr_destroy(groupname);
                gwlist_destroy(names, octstr_destroy_item);
                return -1;
                break;
            case -1:
                error(0, "Group '%s' is no valid group identifier.",
                      octstr_get_cstr(groupname));
                octstr_destroy(name);
                octstr_destroy(groupname);
                gwlist_destroy(names, octstr_destroy_item);
                return -1;
                break;
            default:
                octstr_destroy(name);
                break;
        }
    }
    gwlist_destroy(names, NULL);

    if (is_single_group(groupname)) {
        dict_put(cfg->single_groups, groupname, grp);
    } else {
        list = dict_get(cfg->multi_groups, groupname);
        if (list == NULL) {
            list = gwlist_create();
            dict_put(cfg->multi_groups, groupname, list);
        }
        gwlist_append(list, grp);
    }

    octstr_destroy(groupname);
    return 0;
}


Cfg *cfg_create(Octstr *filename)
{
    Cfg *cfg;
    
    cfg = gw_malloc(sizeof(*cfg));
    cfg->filename = octstr_duplicate(filename);
    cfg->single_groups = dict_create(64, destroy_group);
    cfg->multi_groups = dict_create(64, destroy_group_list);

    return cfg;
}


void cfg_destroy(Cfg *cfg)
{
    if (cfg != NULL) {
        octstr_destroy(cfg->filename);
        dict_destroy(cfg->single_groups);
        dict_destroy(cfg->multi_groups);
        gw_free(cfg);
    }
}


static void parse_value(Octstr *value)
{
    Octstr *temp;
    long len;
    int c;
    
    octstr_strip_blanks(value);

    len = octstr_len(value);
    if (octstr_get_char(value, 0) != '"' || 
        octstr_get_char(value, len - 1) != '"')
	return;

    octstr_delete(value, len - 1, 1);
    octstr_delete(value, 0, 1);

    temp = octstr_duplicate(value);
    octstr_truncate(value, 0);
    
    while (octstr_len(temp) > 0) {
	c = octstr_get_char(temp, 0);
	octstr_delete(temp, 0, 1);
	
    	if (c != '\\' || octstr_len(temp) == 0)
	    octstr_append_char(value, c);
	else {
	    c = octstr_get_char(temp, 0);
	    octstr_delete(temp, 0, 1);

	    switch (c) {
    	    case '\\':
    	    case '"':
	    	octstr_append_char(value, c);
	    	break;
		
    	    default:
	    	octstr_append_char(value, '\\');
	    	octstr_append_char(value, c);
		break;
	    }
	}
    }
    
    octstr_destroy(temp);
}


static List *expand_file(Octstr *file, int forward) 
{
    Octstr *os;
    Octstr *line;
    List *lines; 
    List *expand; 
    long lineno; 
    CfgLoc *loc = NULL; 
 
    os = octstr_read_file(octstr_get_cstr(file)); 
    if (os == NULL) 
    	return NULL; 
 
    lines = octstr_split(os, octstr_imm("\n")); 
    lineno = 0; 
    expand = gwlist_create(); 
              
    while ((line = gwlist_extract_first(lines)) != NULL) {
    	if (loc == NULL) {
            ++lineno; 
            loc = cfgloc_create(file); 
            loc->line_no = lineno;
            loc->line = octstr_create("");
            if (forward) 
                gwlist_append(expand, loc); 
            else 
                gwlist_insert(expand, 0, loc);
        }
        /* check for escape and then add to existing loc */
        if (octstr_get_char(line, octstr_len(line) - 1) == '\\') {
            octstr_delete(line, octstr_len(line) - 1, 1);
            octstr_append(loc->line, line); 
            /* check for second escape */
            if (octstr_get_char(line, octstr_len(line) - 1) == '\\')
                loc = NULL;
        } else {
            octstr_append(loc->line, line);
            loc = NULL;
        }
        octstr_destroy(line);
    } 
    
    /* 
     * add newline at each end of included files to avoid 
     * concatenating different groups by mistake
     */
    if (lineno > 0) {
        loc = cfgloc_create(file); 
        loc->line_no = lineno;
        loc->line = octstr_create("\n");
        if (forward) 
            gwlist_append(expand, loc); 
        else 
            gwlist_insert(expand, 0, loc); 
    }
         
    gwlist_destroy(lines, octstr_destroy_item); 
    octstr_destroy(os); 
 
    return expand; 
} 
 
 
int cfg_read(Cfg *cfg) 
{ 
    CfgLoc *loc; 
    CfgLoc *loc_inc; 
    List *lines;
    List *expand; 
    List *stack; 
    Octstr *name;
    Octstr *value;
    Octstr *filename; 
    CfgGroup *grp;
    long equals;
    long lineno;
    long error_lineno;
    
    loc = loc_inc = NULL;

    /* 
     * expand initial main config file and add it to the recursion 
     * stack to protect against cycling 
     */ 
    if ((lines = expand_file(cfg->filename, 1)) == NULL) { 
        panic(0, "Failed to load main configuration file `%s'. Aborting!", 
              octstr_get_cstr(cfg->filename)); 
    } 
    stack = gwlist_create(); 
    gwlist_insert(stack, 0, octstr_duplicate(cfg->filename)); 

    grp = NULL;
    lineno = 0;
    error_lineno = 0;
    while (error_lineno == 0 && (loc = gwlist_extract_first(lines)) != NULL) { 
        octstr_strip_blanks(loc->line); 
        if (octstr_len(loc->line) == 0) { 
            if (grp != NULL && add_group(cfg, grp) == -1) { 
                error_lineno = loc->line_no; 
                destroy_group(grp); 
            } 
            grp = NULL; 
        } else if (octstr_get_char(loc->line, 0) != '#') { 
            equals = octstr_search_char(loc->line, '=', 0); 
            if (equals == -1) { 
                error(0, "An equals sign ('=') is missing on line %ld of file %s.", 
                      loc->line_no, octstr_get_cstr(loc->filename)); 
                error_lineno = loc->line_no; 
            } else  
             
            /* 
             * check for special config directives, like include or conditional 
             * directives here 
             */ 
            if (octstr_search(loc->line, octstr_imm("include"), 0) != -1) { 
                filename = octstr_copy(loc->line, equals + 1, octstr_len(loc->line)); 
                parse_value(filename); 
 
                /* check if we are cycling */ 
                if (gwlist_search(stack, filename, octstr_item_match) != NULL) { 
                    panic(0, "Recursive include for config file `%s' detected " 
                             "(on line %ld of file %s).", 
                          octstr_get_cstr(filename), loc->line_no,  
                          octstr_get_cstr(loc->filename)); 
                } else {     
                    List *files = gwlist_create();
                    Octstr *file;
                    struct stat filestat;

                    /* check if included file is a directory */
                    if (lstat(octstr_get_cstr(filename), &filestat) != 0) {
                        error(errno, "lstat failed: couldn't stat `%s'", 
                              octstr_get_cstr(filename));
                        panic(0, "Failed to include `%s' "
                              "(on line %ld of file %s). Aborting!",  
                              octstr_get_cstr(filename), loc->line_no,  
                              octstr_get_cstr(loc->filename)); 
                    }
                    
                    /* 
                     * is a directory, create a list with files of
                     * this directory and load all as part of the
                     * whole configuration.
                     */
                    if (S_ISDIR(filestat.st_mode)) {
                        DIR *dh;
                        struct dirent *diritem;

                        debug("gwlib.cfg", 0, "Loading include dir `%s' "
                              "(on line %ld of file %s).",  
                              octstr_get_cstr(filename), loc->line_no,  
                              octstr_get_cstr(loc->filename)); 

                        dh = opendir(octstr_get_cstr(filename));
                        while ((diritem = readdir(dh))) {
                            Octstr *fileitem;

                            fileitem = octstr_duplicate(filename);
                            octstr_append_cstr(fileitem, "/");
                            octstr_append_cstr(fileitem, diritem->d_name);

                            lstat(octstr_get_cstr(fileitem), &filestat);
                            if (!S_ISDIR(filestat.st_mode)) {
                                gwlist_insert(files, 0, fileitem);
                            } else {
                            	octstr_destroy(fileitem);
                            }
                        }
                        closedir(dh);
                    } 
		    
                    /* is a file, create a list with it */
                    else {
                        gwlist_insert(files, 0, octstr_duplicate(filename));
                    }

                    /* include files */
                    while ((file = gwlist_extract_first(files)) != NULL) {

                        gwlist_insert(stack, 0, octstr_duplicate(file)); 
                        debug("gwlib.cfg", 0, "Loading include file `%s' (on line %ld of file %s).",  
                              octstr_get_cstr(file), loc->line_no,  
                              octstr_get_cstr(loc->filename)); 

                        /*  
                         * expand the given include file and add it to the current 
                         * processed main while loop 
                         */ 
                        if ((expand = expand_file(file, 0)) != NULL) {
                            while ((loc_inc = gwlist_extract_first(expand)) != NULL) 
                                gwlist_insert(lines, 0, loc_inc); 
                        } else { 
                            panic(0, "Failed to load whole configuration. Aborting!"); 
                        } 
                 
                        gwlist_destroy(expand, NULL); 
                        cfgloc_destroy(loc_inc);
                        octstr_destroy(file);
                    }
                    gwlist_destroy(files, octstr_destroy_item);
                } 
                octstr_destroy(filename); 
            }  
             
            /* 
             * this is a "normal" line, so process it accodingly 
             */ 
            else  { 
                name = octstr_copy(loc->line, 0, equals); 
                octstr_strip_blanks(name); 
                value = octstr_copy(loc->line, equals + 1, octstr_len(loc->line)); 
                parse_value(value); 
 
    	    	if (grp == NULL)
                    grp = create_group(); 
                 
                if (grp->configfile != NULL) {
                    octstr_destroy(grp->configfile); 
                    grp->configfile = NULL;
                }
                grp->configfile = octstr_duplicate(cfg->filename); 

                cfg_set(grp, name, value); 
                octstr_destroy(name); 
                octstr_destroy(value); 
            } 
        } 

        cfgloc_destroy(loc); 
    }

    if (grp != NULL && add_group(cfg, grp) == -1) {
        error_lineno = 1; 
        destroy_group(grp); 
    }

    gwlist_destroy(lines, NULL); 
    gwlist_destroy(stack, octstr_destroy_item); 

    if (error_lineno != 0) {
        error(0, "Error found on line %ld of file `%s'.",  
	          error_lineno, octstr_get_cstr(cfg->filename)); 
        return -1; 
    }

    return 0;
}


CfgGroup *cfg_get_single_group(Cfg *cfg, Octstr *name)
{
    return dict_get(cfg->single_groups, name);
}


List *cfg_get_multi_group(Cfg *cfg, Octstr *name)
{
    List *list, *copy;
    long i;
    
    list = dict_get(cfg->multi_groups, name);
    if (list == NULL)
    	return NULL;

    copy = gwlist_create();
    for (i = 0; i < gwlist_len(list); ++i)
    	gwlist_append(copy, gwlist_get(list, i));
    return copy;
}


Octstr *cfg_get_group_name(CfgGroup *grp)
{
    return octstr_duplicate(grp->name);
}

Octstr *cfg_get_configfile(CfgGroup *grp)
{
    return octstr_duplicate(grp->configfile);
}


Octstr *cfg_get_real(CfgGroup *grp, Octstr *varname, const char *file, 
    	    	     long line, const char *func)
{
    Octstr *os;

    if(grp == NULL) 
    	panic(0, "Trying to fetch variable `%s' in non-existing group",
	      octstr_get_cstr(varname));

    if (grp->name != NULL && !is_allowed_in_group(grp->name, varname))
    	panic(0, "Trying to fetch variable `%s' in group `%s', not allowed.",
	      octstr_get_cstr(varname), octstr_get_cstr(grp->name));

    os = dict_get(grp->vars, varname);
    if (os == NULL)
    	return NULL;
    return gw_claim_area_for(octstr_duplicate(os), file, line, func);
}


int cfg_get_integer(long *n, CfgGroup *grp, Octstr *varname)
{
    Octstr *os;
    int ret;
    
    os = cfg_get(grp, varname);
    if (os == NULL)
    	return -1;
    if (octstr_parse_long(n, os, 0, 0) == -1)
    	ret = -1;
    else
    	ret = 0;
    octstr_destroy(os);
    return ret;
}


int cfg_get_bool(int *n, CfgGroup *grp, Octstr *varname)
{
    Octstr *os;

    os = cfg_get(grp, varname);
    if (os == NULL) {
	*n = 0;
    	return -1;
    }
    if (octstr_case_compare(os, octstr_imm("true")) == 0
	|| octstr_case_compare(os, octstr_imm("yes")) == 0
	|| octstr_case_compare(os, octstr_imm("on")) == 0
	|| octstr_case_compare(os, octstr_imm("1")) == 0)
    {	    
	*n = 1;
    } else if (octstr_case_compare(os, octstr_imm("false")) == 0
	|| octstr_case_compare(os, octstr_imm("no")) == 0
	|| octstr_case_compare(os, octstr_imm("off")) == 0
	|| octstr_case_compare(os, octstr_imm("0")) == 0)
    {
	*n = 0;
    }
    else {
	*n = 1;
	warning(0, "bool variable set to strange value, assuming 'true'");
    }
    octstr_destroy(os);
    return 0;
}


List *cfg_get_list(CfgGroup *grp, Octstr *varname)
{
    Octstr *os;
    List *list;
    
    os = cfg_get(grp, varname);
    if (os == NULL)
    	return NULL;

    list = octstr_split_words(os);
    octstr_destroy(os);
    return list;
}


void cfg_set(CfgGroup *grp, Octstr *varname, Octstr *value)
{
    dict_put(grp->vars, varname, octstr_duplicate(value));
}


void grp_dump(CfgGroup *grp)
{
    List *names;
    Octstr *name;
    Octstr *value;

    if (grp->name == NULL)
	debug("gwlib.cfg", 0, "  dumping group (name not set):");
    else
	debug("gwlib.cfg", 0, "  dumping group (%s):",
	      octstr_get_cstr(grp->name));
    names = dict_keys(grp->vars);
    while ((name = gwlist_extract_first(names)) != NULL) {
	value = cfg_get(grp, name);
	debug("gwlib.cfg", 0, "    <%s> = <%s>", 
	      octstr_get_cstr(name),
	      octstr_get_cstr(value));
    	octstr_destroy(value);
    	octstr_destroy(name);
    }
    gwlist_destroy(names, NULL);
}


void cfg_dump(Cfg *cfg)
{
    CfgGroup *grp;
    List *list;
    List *names;
    Octstr *name;

    debug("gwlib.cfg", 0, "Dumping Cfg %p", (void *) cfg);
    debug("gwlib.cfg", 0, "  filename = <%s>", 
    	  octstr_get_cstr(cfg->filename));

    names = dict_keys(cfg->single_groups);
    while ((name = gwlist_extract_first(names)) != NULL) {
	grp = cfg_get_single_group(cfg, name);
	if (grp != NULL)
	    grp_dump(grp);
    	octstr_destroy(name);
    }
    gwlist_destroy(names, NULL);

    names = dict_keys(cfg->multi_groups);
    while ((name = gwlist_extract_first(names)) != NULL) {
	list = cfg_get_multi_group(cfg, name);
	while ((grp = gwlist_extract_first(list)) != NULL)
	    grp_dump(grp);
	gwlist_destroy(list, NULL);
    	octstr_destroy(name);
    }
    gwlist_destroy(names, NULL);

    debug("gwlib.cfg", 0, "Dump ends.");
}


void cfg_dump_all(void)
{
    #define OCTSTR(name) \
        printf("%s = <please consult user doc>\n", #name);
    #define SINGLE_GROUP(name, fields) \
        printf("#\n#  Single Group\n#\n"); \
        printf("group = %s\n", #name); \
        fields; \
        printf("\n\n");
    #define MULTI_GROUP(name, fields) \
        printf("#\n#  Multi Group\n#\n"); \
        printf("group = %s\n", #name); \
        fields; \
        printf("\n\n");
    #include "cfg.def"
}


void cfg_init(void)
{
    /* make sure we put our own core hooks into the lists */
    allowed_hooks = gwlist_create();
    single_hooks = gwlist_create();

    gwlist_append(allowed_hooks, &core_is_allowed_in_group);
    gwlist_append(single_hooks, &core_is_single_group);
}


void cfg_shutdown(void)
{
    gwlist_destroy(allowed_hooks, NULL);
    gwlist_destroy(single_hooks, NULL);
    allowed_hooks = single_hooks = NULL;
}
