#
# Copyright (c) 2004 MNC S.A.
#
# This program is open-source and released under
# the Kannel Software License, Version 1.0. Please see
# LICENSE from the main Kannel distribution sources.
#

import sys
import re
from socket import *

port = 6666

# you may optionally specify on commandline the port to use
if len( sys.argv ) == 2:
    port = int( sys.argv[1] )

# create the socket which will represent the server endpoint
sock = socket( AF_INET, SOCK_STREAM, 0 )

# allow socket to reuse a port address not fully closed; necessary
# when relaunching the server program quickly several times; see
# http://hea-www.harvard.edu/~fine/Tech/addrinuse.html
sock.setsockopt( SOL_SOCKET, SO_REUSEADDR, 1 )
    
# assign the local address to the socket (127.0.0.1 specifies to only
# accept connections from the local machine, not from the network
sock.bind( ( '127.0.0.1', port ) )

# tell that we're willing to accept new connections */
sock.listen( 1 )

print 'Listening for connections on port %d...' % port

while 1:
    # accept the incoming connection, obtaining the file-descriptor
    # representing the connection
    fd, addr = sock.accept()
    
    # read data sent by the client
    buf = ''
    while 1:
        data = fd.recv( 10000 )
        # print received data on console while it's received
        sys.stdout.write( data )
        buf += data
        
        match = re.search( r'\r\n\r\n', buf )
        if match:
            break
        
    print '-=-=--'

    response  = "HTTP/1.1 200 OK\r\n"
    response += "Connection: close\r\n"
    response += "\r\n"
    response += "<html><body>Ok.</body></html>"

    fd.send( response )

    # we're done
    fd.close()
