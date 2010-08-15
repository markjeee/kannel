#!/usr/bin/python

"""PING cgi.

Gets the name or IP number of a host as CGI argument. Returns as
plain text the output of the ping command for that host.

Lars Wirzenius <liw@wapit.com>
"""

import os, cgi, string

def ping(host):
    if len(string.split(host, "'")) != 1:
    	return "Invalid host name."
    f = os.popen("ping -q -c 4 '%s'" % host)
    lines = f.readlines()
    f.close()
    lines = map(lambda line: line[:-1], lines)
    lines = filter(lambda line: line and line[:4] != "--- ",  lines)
    return string.join(string.split(string.join(lines, " ")), " ")

def do_cgi():
    print "Content-type: text/plain"
    print ""

    form = cgi.FieldStorage()
    if not form.has_key("host"):
	print "CGI argument `host' missing."
    else:
	host = form["host"].value
	print ping(host)

if __name__ == "__main__":
    do_cgi()
