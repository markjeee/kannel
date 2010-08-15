#!/usr/bin/python

MAIL_SENDER = "liw"
MAIL_RECEIVER = "liw"

import cgi, os, string

class Vars:
    def __init__(self):
    	self._dict = cgi.FieldStorage()

    def __getitem__(self, key):
    	return self._dict[key].value

def smstomail():
    print "Content-Type: text/plain"
    print ""
    
    v = Vars()
    
    f = os.popen("/usr/sbin/sendmail -oi %s" % MAIL_RECEIVER, "w")
    f.write("From: %s\nTo: %s\nSubject: SMS message from %s\n\n%s:\n%s\n" %
    	    (MAIL_SENDER, MAIL_RECEIVER, v["from"], v["to"], v["text"]))
    f.close()
    
    print "Sent via mail to receiver."

if __name__ == "__main__":
    smstomail()
