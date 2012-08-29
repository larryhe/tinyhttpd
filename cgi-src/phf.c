/* phf - cracker trap
**
** Old distributions of the NCSA and Apache web servers included a
** version of the phf program that had a bug.  The program could
** easily be made to run arbitrary shell commands.  There is no real
** legitimate use for phf, so any attempts to run it must be considered
** to be attacks.  Accordingly, this version of phf logs the attack
** and then returns a page indicating that phf doesn't exist.
**
**
** Copyright © 1996 by Jef Poskanzer <jef@mail.acme.com>.
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
** ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
** OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
** HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
** LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
** OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
** SUCH DAMAGE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "config.h"

static char* argv0;

int
main( int argc, char* argv[] )
    {
    char* cp;

    argv0 = argv[0];
    cp = strrchr( argv0, '/' );
    if ( cp != (char*) 0 )
	++cp;
    else
	cp = argv0;
    openlog( cp, LOG_NDELAY|LOG_PID, LOG_FACILITY );
    syslog( LOG_CRIT, "phf CGI probe from %s", getenv( "REMOTE_ADDR" ) );
    (void) printf( "\
Content-type: text/html\n\
Status: 404/html\n\
\n\
<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\n\
<BODY><H2>404 Not Found</H2>\n\
The requested object does not exist on this server.\n\
The link you followed is either outdated, inaccurate,\n\
or the server has been instructed not to let you have it.\n\
</BODY></HTML>\n" );
    exit( 0 );
    }
