dnl acinclude.m4 -- local include for for autoconf
dnl
dnl This file is processed while autoconf generates configure.
dnl This file is part of the Kannel WAP and SMS gateway project.


dnl Check if installed version string is equal or higher then required. 
dnl This is used in a couple of tests to ensure we have a valid version 
dnl of a software package installed. The basic idea is to split the 
dnl version sequences into three parts and then test against eachother
dnl in a whole complex if statement. 
dnl
dnl AC_CHECK_VERSION(installed, required, [do-if-success], [do-if-tail])
dnl
dnl Written by Stipe Tolj <stolj@kannel.org> <st@tolj.org> 
 
AC_DEFUN([AC_CHECK_VERSION], 
[ 
  dnl split installed version string 
  ac_inst_ver_maj=`echo $1 | sed -e 's/^\(.*\)\..*\..*$/\1/'` 
  ac_inst_ver_mid=`echo $1 | sed -e 's/^.*\.\(.*\)\..*$/\1/'` 
  ac_inst_ver_min=`echo $1 | sed -e 's/^.*\..*\.\(.*\)$/\1/'` 
 
  dnl split required version string 
  ac_req_ver_maj=`echo $2 | sed -e 's/^\(.*\)\..*\..*$/\1/'` 
  ac_req_ver_mid=`echo $2 | sed -e 's/^.*\.\(.*\)\..*$/\1/'` 
  ac_req_ver_min=`echo $2 | sed -e 's/^.*\..*\.\(.*\)$/\1/'` 

  dnl now perform the test 
  if test "$ac_inst_ver_maj" -lt "$ac_req_ver_maj" || \
    ( test "$ac_inst_ver_maj" -eq "$ac_req_ver_maj" && \
      test "$ac_inst_ver_mid" -lt "$ac_req_ver_mid" ) || \
    ( test "$ac_inst_ver_mid" -eq "$ac_req_ver_mid" && \
      test "$ac_inst_ver_min" -lt "$ac_req_ver_min" )
  then 
    ac_ver_fail=yes 
  else 
    ac_ver_fail=no 
  fi 
 
  dnl now see if we have to do something 
  ifelse([$3],,, 
  [if test $ac_ver_fail = no; then 
    $3 
   fi]) 
  ifelse([$4],,, 
  [if test $ac_ver_fail = yes; then 
    $4 
   fi]) 
]) 
    

dnl Some optional terminal sequences for configure
dnl Taken from the mod_ssl package by Ralf S. Engelschall.

AC_DEFUN([AC_SET_TERMINAL_SEQUENCES],
[
  case $TERM in
    xterm|xterm*|vt220|vt220*|cygwin)
        T_MD=`echo dummy | awk '{ printf("%c%c%c%c", 27, 91, 49, 109); }'`
        T_ME=`echo dummy | awk '{ printf("%c%c%c", 27, 91, 109); }'`
        ;;
    vt100|vt100*)
        T_MD=`echo dummy | awk '{ printf("%c%c%c%c%c%c", 27, 91, 49, 109, 0, 0); }'`
        T_ME=`echo dummy | awk '{ printf("%c%c%c%c%c", 27, 91, 109, 0, 0); }'`
        ;;
    default)
        T_MD=''
        T_ME=''
        ;;
  esac
])


dnl Display configure section name in bold white letters
dnl if available on the terminal

AC_DEFUN([AC_CONFIG_SECTION],
[
  nl='
'
  echo "${nl}${T_MD}$1 ...${T_ME}"
])


dnl Check which SVN revision is and apply
dnl the value to the given variable

AC_DEFUN([AC_SVN_REVISION],
[
  if test -d ".svn"
  then
    revision=`svnversion .`
    test -z "$revision" && revision="unknown"
    $1="$revision"
  fi
])


dnl Available from the GNU Autoconf Macro Archive at:
dnl http://www.gnu.org/software/ac-archive/htmldoc/ac_caolan_func_which_gethostbyname_r.html
dnl Modified by Alexander Malysh for Kannel Project.

AC_DEFUN([AC_FUNC_WHICH_GETHOSTBYNAME_R],
[AC_CACHE_CHECK(for which type of gethostbyname_r, ac_cv_func_which_gethostname_r, [
AC_TRY_COMPILE([
#include <netdb.h>
  ], [
        char *name;
        struct hostent *he;
        struct hostent_data data;
        (void) gethostbyname_r(name, he, &data);
     ], ac_cv_func_which_gethostname_r=3, [
AC_TRY_COMPILE([
#include <netdb.h>
  ], [
        char *name;
        struct hostent *he, *res;
        char buffer[2048];
        int buflen = 2048;
        int h_errnop;
        (void) gethostbyname_r(name, he, buffer, buflen, &res, &h_errnop);
     ], ac_cv_func_which_gethostname_r=6, [
AC_TRY_COMPILE([
#include <netdb.h>
  ], [
        char *name;
        struct hostent *he;
        char buffer[2048];
        int buflen = 2048;
        int h_errnop;
        (void) gethostbyname_r(name, he, buffer, buflen, &h_errnop);
     ], ac_cv_func_which_gethostname_r=5 , ac_cv_func_which_gethostname_r=0)]
   )]
)])
if test $ac_cv_func_which_gethostname_r -eq 6; then
  AC_DEFINE(HAVE_FUNC_GETHOSTBYNAME_R_6)
elif test $ac_cv_func_which_gethostname_r -eq 5; then
  AC_DEFINE(HAVE_FUNC_GETHOSTBYNAME_R_5)
elif test $ac_cv_func_which_gethostname_r -eq 3; then
  AC_DEFINE(HAVE_FUNC_GETHOSTBYNAME_R_3)
elif test $ac_cv_func_which_gethostname_r -eq 0; then
  ac_cv_func_which_gethostname_r = no
fi
])


dnl Creates a config.nice shell script that contains all given configure
dnl options to the orginal configure call. Can be used to add further options
dnl in additional re-configure calls. This is perfect while handling with a
dnl large number of configure option switches. 
dnl This macro is taken from PHP5 aclocal.m4, Stipe Tolj.

AC_DEFUN([AC_CONFIG_NICE],
[
  test -f $1 && mv $1 $1.old
  rm -f $1.old
  cat >$1<<EOF
#! /bin/sh
#
# Created by configure

EOF

  for var in CFLAGS CXXFLAGS CPPFLAGS LDFLAGS LIBS CC CXX; do
    eval val=\$$var
    if test -n "$val"; then
      echo "$var='$val' \\" >> $1
    fi
  done

  for arg in [$]0 "[$]@"; do
    echo "'[$]arg' \\" >> $1
  done
  echo '"[$]@"' >> $1
  chmod +x $1
])


AC_DEFUN([AC_CVS_DATE],
[
  cvs_date=`grep ChangeLog CVS/Entries | cut -f4 -d/`
  day=`grep ChangeLog CVS/Entries | cut -f4 -d/ | cut -c9-10 | tr " " "0"`
  month=`echo $cvs_date | cut -f2 -d' '`
  case $month in
    "Jan") month="01" ;;
    "Feb") month="02" ;;
    "Mar") month="03" ;;
    "Apr") month="04" ;;
    "May") month="05" ;;
    "Jun") month="06" ;;
    "Jul") month="07" ;;
    "Aug") month="08" ;;
    "Sep") month="09" ;;
    "Oct") month="10" ;;
    "Nov") month="11" ;;
    "Dec") month="12" ;;
  esac
  year=`echo $cvs_date | cut -f5 -d' '`
  $1="$year$month$day"
])
