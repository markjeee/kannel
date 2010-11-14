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
 * utils.c - generally useful, non-application specific functions for Gateway
 *
 */

#include "gw-config.h"
 
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>
#include <libgen.h>

#include "gwlib.h"

/* Headers required for the version dump. */
#if defined(HAVE_LIBSSL) || defined(HAVE_WTLS_OPENSSL) 
#include <openssl/opensslv.h>
#endif
#ifdef HAVE_MYSQL 
#include <mysql_version.h>
#include <mysql.h>
#endif
#ifdef HAVE_SQLITE 
#include <sqlite.h>
#endif
#ifdef HAVE_SQLITE3 
#include <sqlite3.h>
#endif
#ifdef HAVE_ORACLE 
#include <oci.h>
#endif


/* pid of child process when parachute is used */
static pid_t child_pid = -1;
/* saved child signal handlers */
static struct sigaction child_actions[32];
/* just a flag that child signal handlers are stored */
static int child_actions_init = 0;
/* our pid file name */
static char *pid_file = NULL;
static volatile sig_atomic_t parachute_shutdown = 0;


static void parachute_sig_handler(int signum)
{
    info(0, "Signal %d received, forward to child pid (%ld)", signum, (long) child_pid);

    /* we do not handle any signal, just forward these to child process */
    if (child_pid != -1 && getpid() != child_pid)
        kill(child_pid, signum);

    /* if signal received and no child there, terminating */
    switch(signum) {
        case SIGTERM:
        case SIGINT:
        case SIGABRT:
            if (child_pid == -1)
                exit(0);
            else
                parachute_shutdown = 1;
    }
}

static void parachute_init_signals(int child)
{
    struct sigaction sa;

    if (child_actions_init && child) {
        sigaction(SIGTERM, &child_actions[SIGTERM], NULL);
        sigaction(SIGQUIT, &child_actions[SIGQUIT], NULL);
        sigaction(SIGINT,  &child_actions[SIGINT], NULL);
        sigaction(SIGABRT, &child_actions[SIGABRT], NULL);
        sigaction(SIGHUP,  &child_actions[SIGHUP], NULL);
        sigaction(SIGALRM, &child_actions[SIGALRM], NULL);
        sigaction(SIGUSR1, &child_actions[SIGUSR1], NULL);
        sigaction(SIGUSR2, &child_actions[SIGUSR2], NULL);
        sigaction(SIGPIPE, &child_actions[SIGPIPE], NULL);
    }
    else if (!child && !child_actions_init) {
        sa.sa_flags = 0;
        sigemptyset(&sa.sa_mask);
        sa.sa_handler = parachute_sig_handler;
        sigaction(SIGTERM, &sa, &child_actions[SIGTERM]);
        sigaction(SIGQUIT, &sa, &child_actions[SIGQUIT]);
        sigaction(SIGINT,  &sa, &child_actions[SIGINT]);
        sigaction(SIGABRT, &sa, &child_actions[SIGABRT]);
        sigaction(SIGHUP,  &sa, &child_actions[SIGHUP]);
        sigaction(SIGALRM, &sa, &child_actions[SIGALRM]);
        sigaction(SIGUSR1, &sa, &child_actions[SIGUSR1]);
        sigaction(SIGUSR2, &sa, &child_actions[SIGUSR2]);
        sa.sa_handler = SIG_IGN;
        sigaction(SIGPIPE, &sa, &child_actions[SIGPIPE]);
        sigaction(SIGTTOU, &sa, NULL);
        sigaction(SIGTTIN, &sa, NULL);
        sigaction(SIGTSTP, &sa, NULL);
        child_actions_init = 1;
    }
    else
        panic(0, "Child process signal handlers not initialized before.");
}

static int is_executable(const char *filename)
{
    struct stat buf;

    if (stat(filename, &buf)) {
        error(errno, "Error while stat of file `%s'", filename);
        return 0;
    }
    if (!S_ISREG(buf.st_mode) && !S_ISLNK(buf.st_mode)) {
        error(0, "File `%s' is not a regular file.", filename);
        return 0;
    }
    /* others has exec permission */
    if (S_IXOTH & buf.st_mode) return 1;
    /* group has exec permission */
    if ((S_IXGRP & buf.st_mode) && buf.st_gid == getgid())
        return 1;
    /* owner has exec permission */
    if ((S_IXUSR & buf.st_mode) && buf.st_uid == getuid())
        return 1;

    return 0;
}

/*
 * become daemon.
 * returns 0 for father process; 1 for child process
 */
static int become_daemon(void)
{
    int fd;
    if (getppid() != 1) {
       signal(SIGTTOU, SIG_IGN);
       signal(SIGTTIN, SIG_IGN);
       signal(SIGTSTP, SIG_IGN);
       if (fork())
          return 0;
       setsid();
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    fd = open("/dev/null", O_RDWR); /* stdin */
    if (fd == -1)
        panic(errno, "Could not open `/dev/null'");
    dup(fd); /* stdout */
    dup(fd); /* stderr */

    chdir("/");
    return 1;
}

#define PANIC_SCRIPT_MAX_LEN 4096

static PRINTFLIKE(2,3) void execute_panic_script(const char *panic_script, const char *format, ...)
{
    char *args[3];
    char buf[PANIC_SCRIPT_MAX_LEN + 1];
    va_list ap;

    va_start(ap, format);
    vsnprintf(buf, PANIC_SCRIPT_MAX_LEN, format, ap);
    va_end(ap);

    if (fork())
       return;

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    args[0] = (char*) panic_script;
    args[1] = buf;
    args[2] = NULL;

    execv(args[0], args);
}


static void parachute_start(const char *myname, const char *panic_script) {
    time_t last_start = 0, last_panic = 0;
    long respawn_count = 0;
    int status;


    if (panic_script && !is_executable(panic_script))
        panic(0, "Panic script `%s' is not executable for us.", panic_script);

    /* setup sighandler */
    parachute_init_signals(0);

    for (;;) {
        if (respawn_count > 0 && difftime(time(NULL), last_start) < 10) {
            error(0, "Child process died too fast, disabling for 30 sec.");
            gwthread_sleep(30.0);
        }
        if (!(child_pid = fork())) { /* child process */
            parachute_init_signals(1); /* reset sighandlers */
	    return;
        }
	else if (child_pid < 0) {
	    error(errno, "Could not start child process! Will retry in 5 sec.");
	    gwthread_sleep(5.0);
            continue;
	}
	else { /* father process */
	    time(&last_start);
            info(0, "Child process with PID (%ld) started.", (long) child_pid);
            do {
                if (waitpid(child_pid, &status, 0) == child_pid) {
                    /* check here why child terminated */
                   if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                       info(0, "Child process exited gracefully, exit...");
                       gwlib_shutdown();
                       exit(0);
                   }
                   else if (WIFEXITED(status)) {
                       error(0, "Caught child PID (%ld) which died with return code %d",
                           (long) child_pid, WEXITSTATUS(status));
                       child_pid = -1;
                   }
                   else if (WIFSIGNALED(status)) {
                       error(0, "Caught child PID (%ld) which died due to signal %d",
                           (long) child_pid, WTERMSIG(status));
                       child_pid = -1;
                   }
                }
                else if (errno != EINTR) {
                    error(errno, "Error while waiting of child process.");
                }
            } while(child_pid > 0);

            if (parachute_shutdown) {
                /* may only happens if child process crashed while shutdown */
                info(0, "Child process crashed while shutdown. Exiting due to signal...");
                info(0, "Going into gwlib_shutdown...");
                gwlib_shutdown();
                info(0, "gwlib_shutdown done... Bye bye...");
                exit(WIFEXITED(status) ? WEXITSTATUS(status) : 0);
            }

            /* check whether it's panic while start */
            if (respawn_count == 0 && difftime(time(NULL), last_start) < 2) {
                info(0, "Child process crashed while starting. Exiting...");
                info(0, "Going into gwlib_shutdown...");
                gwlib_shutdown();
                info(0, "gwlib_shutdown done... Bye bye...");
                exit(WIFEXITED(status) ? WEXITSTATUS(status) : 1);
            }

            respawn_count++;
            if (panic_script && myname && difftime(time(NULL), last_panic) > 300) {
                time(&last_panic);
                debug("kannel", 0, "Executing panic script: %s %s %ld", panic_script, myname, respawn_count);
                execute_panic_script(panic_script, "%s %ld", myname, respawn_count);
            }
            /* sleep a while to get e.g. sockets released */
            gwthread_sleep(5.0);
	}
    }
}


static void write_pid_file(void)
{
    int fd;
    FILE *file;

    if (!pid_file)
        return;

    fd = open(pid_file, O_WRONLY|O_NOCTTY|O_TRUNC|O_CREAT|O_EXCL, 0644);
    if (fd == -1)
        panic(errno, "Could not open pid-file `%s'", pid_file);

    file = fdopen(fd, "w");
    if (!file)
        panic(errno, "Could not open file-stream `%s'", pid_file);

    fprintf(file, "%ld\n", (long) getpid());
    fclose(file);
}

static void remove_pid_file(void)
{
    if (!pid_file)
        return;

    /* ensure we don't called from child process */
    if (child_pid == 0)
        return;

    if (-1 == unlink(pid_file))
        error(errno, "Could not unlink pid-file `%s'", pid_file);
}


static int change_user(const char *user)
{
    struct passwd *pass;

    if (!user)
        return -1;

    pass = getpwnam(user);
    if (!pass) {
        error(0, "Could not find a user `%s' in system.", user);
        return -1;
    }

    if (-1 == setgid(pass->pw_gid)) {
        error(errno, "Could not change group id from %ld to %ld.", (long) getgid(), (long) pass->pw_gid);
        return -1;
    }

#ifndef __INTERIX
    if (initgroups(user, -1) == -1) {
        error(errno, "Could not set supplementary group ID's.");
    }
#endif
    
    if (-1 == setuid(pass->pw_uid)) {
        error(errno, "Could not change user id from %ld to %ld.", (long) getuid(), (long) pass->pw_uid);
        return -1;
    }

    return 0;
}

/*
 * new datatype functions
 */


MultibyteInt get_variable_value(Octet *source, int *len)
{
    MultibyteInt retval = 0;
    
    for(*len=1;; (*len)++, source++) {
	retval = retval * 0x80 + (*source & 0x7F);
	if (*source < 0x80)  /* if the continue-bit (high bit) is not set */
	    break;
    }
    return retval;
}


int write_variable_value(MultibyteInt value, Octet *dest)
{
    int i, loc = 0;
    Octet revbuffer[20];	/* we write it backwards */
    
    for (;;) {
	revbuffer[loc++] = (value & 0x7F) + 0x80;	
	if (value >= 0x80)
	    value = value >> 7;
	else
	    break;
    }
    for(i=0; i < loc; i++)		/* reverse the buffer */
	dest[i] = revbuffer[loc-i-1];
    
    dest[loc-1] &= 0x7F;	/* remove trailer-bit from last */

    return loc;
}


Octet reverse_octet(Octet source)
{
    Octet	dest;
    dest = (source & 1) <<7;
    dest += (source & 2) <<5;
    dest += (source & 4) <<3;
    dest += (source & 8) <<1;
    dest += (source & 16) >>1;
    dest += (source & 32) >>3;
    dest += (source & 64) >>5;
    dest += (source & 128) >>7;
    
    return dest;
}


void report_versions(const char *boxname)
{
    Octstr *os;
    
    os = version_report_string(boxname);
    debug("gwlib.gwlib", 0, "%s", octstr_get_cstr(os));
    octstr_destroy(os);
}


Octstr *version_report_string(const char *boxname)
{
    struct utsname u;

    uname(&u);
    return octstr_format(GW_NAME " %s version `%s'.\nBuild `%s', compiler `%s'.\n"
                         "System %s, release %s, version %s, machine %s.\n"
             "Hostname %s, IP %s.\n"
             "Libxml version %s.\n"
#ifdef HAVE_LIBSSL
             "Using "
#ifdef HAVE_WTLS_OPENSSL
             "WTLS library "
#endif
             "%s.\n"
#endif
#ifdef HAVE_MYSQL
             "Compiled with MySQL %s, using MySQL %s.\n"
#endif
#ifdef HAVE_SDB
             "Using LibSDB %s.\n"
#endif
#if defined(HAVE_SQLITE) || defined(HAVE_SQLITE3)
             "Using SQLite %s.\n"
#endif
#ifdef HAVE_ORACLE
#if defined(OCI_MAJOR_VERSION) && defined(OCI_MINOR_VERSION)
             "Using Oracle OCI %d.%d.\n"
#else
             "Using Oracle OCI.\n"
#endif
#endif
             "Using %s malloc.\n",
             boxname, GW_VERSION,
#ifdef __GNUC__ 
             (__DATE__ " " __TIME__) ,
             __VERSION__,
#else 
             "unknown" , "unknown",
#endif 
             u.sysname, u.release, u.version, u.machine,
             octstr_get_cstr(get_official_name()),
             octstr_get_cstr(get_official_ip()),
             LIBXML_DOTTED_VERSION,
#ifdef HAVE_LIBSSL
             OPENSSL_VERSION_TEXT,
#endif
#ifdef HAVE_MYSQL
             MYSQL_SERVER_VERSION, mysql_get_client_info(),
#endif
#ifdef HAVE_SDB
             LIBSDB_VERSION,
#endif
#if defined(HAVE_SQLITE) || defined(HAVE_SQLITE3)
             SQLITE_VERSION,
#endif
#ifdef HAVE_ORACLE
#if defined(OCI_MAJOR_VERSION) && defined(OCI_MINOR_VERSION)
             OCI_MAJOR_VERSION, OCI_MINOR_VERSION,
#endif
#endif
             octstr_get_cstr(gwmem_type()));
}


int get_and_set_debugs(int argc, char **argv,
		       int (*find_own) (int index, int argc, char **argv))
{
    int i, ret = -1;
    int debug_lvl = -1;
    int file_lvl = GW_DEBUG;
    char *log_file = NULL;
    char *debug_places = NULL;
    char *panic_script = NULL, *user = NULL;
    int parachute = 0, daemonize = 0;

    for (i=1; i < argc; i++) {
        if (strcmp(argv[i],"-v")==0 || strcmp(argv[i],"--verbosity")==0) {
            if (i+1 < argc) {
                debug_lvl = atoi(argv[i+1]);
                i++;
            } else
                panic(0, "Missing argument for option %s\n", argv[i]); 
        } else if (strcmp(argv[i],"-F")==0 || strcmp(argv[i],"--logfile")==0) {
            if (i+1 < argc && *(argv[i+1]) != '-') {
                log_file = argv[i+1];
                i++;
            } else
                panic(0, "Missing argument for option %s\n", argv[i]); 
        } else if (strcmp(argv[i],"-V")==0 || strcmp(argv[i],"--fileverbosity")==0) {
            if (i+1 < argc) {
                file_lvl = atoi(argv[i+1]);
                i++;
            } else
                panic(0, "Missing argument for option %s\n", argv[i]); 
        } else if (strcmp(argv[i],"-D")==0 || strcmp(argv[i],"--debug")==0) {
            if (i+1 < argc) {
                debug_places = argv[i+1];
                i++;
            } else
                panic(0, "Missing argument for option %s\n", argv[i]); 
        } else if (strcmp(argv[i], "-X")==0 || strcmp(argv[i], "--panic-script")==0) {
            if (i+1 < argc) {
                panic_script = argv[i+1];
                i++;
            } else
                panic(0, "Missing argument for option %s\n", argv[i]);
        } else if (strcmp(argv[i], "-P")==0 || strcmp(argv[i], "--parachute")==0) {
            parachute = 1;
        } else if (strcmp(argv[i], "-d")==0 || strcmp(argv[i], "--daemonize")==0) {
            daemonize = 1;
        } else if (strcmp(argv[i], "-p")==0 || strcmp(argv[i], "--pid-file")==0) {
            if (i+1 < argc) {
                pid_file = argv[i+1];
                i++;
            } else
                panic(0, "Missing argument for option %s\n", argv[i]);
        } else if (strcmp(argv[i], "-u")==0 || strcmp(argv[i], "--user")==0) {
            if (i+1 < argc) {
                user = argv[i+1];
                i++;
            } else
                panic(0, "Missing argument for option %s\n", argv[i]);
        } else if (strcmp(argv[i], "-g")==0 || strcmp(argv[i], "--generate")==0) {
            cfg_dump_all();
            exit(0);
        } else if (strcmp(argv[i], "--version")==0) {
            Octstr *version = version_report_string(basename(argv[0]));
            printf("%s", octstr_get_cstr(version));
            octstr_destroy(version);
            exit(0);
        } else if (strcmp(argv[i],"--")==0) {
            i++;
            break;
        } else if (*argv[i] != '-') {
            break;
        } else {
            if (find_own != NULL) {
           	ret = find_own(i, argc, argv);
        }
        if (ret < 0) {
            fprintf(stderr, "Unknown option %s, exiting.\n", argv[i]);
            panic(0, "Option parsing failed");
        } else
            i += ret;	/* advance additional args */
        }
    }

    if (user && -1 == change_user(user))
        panic(0, "Could not change to user `%s'.", user);

    /* deamonize */
    if (daemonize && !become_daemon())
       exit(0);

    if (pid_file) {
        write_pid_file();
        atexit(remove_pid_file);
    }

    if (parachute) {
        /*
         * if we are running as daemon so open syslog
         * in order not to deal with i.e. log rotate.
         */
        if (daemonize) {
            char *ident = strrchr(argv[0], '/');
            if (!ident)
                ident = argv[0];
            else
                ident++;
            log_set_syslog(ident, (debug_lvl > -1 ? debug_lvl : 0));
        }
        parachute_start(argv[0], panic_script);
        /* now we are in child process so close syslog */
        if (daemonize)
            log_close_all();
    }

    if (debug_lvl > -1)
        log_set_output_level(debug_lvl);
    if (debug_places != NULL)
        log_set_debug_places(debug_places);
    if (log_file != NULL)
        log_open(log_file, file_lvl, GW_NON_EXCL);

    info(0, "Debug_lvl = %d, log_file = %s, log_lvl = %d",
         debug_lvl, log_file ? log_file : "<none>", file_lvl);
    if (debug_places != NULL)
	    info(0, "Debug places: `%s'", debug_places);

    return i;
}


static int pattern_matches_ip(Octstr *pattern, Octstr *ip)
{
    long i, j;
    long pat_len, ip_len;
    int pat_c, ip_c;
    
    pat_len = octstr_len(pattern);
    ip_len = octstr_len(ip);

    i = 0;
    j = 0;
    while (i < pat_len && j < ip_len) {
	pat_c = octstr_get_char(pattern, i);
	ip_c = octstr_get_char(ip, j);
	if (pat_c == ip_c) {
	    /* The characters match, go to the next ones. */
	    ++i;
	    ++j;
	} else if (pat_c != '*') {
	    /* They differ, and the pattern isn't a wildcard one. */
	    return 0;
	} else {
	    /* We found a wildcard in the pattern. Skip in ip. */
	    ++i;
	    while (j < ip_len && ip_c != '.') {
		++j;
		ip_c = octstr_get_char(ip, j);
	    }
	}
    }
    
    if (i >= pat_len && j >= ip_len)
    	return 1;
    return 0;
}


static int pattern_list_matches_ip(Octstr *pattern_list, Octstr *ip)
{
    List *patterns;
    Octstr *pattern;
    int matches;

    patterns = octstr_split(pattern_list, octstr_imm(";"));
    matches = 0;

    while (!matches && (pattern = gwlist_extract_first(patterns)) != NULL) {
	matches = pattern_matches_ip(pattern, ip);
	octstr_destroy(pattern);
    }
    
    gwlist_destroy(patterns, octstr_destroy_item);
    return matches;
}


int is_allowed_ip(Octstr *allow_ip, Octstr *deny_ip, Octstr *ip)
{
    if (ip == NULL)
	return 0;

    if (octstr_len(deny_ip) == 0)
	return 1;

    if (allow_ip != NULL && pattern_list_matches_ip(allow_ip, ip))
	return 1;

    if (pattern_list_matches_ip(deny_ip, ip))
    	return 0;

    return 1;
}


int connect_denied(Octstr *allow_ip, Octstr *ip)
{
    if (ip == NULL)
	return 1;

    /* If IP not set, allow from Localhost */
    if (allow_ip == NULL) { 
	if (pattern_list_matches_ip(octstr_imm("127.0.0.1"), ip))
	    return 0;
    } else {
	if (pattern_list_matches_ip(allow_ip, ip))
	    return 0;
    }
    return 1;
}


int does_prefix_match(Octstr *prefix, Octstr *number)
{
    /* XXX modify to use just octstr operations
     */
    char *b, *p, *n;

    gw_assert(prefix != NULL);
    gw_assert(number != NULL);

    p = octstr_get_cstr(prefix);
    n = octstr_get_cstr(number);
    

    while (*p != '\0') {
        b = n;
        for (b = n; *b != '\0'; b++, p++) {
            if (*p == ';' || *p == '\0') {
                return 1;
            }
            if (*p != *b) break;
        }
        if (*p == ';' || *p == '\0') {
            return 1;
        }
        while (*p != '\0' && *p != ';')
            p++;
        while (*p == ';') p++;
    }
    return 0;
}


int normalize_number(char *dial_prefixes, Octstr **number)
{
    char *t, *p, *official, *start;
    int len, official_len;
    
    if (dial_prefixes == NULL || dial_prefixes[0] == '\0')
        return 0;

    t = official = dial_prefixes;
    official_len = 0;

    gw_assert(number != NULL);
    
    while(1) {

    	p = octstr_get_cstr(*number);
        for(start = t, len = 0; ; t++, p++, len++)
	{
            if (*t == ',' || *t == ';' || *t == '\0') {
                if (start != official) {
                    Octstr *nstr;
		    long n;
		    
		    if ( official[0] == '-' ) official_len=0;
		    n = official_len;
		    if (strlen(official) < (size_t) n)
		    	n = strlen(official);
                    nstr = octstr_create_from_data(official, n);
                    octstr_insert_data(nstr, official_len,
                                           octstr_get_cstr(*number) + len,
                                           octstr_len(*number) - len);
                    octstr_destroy(*number);
                    *number = nstr;
                }
                return 1;
            }
            if (*p == '\0' || *t != *p)
                break;          /* not matching */
        }
        for(; *t != ',' && *t != ';' && *t != '\0'; t++, len++)
            ;
        if (*t == '\0') break;
        if (start == official) official_len = len;
        if (*t == ';') official = t+1;
        t++;
    }
    return 0;
}





long decode_network_long(unsigned char *data) {
        return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
}


void encode_network_long(unsigned char *data, unsigned long value) {
        data[0] = (value >> 24) & 0xff;
        data[1] = (value >> 16) & 0xff;
        data[2] = (value >> 8) & 0xff;
        data[3] = value & 0xff;
}

/* Something that does the same as GNU cfmakeraw. We don't use cfmakeraw
   so that we always know what it does, and also to reduce configure.in
   complexity. */

void kannel_cfmakeraw (struct termios *tio){
    /* Block until a charactor is available, but it only needs to be one*/
    tio->c_cc[VMIN]    = 1;
    tio->c_cc[VTIME]   = 0;

    /* GNU cfmakeraw sets these flags so we had better too...*/

    /* Control modes */
    /* Mask out character size (CSIZE), then set it to 8 bits (CS8).
     * Enable parity bit generation in both directions (PARENB).
     */
    tio->c_cflag      &= ~(CSIZE|PARENB);
    tio->c_cflag      |= CS8;

    /* Input Flags,*/
    /* Turn off all input flags that interfere with the byte stream:
     * BRKINT - generate SIGINT when receiving BREAK, ICRNL - translate
     * NL to CR, IGNCR - ignore CR, IGNBRK - ignore BREAK,
     * INLCR - translate NL to CR, IXON - use XON/XOFF flow control,
     * ISTRIP - strip off eighth bit.
     */
    tio->c_iflag &= ~(BRKINT|ICRNL|IGNCR|IGNBRK|INLCR|IXON|ISTRIP);

    /* Other flags,*/
    /* Turn off all local flags that interpret the byte stream:
     * ECHO - echo input chars, ECHONL - always echo NL even if ECHO is off,
     * ICANON - enable canonical mode (basically line-oriented mode),
     * IEXTEN - enable implementation-defined input processing,
     * ISIG - generate signals when certain characters are received. */
    tio->c_lflag      &= ~(ECHO|ECHONL|ICANON|IEXTEN|ISIG);

    /* Output flags,*/
    /* Disable implementation defined processing on the output stream*/
    tio->c_oflag      &= ~OPOST;
}


int gw_isdigit(int c)
{
    return isdigit(c);
}


int gw_isxdigit(int c)
{
    return isxdigit(c);
}


/* Rounds up the result of a division */
int roundup_div(int a, int b)
{
    int t;

    t = a / b;
    if (t * b != a)
        t += 1;

    return t;
}


unsigned long long gw_generate_id(void)
{
    /* create a 64 bit unique Id by putting a 32 bit epoch time value
     * and a 32 bit random value together */
    unsigned long random, timer;
     
    random = gw_rand();
    timer = (unsigned long)time(NULL);
    
    return ((unsigned long long)timer << 32) + random;
}

