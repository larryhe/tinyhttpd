/* mmc.c - mmap cache
**
** Copyright © 1998,2001 by Jef Poskanzer <jef@mail.acme.com>.
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

#include "config.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <fcntl.h>
#include <syslog.h>
#include <errno.h>

#ifdef HAVE_MMAP
#include <sys/mman.h>
#endif /* HAVE_MMAP */

#include "mmc.h"
#include "libhttpd.h"

#ifndef HAVE_INT64T
typedef long long int64_t;
#endif


/* Defines. */
#ifndef DEFAULT_EXPIRE_AGE
#define DEFAULT_EXPIRE_AGE 600
#endif
#ifndef DESIRED_FREE_COUNT
#define DESIRED_FREE_COUNT 100
#endif
#ifndef DESIRED_MAX_MAPPED_FILES
#define DESIRED_MAX_MAPPED_FILES 2000
#endif
#ifndef DESIRED_MAX_MAPPED_BYTES
#define DESIRED_MAX_MAPPED_BYTES 1000000000
#endif
#ifndef INITIAL_HASH_SIZE
#define INITIAL_HASH_SIZE (1 << 10)
#endif

#ifndef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))
#endif
#ifndef MIN
#define MIN(a,b) ((a)<(b)?(a):(b))
#endif


/* The Map struct. */
typedef struct MapStruct {
    ino_t ino;
    dev_t dev;
    off_t size;
    time_t ctime;
    int refcount;
    time_t reftime;
    void* addr;
    unsigned int hash;
    int hash_idx;
    struct MapStruct* next;
    } Map;


/* Globals. */
static Map* maps = (Map*) 0;
static Map* free_maps = (Map*) 0;
static int alloc_count = 0, map_count = 0, free_count = 0;
static Map** hash_table = (Map**) 0;
static int hash_size;
static unsigned int hash_mask;
static time_t expire_age = DEFAULT_EXPIRE_AGE;
static off_t mapped_bytes = 0;



/* Forwards. */
static void panic( void );
static void really_unmap( Map** mm );
static int check_hash_size( void );
static int add_hash( Map* m );
static Map* find_hash( ino_t ino, dev_t dev, off_t size, time_t ctime );
static unsigned int hash( ino_t ino, dev_t dev, off_t size, time_t ctime );


void*
mmc_map( char* filename, struct stat* sbP, struct timeval* nowP )
    {
    time_t now;
    struct stat sb;
    Map* m;
    int fd;

    /* Stat the file, if necessary. */
    if ( sbP != (struct stat*) 0 )
	sb = *sbP;
    else
	{
	if ( stat( filename, &sb ) != 0 )
	    {
	    syslog( LOG_ERR, "stat - %m" );
	    return (void*) 0;
	    }
	}

    /* Get the current time, if necessary. */
    if ( nowP != (struct timeval*) 0 )
	now = nowP->tv_sec;
    else
	now = time( (time_t*) 0 );

    /* See if we have it mapped already, via the hash table. */
    if ( check_hash_size() < 0 )
	{
	syslog( LOG_ERR, "check_hash_size() failure" );
	return (void*) 0;
	}
    m = find_hash( sb.st_ino, sb.st_dev, sb.st_size, sb.st_ctime );
    if ( m != (Map*) 0 )
	{
	/* Yep.  Just return the existing map */
	++m->refcount;
	m->reftime = now;
	return m->addr;
	}

    /* Open the file. */
    fd = open( filename, O_RDONLY );
    if ( fd < 0 )
	{
	syslog( LOG_ERR, "open - %m" );
	return (void*) 0;
	}

    /* Find a free Map entry or make a new one. */
    if ( free_maps != (Map*) 0 )
	{
	m = free_maps;
	free_maps = m->next;
	--free_count;
	}
    else
	{
	m = (Map*) malloc( sizeof(Map) );
	if ( m == (Map*) 0 )
	    {
	    (void) close( fd );
	    syslog( LOG_ERR, "out of memory allocating a Map" );
	    return (void*) 0;
	    }
	++alloc_count;
	}

    /* Fill in the Map entry. */
    m->ino = sb.st_ino;
    m->dev = sb.st_dev;
    m->size = sb.st_size;
    m->ctime = sb.st_ctime;
    m->refcount = 1;
    m->reftime = now;

    /* Avoid doing anything for zero-length files; some systems don't like
    ** to mmap them, other systems dislike mallocing zero bytes.
    */
    if ( m->size == 0 )
	m->addr = (void*) 1;	/* arbitrary non-NULL address */
    else
	{
	size_t size_size = (size_t) m->size;	/* loses on files >2GB */
#ifdef HAVE_MMAP
	/* Map the file into memory. */
	m->addr = mmap( 0, size_size, PROT_READ, MAP_PRIVATE, fd, 0 );
	if ( m->addr == (void*) -1 && errno == ENOMEM )
	    {
	    /* Ooo, out of address space.  Free all unreferenced maps
	    ** and try again.
	    */
	    panic();
	    m->addr = mmap( 0, size_size, PROT_READ, MAP_PRIVATE, fd, 0 );
	    }
	if ( m->addr == (void*) -1 )
	    {
	    syslog( LOG_ERR, "mmap - %m" );
	    (void) close( fd );
	    free( (void*) m );
	    --alloc_count;
	    return (void*) 0;
	    }
#else /* HAVE_MMAP */
	/* Read the file into memory. */
	m->addr = (void*) malloc( size_size );
	if ( m->addr == (void*) 0 )
	    {
	    /* Ooo, out of memory.  Free all unreferenced maps
	    ** and try again.
	    */
	    panic();
	    m->addr = (void*) malloc( size_size );
	    }
	if ( m->addr == (void*) 0 )
	    {
	    syslog( LOG_ERR, "out of memory storing a file" );
	    (void) close( fd );
	    free( (void*) m );
	    --alloc_count;
	    return (void*) 0;
	    }
	if ( httpd_read_fully( fd, m->addr, size_size ) != size_size )
	    {
	    syslog( LOG_ERR, "read - %m" );
	    (void) close( fd );
	    free( (void*) m );
	    --alloc_count;
	    return (void*) 0;
	    }
#endif /* HAVE_MMAP */
	}
    (void) close( fd );

    /* Put the Map into the hash table. */
    if ( add_hash( m ) < 0 )
	{
	syslog( LOG_ERR, "add_hash() failure" );
	free( (void*) m );
	--alloc_count;
	return (void*) 0;
	}

    /* Put the Map on the active list. */
    m->next = maps;
    maps = m;
    ++map_count;

    /* Update the total byte count. */
    mapped_bytes += m->size;

    /* And return the address. */
    return m->addr;
    }


void
mmc_unmap( void* addr, struct stat* sbP, struct timeval* nowP )
    {
    Map* m = (Map*) 0;

    /* Find the Map entry for this address.  First try a hash. */
    if ( sbP != (struct stat*) 0 )
	{
	m = find_hash( sbP->st_ino, sbP->st_dev, sbP->st_size, sbP->st_ctime );
	if ( m != (Map*) 0 && m->addr != addr )
	    m = (Map*) 0;
	}
    /* If that didn't work, try a full search. */
    if ( m == (Map*) 0 )
	for ( m = maps; m != (Map*) 0; m = m->next )
	    if ( m->addr == addr )
		break;
    if ( m == (Map*) 0 )
	syslog( LOG_ERR, "mmc_unmap failed to find entry!" );
    else if ( m->refcount <= 0 )
	syslog( LOG_ERR, "mmc_unmap found zero or negative refcount!" );
    else
	{
	--m->refcount;
	if ( nowP != (struct timeval*) 0 )
	    m->reftime = nowP->tv_sec;
	else
	    m->reftime = time( (time_t*) 0 );
	}
    }


void
mmc_cleanup( struct timeval* nowP )
    {
    time_t now;
    Map** mm;
    Map* m;

    /* Get the current time, if necessary. */
    if ( nowP != (struct timeval*) 0 )
	now = nowP->tv_sec;
    else
	now = time( (time_t*) 0 );

    /* Really unmap any unreferenced entries older than the age limit. */
    for ( mm = &maps; *mm != (Map*) 0; )
	{
	m = *mm;
	if ( m->refcount == 0 && now - m->reftime >= expire_age )
	    really_unmap( mm );
	else
	    mm = &(*mm)->next;
	}

    /* Adjust the age limit if there are too many bytes mapped, or
    ** too many or too few files mapped.
    */
    if ( mapped_bytes > DESIRED_MAX_MAPPED_BYTES )
	expire_age = MAX( ( expire_age * 2 ) / 3, DEFAULT_EXPIRE_AGE / 10 );
    else if ( map_count > DESIRED_MAX_MAPPED_FILES )
	expire_age = MAX( ( expire_age * 2 ) / 3, DEFAULT_EXPIRE_AGE / 10 );
    else if ( map_count < DESIRED_MAX_MAPPED_FILES / 2 )
	expire_age = MIN( ( expire_age * 5 ) / 4, DEFAULT_EXPIRE_AGE * 3 );

    /* Really free excess blocks on the free list. */
    while ( free_count > DESIRED_FREE_COUNT )
	{
	m = free_maps;
	free_maps = m->next;
	--free_count;
	free( (void*) m );
	--alloc_count;
	}
    }


static void
panic( void )
    {
    Map** mm;
    Map* m;

    syslog( LOG_ERR, "mmc panic - freeing all unreferenced maps" );

    /* Really unmap all unreferenced entries. */
    for ( mm = &maps; *mm != (Map*) 0; )
	{
	m = *mm;
	if ( m->refcount == 0 )
	    really_unmap( mm );
	else
	    mm = &(*mm)->next;
	}
    }


static void
really_unmap( Map** mm )
    {
    Map* m;

    m = *mm;
    if ( m->size != 0 )
	{
#ifdef HAVE_MMAP
	if ( munmap( m->addr, m->size ) < 0 )
	    syslog( LOG_ERR, "munmap - %m" );
#else /* HAVE_MMAP */
	free( (void*) m->addr );
#endif /* HAVE_MMAP */
	}
    /* Update the total byte count. */
    mapped_bytes -= m->size;
    /* And move the Map to the free list. */
    *mm = m->next;
    --map_count;
    m->next = free_maps;
    free_maps = m;
    ++free_count;
    /* This will sometimes break hash chains, but that's harmless; the
    ** unmapping code that searches the hash table knows to keep searching.
    */
    hash_table[m->hash_idx] = (Map*) 0;
    }


void
mmc_destroy( void )
    {
    Map* m;

    while ( maps != (Map*) 0 )
	really_unmap( &maps );
    while ( free_maps != (Map*) 0 )
	{
	m = free_maps;
	free_maps = m->next;
	--free_count;
	free( (void*) m );
	--alloc_count;
	}
    }


/* Make sure the hash table is big enough. */
static int
check_hash_size( void )
    {
    int i;
    Map* m;

    /* Are we just starting out? */
    if ( hash_table == (Map**) 0 )
	{
	hash_size = INITIAL_HASH_SIZE;
	hash_mask = hash_size - 1;
	}
    /* Is it at least three times bigger than the number of entries? */
    else if ( hash_size >= map_count * 3 )
	return 0;
    else
	{
	/* No, got to expand. */
	free( (void*) hash_table );
	/* Double the hash size until it's big enough. */
	do
	    {
	    hash_size = hash_size << 1;
	    }
	while ( hash_size < map_count * 6 );
	hash_mask = hash_size - 1;
	}
    /* Make the new table. */
    hash_table = (Map**) malloc( hash_size * sizeof(Map*) );
    if ( hash_table == (Map**) 0 )
	return -1;
    /* Clear it. */
    for ( i = 0; i < hash_size; ++i )
	hash_table[i] = (Map*) 0;
    /* And rehash all entries. */
    for ( m = maps; m != (Map*) 0; m = m->next )
	if ( add_hash( m ) < 0 )
	    return -1;
    return 0;
    }


static int
add_hash( Map* m )
    {
    unsigned int h, he, i;

    h = hash( m->ino, m->dev, m->size, m->ctime );
    he = ( h + hash_size - 1 ) & hash_mask;
    for ( i = h; ; i = ( i + 1 ) & hash_mask )
	{
	if ( hash_table[i] == (Map*) 0 )
	    {
	    hash_table[i] = m;
	    m->hash = h;
	    m->hash_idx = i;
	    return 0;
	    }
	if ( i == he )
	    break;
	}
    return -1;
    }


static Map*
find_hash( ino_t ino, dev_t dev, off_t size, time_t ctime )
    {
    unsigned int h, he, i;
    Map* m;

    h = hash( ino, dev, size, ctime );
    he = ( h + hash_size - 1 ) & hash_mask;
    for ( i = h; ; i = ( i + 1 ) & hash_mask )
	{
	m = hash_table[i];
	if ( m == (Map*) 0 )
	    break;
	if ( m->hash == h && m->ino == ino && m->dev == dev &&
	     m->size == size && m->ctime == ctime )
	    return m;
	if ( i == he )
	    break;
	}
    return (Map*) 0;
    }


static unsigned int
hash( ino_t ino, dev_t dev, off_t size, time_t ctime )
    {
    unsigned int h = 177573;

    h ^= ino;
    h += h << 5;
    h ^= dev;
    h += h << 5;
    h ^= size;
    h += h << 5;
    h ^= ctime;

    return h & hash_mask;
    }


/* Generate debugging statistics syslog message. */
void
mmc_logstats( long secs )
    {
    syslog(
	LOG_INFO, "  map cache - %d allocated, %d active (%lld bytes), %d free; hash size: %d; expire age: %ld",
	alloc_count, map_count, (int64_t) mapped_bytes, free_count, hash_size,
	expire_age );
    if ( map_count + free_count != alloc_count )
	syslog( LOG_ERR, "map counts don't add up!" );
    }
