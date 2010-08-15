#!/usr/bin/perl -w

# This script joins multi-line entries from access.log
# cat bearerbox_access.log | multi-line.pl

$|=1;

$linenum=0;
$result="";

while($line = <STDIN>) {
	$linenum++;
	chop($line);
        next if $line =~ /Log begins/;
	next if $line =~ /Log ends/;

	if ( $result ne "" && $line =~ /^[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2} .+? .+? \[SMSC:.*?\] \[SVC:.*?\] \[from:.*?\] \[to:.*?\] \[flags:.:.:.:.:.+?\] \[msg:.+?:.*$/) {
		$result = "";
		print STDERR "$linenum:$line\n";
	}

	$result .= $line;

	if($result =~ /^[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2} .+? .+? \[SMSC:.*?\] \[SVC:.*?\] \[from:.*?\] \[to:.*?\] \[flags:.:.:.:.:.+?\] \[msg:.+?:.*?\] \[udh:.+?:.*?\]$/i) {
		print $result."\n";
		$result="";
	}
}

