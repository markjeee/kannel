#!/usr/bin/perl -w

# This script reads access.log (pass it through multi-line first!) and
# split them by SMSC or Service

# Just define your service and smsc names as:
#
# SMSC: (smsc-id in smsc groups)
# <Client>-<Number>
#
# Service: (name in sms-service and sendsms-user)
# <Client>-<SVC>-<service_name>
# SVC = for user, MT or USER
#       for service, MO or SERVICE



$dir = shift || "/tmp";

foreach $line (<>) {
	
	$line =~ /^.{19} (.+) \[SMSC:(.*?)\] \[SVC:(.*?)\].*$/; 

	$status= $1; $smsc= $2; $service= $3;

	if( $status =~ /Receive/) {
		open(X, ">>$dir/$smsc.log");
	} else {
		open(X, ">>$dir/$service.log");
	}

	print X $line;

	close(X);
}
