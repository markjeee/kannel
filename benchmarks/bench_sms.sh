#!/bin/sh
#
# Use `test/test_smsc' to test SMS speed.

set -e

case "$1" in
--fast) times=1000; shift ;;
*) times=100000 ;;
esac

. benchmarks/functions.inc

function gather_data {
    rm -f bench_sms_*.log
    
    test/test_smsc -m "$1" -r $times 2> bench_sms_smsc.log &
    sleep 1
    gw/bearerbox -v 4 benchmarks/bench_sms.conf &
    sleep 1
    gw/smsbox -v 4 benchmarks/bench_sms.conf &
    
    wait
    
    check_for_errors bench_sms_*.log
}

function analyze_logs {
    for type in submit # deliver deliver_ack http_request 
    do
	awk "/INFO: Event .*, type $type,/ { print \$NF, \$(NF-2) }" \
	    bench_sms_smsc.log |
	uniq -c | 
	awk '
	    NR == 1 { first = $2 }
	    { print $2 - first, $1 }
	' > bench_sms-$type.dat
    done

    awk '/DEBUG: RTT / { print ++n, $NF }' bench_sms_smsc.log \
    	> bench_sms-rtt.dat
}
    
function make_graphs {
    plot benchmarks/bench_sms_"$1" \
	"time (s)" "requests/s (Hz)" \
	"bench_sms-submit.dat" "submit"

#	"bench_sms-deliver.dat" "deliver" \
#	"bench_sms-deliver_ack.dat" "deliver_ack" \
#	"bench_sms-http_request.dat" "http_request" \
    
    plot benchmarks/bench_sms_rtt_"$1" \
	"received message number" "average round trip time (s)" \
	"bench_sms-rtt.dat" ""
}

function run {
    gather_data "$1"
    analyze_logs
    make_graphs "$1"
}

run n_messages
# run sustained_level
sed "s/#TIMES#/$times/g" benchmarks/bench_sms.txt

rm -f bench_sms*.log
rm -f bench_sms*.dat
