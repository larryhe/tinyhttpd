/* fdwatch.h - header file for fdwatch package
**
** This package abstracts the use of the select()/poll()/kqueue()
** system calls.  The basic function of these calls is to watch a set
** of file descriptors for activity.  select() originated in the BSD world,
** while poll() came from SysV land, and their interfaces are somewhat
** different.  fdwatch lets you write your code to a single interface,
** with the portability differences hidden inside the package.
**
** Usage is fairly simple.  Call fdwatch_get_nfiles() to initialize
** the package and find out how many fine descriptors are available.
** Then each time through your main loop, call fdwatch_clear(), then
** fdwatch_add_fd() for each of the descriptors you want to watch,
** then call fdwatch() to actually perform the watch.  After it returns
** you can check which descriptors are ready via fdwatch_check_fd().
**
** If your descriptor set hasn't changed from the last time through
** the loop, you can skip calling fdwatch_clear() and fdwatch_add_fd()
** to save a little CPU time.
**
**
** Copyright © 1999 by Jef Poskanzer <jef@mail.acme.com>.
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

#ifndef _FDWATCH_H_
#define _FDWATCH_H_

#define FDW_READ 0
#define FDW_WRITE 1

#ifndef INFTIM
#define INFTIM -1
#endif /* INFTIM */

/* Figure out how many file descriptors the system allows, and
** initialize the fdwatch data structures.  Returns -1 on failure.
*/
extern int fdwatch_get_nfiles( void );

/* Add a descriptor to the watch list.  rw is either FDW_READ or FDW_WRITE.  */
extern void fdwatch_add_fd( int fd, void* client_data, int rw );

/* Delete a descriptor from the watch list. */
extern void fdwatch_del_fd( int fd );

/* Do the watch.  Return value is the number of descriptors that are ready,
** or 0 if the timeout expired, or -1 on errors.  A timeout of INFTIM means
** wait indefinitely.
*/
extern int fdwatch( long timeout_msecs );

/* Check if a descriptor was ready. */
extern int fdwatch_check_fd( int fd );

/* Get the client data for the next returned event.  Returns -1 when there
** are no more events.
*/
extern void* fdwatch_get_next_client_data( void );

/* Generate debugging statistics syslog message. */
extern void fdwatch_logstats( long secs );

#endif /* _FDWATCH_H_ */
