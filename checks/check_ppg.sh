#!/bin/sh
#
# Use 'test/test_ppg' and 'test/test_http_server' to test PPG. It presumes
# using of http smsc.
#
# Note: Running this script can take quite a long time, if your input does have
# many files. Two (ok ip and sms control document) should be quite enough for 
# a general make check. Use more only if you are interested of detailed test-
# ing of ppg.

set -e
#set -x

host=127.0.0.1
list_port=18082
server_port=18081
push_port=18080
loglevel=0
username="foo"
password="bar"
prefix="test"
contents="sl" 
# Kannel configuration file
conf_file="gw/pushkannel.conf"
# Push content. Use only sl, because compilers should be tested separately
content_file="$prefix/sl.txt"
# Ok ip control files
ip_control_files="$prefix/*iptest*"
# Ok sms control files
sms_control_files="$prefix/*smstest*"
# Erroneous ip control files
wrong_ip_files="$prefix/*witest*"
# Erroneous sms control files
wrong_sms_files="$prefix/*wstest*"
# File containing the blacklist
blacklist="$prefix/blacklist.txt"
# File containing the whitelist
whitelist="$prefix/whitelist.txt"

sleep 1
test/test_http_server -p $list_port -w $whitelist -b $blacklist > check_http_list.log 2>&1 & listid=$!
error=no

# ok control files requesting an ip bearer. Names contain string 'ip'. Bearer-
# box should not use smsc (do a http fetch) when ip bearer is requested.

for control_file in $ip_control_files;
    do 
        if [ -f $control_file ]
        then
            gw/bearerbox -v $loglevel $conf_file > check_bb.tmp 2>&1 & bbpid=$!
            sleep 2 

            gw/wapbox -v $loglevel $conf_file > check_wap.tmp 2>&1 & wappid=$!
            sleep 2

            test/test_ppg -c $contents http://$host:$push_port/cgi-bin/wap-push.cgi?username=$username'&'password=$password $content_file $control_file > check_ppg.tmp 2>&1 
            sleep 1

            if ! grep "and type push response" check_ppg.tmp > /dev/null
            then
                cat check_ppg.tmp >> check_ppg.log 2>&1
                error="yes"
                echo "ppg failed with control file $control_file"
            fi

            if ! grep "Connectionless push accepted" check_wap.tmp > /dev/null
            then
                cat check_wap.tmp >> check_wap.log 2>&1
                error="yes"
                echo "wap failed with control file $control_file"
            fi
        
            if ! grep "got wdp from wapbox" check_bb.tmp > /dev/null
            then
                cat check_bb.tmp >> check_bb.log 2>&1
                error="yes"
                echo "bb failed with control file $control_file"
            fi

            kill -INT $wappid
            sleep 1
            kill -INT $bbpid
            sleep 2

# We can panic when we are going down, too
            if test "$error" != "yes"          
            then
                if grep 'WARNING:|ERROR:|PANIC:' check_bb.tmp > /dev/null
                then
                    cat check_bb.tmp >> check_bb.log 2>&1
                    error="yes"
                    echo "got errors in bb when going down when $control_file"
                fi

                if grep 'WARNING:|ERROR:|PANIC:' check_wap.tmp > /dev/null
                then
                    cat check_wap.tmp >> check_wap.log 2>&1
                    error="yes"
                    echo "got errors in wap when going down when $control_file"
                fi 

                if grep 'WARNING:|ERROR:|PANIC:' check_ppg.tmp > /dev/null
                then
                    cat check_ppg.tmp >> check_ppg.log 2>&1
                    error="yes"
                    echo "got errors in ppg when going down when $control_file"
                fi 
           fi
         
           rm -f check_bb.tmp check_wap.tmp check_ppg.tmp
        fi;
    done

# Erroneous control files requesting an ip bearer. Ppg should reject these and
# report pi. Names contain string 'wi'.

for control_file in $wrong_ip_files;
    do 
        if [ -f $control_file ]
        then
            have_iperrors=yes
            gw/bearerbox -v $loglevel $conf_file > check_bb.tmp 2>&1 & bbpid=$!
            sleep 2 

            gw/wapbox -v $loglevel $conf_file > check_wap.tmp 2>&1 & wappid=$!
            sleep 2

            test/test_ppg -c $contents http://$host:$push_port/cgi-bin/wap-push.cgi?username=$username'&'password=$password $content_file $control_file > check_ppg.tmp 2>&1
            sleep 1

            if ! grep "and type push response" check_ppg.tmp > /dev/null &&
               ! grep "and type bad message response" check_ppg.tmp > /dev/null
            then
                cat check_ppg.tmp >> check_ppg.log 2>&1
                error="yes"
                echo "ppg failed when control file $control_file"
            fi

            if grep "Connectionless push accepted" check_wap.tmp > /dev/null &&
               grep "WARNING" check_wap.tmp > /dev/null 
            then
                cat check_wap.tmp >> check_wap.log 2>&1
                error="yes"
                echo "wap failed when control file $control_file"
            fi
        
            if grep "got wdp from wapbox" check_bb.tmp > /dev/null
            then
                cat check_bb.tmp >> check_bb.log 2>&1
                error="yes"
                echo "bb failed when control file $control_file"
            fi

            kill -INT $wappid
            sleep 1
            kill -INT $bbpid
            sleep 2

# We can panic when we are going down, too
            if test "$error" != "yes"
            then
                if grep 'ERROR:|PANIC:' check_bb.tmp > /dev/null
                then
                    cat check_bb.tmp >> check_bb.log 2>&1
                    error="yes"
                    echo "got errors in bb when going down with $control_file"
                fi

                if grep 'ERROR:|PANIC:' check_wap.tmp > /dev/null
                then
                    cat check_wap.tmp >> check_wap.log 2>&1
                    error="yes"
                    echo "got errors in wap when going down with $control_file"
                fi 

                if grep 'ERROR:|PANIC:' check_ppg.tmp > /dev/null
                then
                    cat check_ppg.tmp >> check_ppg.log 2>&1
                    error="yes"
                    echo "got errors in ppg when going down with $control_file"
                fi 
           fi
         
           rm -f check_bb.tmp check_wap.tmp check_ppg.tmp
        fi;
    done

# Ok control files requesting a sms bearer. Names contain string 'sms'. Ppg
# should use smsc (do a http fetch).

for control_file in $sms_control_files;
    do 
        if [ -f $control_file ]
        then
            test/test_http_server -p $server_port > check_http_sim.tmp 2>&1 & simid=$!
            sleep 1
            gw/bearerbox -v $loglevel $conf_file > check_bb.tmp 2>&1 & bbpid=$!
            sleep 2 
            gw/wapbox -v $loglevel $conf_file > check_wap.tmp 2>&1 & wappid=$!
            sleep 2
            test/test_ppg -c $contents http://$host:$push_port/cgi-bin/wap-push.cgi?username=$username'&'password=$password $content_file $control_file > check_ppg.tmp 2>&1 
            sleep 1

            if ! grep "and type push response" check_ppg.tmp > /dev/null
            then
                cat check_ppg.tmp >> check_ppg.log 2>&1
                error="yes"
                echo "ppg failed with control file $control_file"
            fi

            if ! grep "Connectionless push accepted" check_wap.tmp > /dev/null
            then
                cat check_wap.tmp >> check_wap.log 2>&1
                error="yes"
                echo "wap failed with control file $control_file"
            fi
        
            if ! grep "got sms from wapbox" check_bb.tmp > /dev/null
            then
                cat check_bb.tmp >> check_bb.log 2>&1
                error="yes"
                echo "bb failed with control file $control_file"
            fi
           
            kill -INT $wappid
            sleep 1
            kill -INT $bbpid
            sleep 2
            test/test_http -qv 4 http://$host:$server_port/quit
            sleep 2
# We can panic when we are going down, too
            if test "$error" != "yes"  
            then
                if grep 'WARNING:|ERROR:|PANIC:' check_bb.tmp > /dev/null
                then
                    cat check_bb.tmp >> check_bb.log 2>&1
                    error="yes"
                    echo "got errors in bb when going down with $control_file"
                fi 

                if grep 'WARNING:|ERROR:|PANIC:' check_wap.tmp > /dev/null
                then
                    cat check_wap.tmp >> check_wap.log 2>&1
                    error="yes"
                    echo "got errors in wap when going down with $control_file"
                fi 

                if grep 'WARNING:|ERROR:|PANIC:' check_ppg.tmp > /dev/null
                then
                    cat check_ppg.tmp >> check_ppg.log 2>&1
                    error="yes"
                    echo "got errors in ppg when going down with $control_file"
                fi

                if grep 'WARNING:|ERROR:|PANIC:' check_http_sim.tmp > /dev/null
                then
                    cat check_sim.tmp >> check_sim.log 2>&1
                    error="yes"
                    echo "errors, http_sim when going down with $control_file"
                fi 
            fi
         
            rm -f check_bb.tmp check_wap.tmp check_ppg.tmp check_http_sim.tmp
        fi;
    done

# Erroneous control documents requesting a sms bearer. Ppg should reject these
# and inform pi. Names contain the string 'ws'.

for control_file in $wrong_sms_files;
    do 
        if [ -f $control_file ]
        then
            test/test_http_server -p $server_port > check_http_sim.tmp 2>&1 & simid=$
            sleep 1
            gw/bearerbox -v $loglevel $conf_file > check_bb.tmp 2>&1 & bbpid=$!
            sleep 2 

            gw/wapbox -v $loglevel $conf_file > check_wap.tmp 2>&1 & wappid=$!
            sleep 2

            test/test_ppg -c $contents http://$host:$push_port/cgi-bin/wap-push.cgi?username=$username'&'password=$password $content_file $control_file > check_ppg.tmp 2>&1
            sleep 1

            if ! grep "and type push response" check_ppg.tmp > /dev/null &&
               ! grep "and type bad message response" check_ppg.tmp > /dev/null
            then
                cat check_ppg.tmp >> check_ppg.log 2>&1
                error="yes"
                echo "ppg failed, going down with control file $control_file"
            fi

            if grep "Connectionless push accepted" check_wap.tmp > /dev/null &&
               grep "WARNING" check_wap.tmp > /dev/null 
            then
                cat check_wap.tmp >> check_wap.log 2>&1
                error="yes"
                echo "wap failed, going down with control file $control_file"
            fi
        
            if grep "got sms from wapbox" check_bb.tmp > /dev/null
            then
                cat check_bb.tmp >> check_bb.log 2>&1
                error="yes"
                echo "bb failed, going down with control file $control_file"
            fi

            kill -INT $wappid
            sleep 2
            kill -INT $bbpid
            sleep 1
            test/test_http -qv 4 http://$host:$server_port/quit
            sleep 2

# We can panic when we are going down, too
            if test "$error" != "yes" 
            then
                if grep 'ERROR:|PANIC:' check_bb.tmp > /dev/null
                then
                    cat check_bb.tmp >> check_bb.log 2>&1
                    error="yes"
                    echo "got errors in bb when ending tests"
                fi

                if grep 'ERROR:|PANIC:' check_wap.tmp > /dev/null
                then
                    cat check_wap.tmp >> check_wap.log 2>&1
                    error="yes"
                    echo "got errors in wap when ending tests"
                fi 

                if grep 'ERROR:|PANIC:' check_ppg.tmp > /dev/null
                then
                    cat check_ppg.tmp >> check_ppg.log 2>&1
                    error="yes"
                    echo "got errors in ppg when ending tests"
                fi 

                if grep 'ERROR:|PANIC:' check_http_sim.tmp > /dev/null
                then
                    cat check_http_sim.tmp >> check_http_sim.log 2>&1
                    error="yes"
                    echo "got errors in http_sim when ending tests"
                fi
            fi
         
            rm -f check_bb.tmp check_wap.tmp check_ppg.tmp
        fi;
done

kill -INT $listid
sleep 1
test/test_http -qv 4 http://$host:$list_port/quit
wait

if test "$error" = "yes"
then
        echo "check_ppg failed" 1>&2
	echo "See check_bb.log, check_wap.log, check_ppg.log," 1>&2
	echo "check_http_list.log, check_http_sim.log for info" 1>&2
	exit 1 
fi

rm -f check_bb.log check_wap.log check_ppg.log check_http_list.log check_http_sim.log

exit 0

