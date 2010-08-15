#!/usr/bin/perl -wn
#
# utf8map.pl - remap ascii to utf8
#
# Program was created to build conversion table from ascii into utf8.
# Ascii table's first half does not require any changes (because utf8
# [0-127] encoding is the same as in ascii). To control second half's
# encoding we have to specify all unicode codes for characters in
# [128-255] interval. Program takes unicode codes for 128 characters
# on input (in hex format with leading 0x) and generates conversion table .
# Every utf8 code is padded by '0' and occupies 4 bytes.
# It is suitable for use in 'C' programs.
#
# For example,
#  in windows-1257 table character 169 '(c)' has code 0x00A9 in unicode.
#  Program will generate folowing string:
#
#  0xC2, 0xA9, 0x00, 0x00, /*   169           0x00a9 */
# \______________________/     \___/         \______/
#    utf8 code (2 bytes        ascii         unicode
#    with padding)
#
# USAGE:
#   perl utf8map.pl asci_128-255_unicode_table.txt
#
# Andrejs Dubovskis
#

use strict ;

use vars qw/$N/ ;

BEGIN {
  # we going to prepare table for characters in 128-255 interval
  $N = 128 ;
}

# look for hex number (unicode)
for my $hex (/0x[\da-f]+/ig) {
  my $num = hex($hex) ;
  my @out = () ;

  if ($num > 0xffff) {
    die "too large number: $hex" ;
  } elsif ($num > 0x07ff) {
    # result is three bytes long
    @out = (
	    (($num >> 12) & 0xf) | 0xe0,
	    (($num >> 6) & 0x3f) | 0x80,
	    ($num & 0x3f) | 0x80
	   ) ;
  } elsif ($num > 0x7f) {
    # result is two bytes long
    @out = (
	    (($num >> 6) & 0x1f) | 0xc0,
	    ($num & 0x3f) | 0x80
	   ) ;
  } else {
    # only zero is legal here
    die "wrong input data: $hex" if $num ;
  }

  # pad by '0'
  push(@out, 0) while @out < 4 ;

  # output utf8 code
  printf("0x%02X,\t0x%02X,\t0x%02X,\t0x%02X,\t", @out) ;
  # output comments
  print "/*\t$N\t$hex\t*/\n" ;

  # characters in [128-255] interval only
  exit if ++$N > 255 ;
}
