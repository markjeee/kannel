#!/bin/sh
#
# Compare lynx -source output and test_http output on a number of URLs.
#
# Lars Wirzenius

for url in `cat test/http-test-urls`
do
	echo "Testing $url..."
	lynx -source "$url" > lynx.tmp
	test/test_http -s "$url" > http.tmp
	test/test_http -s "$url" > http2.tmp
	if diff -u lynx.tmp http.tmp >/dev/null && 
	   diff -u lynx.tmp http2.tmp > /dev/null
	then
		:
	else
		echo "Lynx and test_http disagree. Oops."
		echo "URL is <$url>."
		echo "See lynx.tmp and http2.tmp."
		exit 1
	fi
done
echo "All tests passed. Very good."
rm -f lynx.tmp http.tmp http2.tmp
