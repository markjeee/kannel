#!/usr/bin/perl

$exchange = shift;

$exchange = "" if $exchange =~ /-/;

$|=1;
@months = ("Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec");
%results = ( "Sent" => "200", "Receive" => "200", "FAILED Send" => 403, "DISCARDED" => "404");

while ($line = <STDIN>) {
	chop($line);

	# Lines to ignore
	# ---------------
	next if $line =~ /Log begins/;
	next if $line =~ /Log ends/;

	$line =~ /^([0-9]{4})-([0-9]{2})-([0-9]{2}) ([0-9]{2}):([0-9]{2}):([0-9]{2}) (.+?) SMS \[SMSC:(.*?)\] \[SVC:(.*?)\] \[from:(.*?)\] \[to:(.*?)\] \[flags:(.):(.):(.):(.):(.)\] \[msg:([0-9]+):(.*?)\] \[udh:([0-9]+):(.*?)\]$/i;
	($year, $month, $day, $hour, $minute, $second, $result, $smsc, $svc, $from, $to, $f1, $f2, $f3, $f4, $f5, $msglen, $msg, $udhlen, $udh) = ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16, $17, $18, $19, $20);

	$msg =~ s/[^a-zA-Z0-9]//g;
	$msg = substr($msg, 0, 60); 
	$msg =~ tr/a-z/A-Z/;

	if($exchange ne "") {
		$to = $from;
	}

	$to .= ".pt" if $to =~ /^91/;
	$to .= ".fr" if $to =~ /^93/;
	$to .= ".es" if $to =~ /^96/;
	$to .= ".uk" if $to =~ /^95/;

	$string = $to; 
	$string .= " - - [". $day;
	$string .= "/". $months[$month-1];
	$string .= "/". $year;
	$string .= ":". $hour;
	$string .= ":". $minute;
	$string .= ":". $second;
	$string .= " +0100] ". '"GET /'. $msg;
	$string .= ' HTTP/1.0" '. $results{$result};
	$string .= " ". ($msglen+$udhlen);
	$string .= ' "'. $f1. ",". $f2. ",". $f3. ",". $f4. ",". $f5 . '" -'."\n";

	print $string;
}
