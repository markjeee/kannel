#!/bin/sh 
# 
# Use `test/test_http{,_server}' to test gwlib/http.c. 
# Incuding the SSL client and server componentes, if, of course, SSL has been
# enabled. 
 
set -e 
#set -x
 
times=2
host=127.0.0.1 
port="8040" 
port_ssl="8041" 
url="http://$host:$port/foo.txt" 
url_ssl="https://$host:$port_ssl/foo.txt" 
quiturl="http://$host:$port/quit" 
quiturl_ssl="https://$host:$port_ssl/quit" 
ssl_cert="gw/cert.pem" 
ssl_key="gw/key.pem" 
ssl_clientcert="/tmp/clientcert.pem" 
loglevel=0
ssl_enabled="yes"
 
cat $ssl_cert $ssl_key > $ssl_clientcert 
 
test/test_http_server -p $port -v $loglevel > check_http_server.log 2>&1 & 
serverpid=$! 
sleep 1 
 
test/test_http_server -p $port_ssl -v $loglevel -s -c $ssl_cert -k $ssl_key > check_https_server.log 2>&1 & 
serverpid_ssl=$!
sleep 1 
 
test/test_http -r $times $url > check_http.log 2>&1 
ret=$?

test/test_http -r 1 -s -c $ssl_clientcert $url_ssl > check_https.log 2>&1 
ret=$? 

if grep 'SSL not compiled in' check_https.log > /dev/null
then
    echo 'do not check SSL, SSL not compiled in'
    ssl_enabled="no"
fi

if test "$ssl_enabled" = "yes"
then
    echo -n ' checking SSL connections, too...'
    test/test_http -r $times -s -c $ssl_clientcert $url_ssl > check_https.log 2>&1 
    ret=$?
else
    test/test_http -r 1 -s -c $ssl_clientcert $quiturl_ssl >> check_https.log 2>&1
    rm -f check_https.log
    sleep 1
fi
 
test/test_http -r 1 $quiturl >> check_http.log 2>&1
if test "$ssl_enabled" = "yes"
then
     test/test_http -r 1 -s -c $ssl_clientcert $quiturl_ssl >> check_https.log 2>&1
     sleep 1
fi
 
sleep 1
if grep 'ERROR:|PANIC:' check_http.log check_http_server.log  > /dev/null  
then 
	echo check_http failed 1>&2 
	echo See check_http.log and check_http_server.log for info 1>&2 
	exit 1 
fi 

if test "$ssl_enabled" = "yes"
then
    if grep 'ERROR:|PANIC' check_https.log check_https_server.log > /dev/null
    then
        echo check_https failed 1>&2
        echo see check_https_log and check_http_server.log for info 1>&2
        exit 1
    fi
fi

rm -f check_http*.log
rm -f $ssl_clientcert
exit 0



