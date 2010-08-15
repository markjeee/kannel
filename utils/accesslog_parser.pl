#!/usr/bin/perl
#
# accesslog_parser.pl
#
# Kalle Marjola for project Kannel, based on stat.pl by
# 2000-05-12 jarkko@iki.fi

use strict;

my (%stat_in, %stat_out);
my (%userstat_in, %userstat_out);
my (%dailystat_in, %dailystat_out);
my (%keywordstat_in);

my $failed_send = 0;
my $failed_rout = 0;
my $rejected = 0;
my $start = 0;
my $ends = 0;

my $line;
while ($line = <STDIN>) {
    chomp($line);
    # does string begin with date
    if ($line =~ /^(\d\d\d\d)-(\d\d)-(\d\d)\s+(\d\d)\:\d\d\:\d\d\s+(.*)$/) {
	my ($year, $month, $day, $hour, $msg) =  ($1, $2, $3, $4, $5);

	my ($sender, $receiver, $keyword) = 
	    ($msg =~ /\[SMSC\:[^\]]*\] \[[^:]*\:([^\]]*)\] \[[^:]*:([^\]]*)\] \[[^:]*:(\w*)/i);
	if ($msg =~ /receive sms/i) {
	    $stat_in{$sender}{"$year-$month-$day $hour"}++;
	    $userstat_in{$sender}++;
	    $dailystat_in{"$year-$month-$day $hour"}++;
	    $keywordstat_in{lc($keyword)}{"$year-$month-$day $hour"}++;

	} elsif ($msg =~ /sent sms/i) {
	    $stat_out{$receiver}{"$year-$month-$day $hour"}++;
	    $userstat_out{$receiver}++;
	    $dailystat_out{"$year-$month-$day $hour"}++;
	} elsif ($msg =~ /failed send sms/i) {
	    $failed_send++;
	} elsif ($msg =~ /failed routing sms/i) {
	    $failed_rout++;
	} elsif ($msg =~ /rejected/i) {
	    $rejected++;
	} elsif ($msg =~ /log begins/i) {
	    $start++;
	} elsif ($msg =~ /log ends/i) {
	    $ends++;
	}
    }
}

my $key;
my $key2;


# daily/hourly user (phone-number) specific statistics 

print "By phone number\n===============\n";

print "Mobile Originated (from user):\n";
foreach $key (sort keys %stat_in) {
    print "$key:\n";
    foreach $key2 (sort keys %{ $stat_in{$key} } ) {
        print "\t$key2 = $stat_in{$key}{$key2}\n";
    }
    print "\t\t\t\tTotal: $userstat_in{$key}\n\n"; 
}

print "Mobile Terminated (to user):\n";
foreach $key (sort keys %stat_out) {
    print "$key:\n";
    foreach $key2 (sort keys %{ $stat_out{$key} } ) {
	print "\t$key2 = $stat_out{$key}{$key2}\n";
    }
    print "\t\t\t\tTotal: $userstat_out{$key}\n\n"; 
}

# statistics by keyword

my $total;

print "By keyword\n==========\n";

print "Mobile Originated (from user):\n";
foreach $key (sort keys %keywordstat_in) {
    print "$key:\n";
    $total = 0;
    foreach $key2 (sort keys %{ $keywordstat_in{$key} } ) {
        print "\t$key2 = $keywordstat_in{$key}{$key2}\n";
	$total += $keywordstat_in{$key}{$key2};
    }
    print "\t\t\t\tTotal: $total\n\n"; 
}




# statistics by hour basic

my ($total_in, $total_out);

print "Total usage\n===========\n";

print "Mobile Originated (from user):\n";
foreach $key (sort keys %dailystat_in) {
    $total_in += $dailystat_in{$key};
    print "$key = $dailystat_in{$key}\n";
}
print "\t\t\t\tTotal: $total_in\n\n"; 


print "Mobile Terminated (to user):\n";
foreach $key (sort keys %dailystat_out) {
    $total_out += $dailystat_out{$key};
    print "$key = $dailystat_out{$key}\n";
}
print "\t\t\t\tTotal: $total_out\n\n"; 

print "$failed_send failed sendings, $failed_rout failed routing, $rejected rejected messages\n";
my $ugly = $start - $ends;
print "$start Kannel start-ups, $ugly crashes/ugly shutdowns\n";
