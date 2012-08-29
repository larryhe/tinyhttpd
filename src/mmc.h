/* mmc.h - header file for mmap cache package
**
** Copyright © 1998 by Jef Poskanzer <jef@mail.acme.com>.
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

#ifndef _MMC_H_
#define _MMC_H_

/* Returns an mmap()ed area for the given file, or (void*) 0 on errors.
** If you have a stat buffer on the file, pass it in, otherwise pass 0.
** Same for the current time.
*/
extern void* mmc_map( char* filename, struct stat* sbP, struct timeval* nowP );

/* Done with an mmap()ed area that was returned by mmc_map().
** If you have a stat buffer on the file, pass it in, otherwise pass 0.
** Same for the current time.
*/
extern void mmc_unmap( void* addr, struct stat* sbP, struct timeval* nowP );

/* Clean up the mmc package, freeing any unused storage.
** This should be called periodically, say every five minutes.
** If you have the current time, pass it in, otherwise pass 0.
*/
extern void mmc_cleanup( struct timeval* nowP );

/* Free all storage, usually in preparation for exitting. */
extern void mmc_destroy( void );

/* Generate debugging statistics syslog message. */
extern void mmc_logstats( long secs );

#endif /* _MMC_H_ */
