#!/usr/bin/python

FORTUNE_MAX_TRIES = 10
SMS_MAX = 160

import os, string, sys

def fortune():
    for i in range(FORTUNE_MAX_TRIES):
	f = os.popen("/usr/games/fortune", "r")
	data = f.read()
	f.close()
	data = string.join(string.split(data))
	if len(data) <= SMS_MAX:
	    break
    sys.stdout.write("Content-Type: text/plain\r\n\r\n")
    sys.stdout.write(data[:SMS_MAX])

if __name__ == "__main__":
    fortune()
