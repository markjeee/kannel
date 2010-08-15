#!/usr/bin/perl

use strict;

#
# a short perl script to convert (almost) any pictures into WBMP pictures
#
# Kalle Marjola for WapIT Ltd. 1999
#
# USAGE: ./pixmapgen.pl SOURCE >TARGET
#
#
# note: change following strings if needed to
#

my $temp_target = "/tmp/pmgen_tmp.mono";   # the program for mono->WBMP
my $converter = "./test_wbmp";             # temporary file

my $source = $ARGV[0];

my $retval = `convert -verbose -monochrome $source $temp_target`;
my ($width, $height) = ($retval =~ /$temp_target (\d+)x(\d+)/s);

print `$converter $temp_target $width $height`;

