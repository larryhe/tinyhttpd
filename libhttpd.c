/* libhttpd.c - HTTP protocol library
**
** Copyright � 1995,1998,1999,2000,2001,2015 by
** Jef Poskanzer <jef@mail.acme.com>. All rights reserved.
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
#include "version.h"

#ifdef SHOW_SERVER_VERSION
#define EXPOSED_SERVER_SOFTWARE SERVER_SOFTWARE
#else /* SHOW_SERVER_VERSION */
#define EXPOSED_SERVER_SOFTWARE "thttpd"
#endif /* SHOW_SERVER_VERSION */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#ifdef HAVE_MEMORY_H
#include <memory.h>
#endif /* HAVE_MEMORY_H */
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <stdarg.h>

#ifdef HAVE_OSRELDATE_H
#include <osreldate.h>
#endif /* HAVE_OSRELDATE_H */

#ifdef HAVE_DIRENT_H
# include <dirent.h>
# define NAMLEN(dirent) strlen((dirent)->d_name)
#else
# define dirent direct
# define NAMLEN(dirent) (dirent)->d_namlen
# ifdef HAVE_SYS_NDIR_H
#  include <sys/ndir.h>
# endif
# ifdef HAVE_SYS_DIR_H
#  include <sys/dir.h>
# endif
# ifdef HAVE_NDIR_H
#  include <ndir.h>
# endif
#endif

#include "libhttpd.h"
#include "mmc.h"
#include "timers.h"
#include "match.h"
#include "tdate_parse.h"

#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

#ifndef SHUT_WR
#define SHUT_WR 1
#endif

#ifndef HAVE_INT64T
typedef long long int64_t;
#endif

#ifndef HAVE_SOCKLENT
typedef int socklen_t;
#endif

#ifdef __CYGWIN__
#define timezone  _timezone
#endif

#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif


/* Forwards. */
static void check_options( void );
static void free_httpd_server( httpd_server* hs );
static int initialize_listen_socket( httpd_sockaddr* saP );
static void add_response( httpd_conn* hc, char* str );
static void send_mime( httpd_conn* hc, int status, char* title, char* encodings, char* extraheads, char* type, off_t length, time_t mod );
static void send_response( httpd_conn* hc, int status, char* title, char* extraheads, char* form, char* arg );
static void send_response_tail( httpd_conn* hc );
static void defang( char* str, char* dfstr, int dfsize );
#ifdef ERR_DIR
static int send_err_file( httpd_conn* hc, int status, char* title, char* extraheads, char* filename );
#endif /* ERR_DIR */
#ifdef AUTH_FILE
static void send_authenticate( httpd_conn* hc, char* realm );
static int b64_decode( const char* str, unsigned char* space, int size );
static int auth_check( httpd_conn* hc, char* dirname  );
static int auth_check2( httpd_conn* hc, char* dirname  );
#endif /* AUTH_FILE */
static void send_dirredirect( httpd_conn* hc );
static int hexit( char c );
static void strdecode( char* to, char* from );
#ifdef GENERATE_INDEXES
static void strencode( char* to, int tosize, char* from );
#endif /* GENERATE_INDEXES */
#ifdef TILDE_MAP_1
static int tilde_map_1( httpd_conn* hc );
#endif /* TILDE_MAP_1 */
#ifdef TILDE_MAP_2
static int tilde_map_2( httpd_conn* hc );
#endif /* TILDE_MAP_2 */
static int vhost_map( httpd_conn* hc );
static char* expand_symlinks( char* path, char** restP, int no_symlink_check, int tildemapped );
static char* bufgets( httpd_conn* hc );
static void de_dotdot( char* file );
static void init_mime( void );
static void figure_mime( httpd_conn* hc );
#ifdef CGI_TIMELIMIT
static void cgi_kill2( ClientData client_data, struct timeval* nowP );
static void cgi_kill( ClientData client_data, struct timeval* nowP );
#endif /* CGI_TIMELIMIT */
#ifdef GENERATE_INDEXES
static int ls( httpd_conn* hc );
#endif /* GENERATE_INDEXES */
static char* build_env( char* fmt, char* arg );
#ifdef SERVER_NAME_LIST
static char* hostname_map( char* hostname );
#endif /* SERVER_NAME_LIST */
static char** make_envp( httpd_conn* hc );
static char** make_argp( httpd_conn* hc );
static void cgi_interpose_input( httpd_conn* hc, int wfd );
static void post_post_garbage_hack( httpd_conn* hc );
static void cgi_interpose_output( httpd_conn* hc, int rfd );
static void cgi_child( httpd_conn* hc );
static int cgi( httpd_conn* hc );
static int really_start_request( httpd_conn* hc, struct timeval* nowP );
static void make_log_entry( httpd_conn* hc, struct timeval* nowP );
static int check_referrer( httpd_conn* hc );
static int really_check_referrer( httpd_conn* hc );
static int sockaddr_check( httpd_sockaddr* saP );
static size_t sockaddr_len( httpd_sockaddr* saP );
static int my_snprintf( char* str, size_t size, const char* format, ... );
#ifndef HAVE_ATOLL
static long long atoll( const char* str );
#endif /* HAVE_ATOLL */


/* This global keeps track of whether we are in the main process or a
** sub-process.  The reason is that httpd_write_response() can get called
** in either context; when it is called from the main process it must use
** non-blocking I/O to avoid stalling the server, but when it is called
** from a sub-process it wants to use blocking I/O so that the whole
** response definitely gets written.  So, it checks this variable.  A bit
** of a hack but it seems to do the right thing.
*/
static int sub_process = 0;


static void
check_options( void )
    {
#if defined(TILDE_MAP_1) && defined(TILDE_MAP_2)
    syslog( LOG_CRIT, "both TILDE_MAP_1 and TILDE_MAP_2 are defined" );
    exit( 1 );
#endif /* both */
    }


static void
free_httpd_server( httpd_server* hs )
    {
    if ( hs->binding_hostname != (char*) 0 )
	free( (void*) hs->binding_hostname );
    if ( hs->cwd != (char*) 0 )
	free( (void*) hs->cwd );
    if ( hs->cgi_pattern != (char*) 0 )
	free( (void*) hs->cgi_pattern );
    if ( hs->charset != (char*) 0 )
	free( (void*) hs->charset );
    if ( hs->p3p != (char*) 0 )
	free( (void*) hs->p3p );
    if ( hs->url_pattern != (char*) 0 )
	free( (void*) hs->url_pattern );
    if ( hs->local_pattern != (char*) 0 )
	free( (void*) hs->local_pattern );
    free( (void*) hs );
    }


httpd_server*
httpd_initialize(
    char* hostname, httpd_sockaddr* sa4P, httpd_sockaddr* sa6P,
    unsigned short port, char* cgi_pattern, int cgi_limit, char* charset,
    char* p3p, int max_age, char* cwd, int no_log, FILE* logfp,
    int no_symlink_check, int vhost, int global_passwd, char* url_pattern,
    char* local_pattern, int no_empty_referrers )
    {
    httpd_server* hs;
    static char ghnbuf[256];
    char* cp;

    check_options();

    hs = NEW( httpd_server, 1 );
    if ( hs == (httpd_server*) 0 )
	{
	syslog( LOG_CRIT, "out of memory allocating an httpd_server" );
	return (httpd_server*) 0;
	}

    if ( hostname != (char*) 0 )
	{
	hs->binding_hostname = strdup( hostname );
	if ( hs->binding_hostname == (char*) 0 )
	    {
	    syslog( LOG_CRIT, "out of memory copying hostname" );
	    return (httpd_server*) 0;
	    }
	hs->server_hostname = hs->binding_hostname;
	}
    else
	{
	hs->binding_hostname = (char*) 0;
	hs->server_hostname = (char*) 0;
	if ( gethostname( ghnbuf, sizeof(ghnbuf) ) < 0 )
	    ghnbuf[0] = '\0';
#ifdef SERVER_NAME_LIST
	if ( ghnbuf[0] != '\0' )
	    hs->server_hostname = hostname_map( ghnbuf );
#endif /* SERVER_NAME_LIST */
	if ( hs->server_hostname == (char*) 0 )
	    {
#ifdef SERVER_NAME
	    hs->server_hostname = SERVER_NAME;
#else /* SERVER_NAME */
	    if ( ghnbuf[0] != '\0' )
		hs->server_hostname = ghnbuf;
#endif /* SERVER_NAME */
	    }
	}

    hs->port = port;
    if ( cgi_pattern == (char*) 0 )
	hs->cgi_pattern = (char*) 0;
    else
	{
	/* Nuke any leading slashes. */
	if ( cgi_pattern[0] == '/' )
	    ++cgi_pattern;
	hs->cgi_pattern = strdup( cgi_pattern );
	if ( hs->cgi_pattern == (char*) 0 )
	    {
	    syslog( LOG_CRIT, "out of memory copying cgi_pattern" );
	    return (httpd_server*) 0;
	    }
	/* Nuke any leading slashes in the cgi pattern. */
	while ( ( cp = strstr( hs->cgi_pattern, "|/" ) ) != (char*) 0 )
	    (void) ol_strcpy( cp + 1, cp + 2 );
	}
    hs->cgi_limit = cgi_limit;
    hs->cgi_count = 0;
    hs->charset = strdup( charset );
    hs->p3p = strdup( p3p );
    hs->max_age = max_age;
    hs->cwd = strdup( cwd );
    if ( hs->cwd == (char*) 0 )
	{
	syslog( LOG_CRIT, "out of memory copying cwd" );
	return (httpd_server*) 0;
	}
    if ( url_pattern == (char*) 0 )
	hs->url_pattern = (char*) 0;
    else
	{
	hs->url_pattern = strdup( url_pattern );
	if ( hs->url_pattern == (char*) 0 )
	    {
	    syslog( LOG_CRIT, "out of memory copying url_pattern" );
	    return (httpd_server*) 0;
	    }
	}
    if ( local_pattern == (char*) 0 )
	hs->local_pattern = (char*) 0;
    else
	{
	hs->local_pattern = strdup( local_pattern );
	if ( hs->local_pattern == (char*) 0 )
	    {
	    syslog( LOG_CRIT, "out of memory copying local_pattern" );
	    return (httpd_server*) 0;
	    }
	}
    hs->no_log = no_log;
    hs->logfp = (FILE*) 0;
    httpd_set_logfp( hs, logfp );
    hs->no_symlink_check = no_symlink_check;
    hs->vhost = vhost;
    hs->global_passwd = global_passwd;
    hs->no_empty_referrers = no_empty_referrers;

    /* Initialize listen sockets.  Try v6 first because of a Linux peculiarity;
    ** like some other systems, it has magical v6 sockets that also listen for
    ** v4, but in Linux if you bind a v4 socket first then the v6 bind fails.
    */
    if ( sa6P == (httpd_sockaddr*) 0 )
	hs->listen6_fd = -1;
    else
	hs->listen6_fd = initialize_listen_socket( sa6P );
    if ( sa4P == (httpd_sockaddr*) 0 )
	hs->listen4_fd = -1;
    else
	hs->listen4_fd = initialize_listen_socket( sa4P );
    /* If we didn't get any valid sockets, fail. */
    if ( hs->listen4_fd == -1 && hs->listen6_fd == -1 )
	{
	free_httpd_server( hs );
	return (httpd_server*) 0;
	}

    init_mime();

    /* Done initializing. */
    if ( hs->binding_hostname == (char*) 0 )
	syslog(
	    LOG_NOTICE, "%.80s starting on port %d", SERVER_SOFTWARE,
	    (int) hs->port );
    else
	syslog(
	    LOG_NOTICE, "%.80s starting on %.80s, port %d", SERVER_SOFTWARE,
	    httpd_ntoa( hs->listen4_fd != -1 ? sa4P : sa6P ),
	    (int) hs->port );
    return hs;
    }


static int
initialize_listen_socket( httpd_sockaddr* saP )
    {
    int listen_fd;
    int on, flags;

    /* Check sockaddr. */
    if ( ! sockaddr_check( saP ) )
	{
	syslog( LOG_CRIT, "unknown sockaddr family on listen socket" );
	return -1;
	}

    /* Create socket. */
    listen_fd = socket( saP->sa.sa_family, SOCK_STREAM, 0 );
    if ( listen_fd < 0 )
	{
	syslog( LOG_CRIT, "socket %.80s - %m", httpd_ntoa( saP ) );
	return -1;
	}
    (void) fcntl( listen_fd, F_SETFD, 1 );

    /* Allow reuse of local addresses. */
    on = 1;
    if ( setsockopt(
	     listen_fd, SOL_SOCKET, SO_REUSEADDR, (char*) &on,
	     sizeof(on) ) < 0 )
	syslog( LOG_CRIT, "setsockopt SO_REUSEADDR - %m" );

    /* Bind to it. */
    if ( bind( listen_fd, &saP->sa, sockaddr_len( saP ) ) < 0 )
	{
	syslog(
	    LOG_CRIT, "bind %.80s - %m", httpd_ntoa( saP ) );
	(void) close( listen_fd );
	return -1;
	}

    /* Set the listen file descriptor to no-delay / non-blocking mode. */
    flags = fcntl( listen_fd, F_GETFL, 0 );
    if ( flags == -1 )
	{
	syslog( LOG_CRIT, "fcntl F_GETFL - %m" );
	(void) close( listen_fd );
	return -1;
	}
    if ( fcntl( listen_fd, F_SETFL, flags | O_NDELAY ) < 0 )
	{
	syslog( LOG_CRIT, "fcntl O_NDELAY - %m" );
	(void) close( listen_fd );
	return -1;
	}

    /* Start a listen going. */
    if ( listen( listen_fd, LISTEN_BACKLOG ) < 0 )
	{
	syslog( LOG_CRIT, "listen - %m" );
	(void) close( listen_fd );
	return -1;
	}

    /* Use accept filtering, if available. */
#ifdef SO_ACCEPTFILTER
    {
#if ( __FreeBSD_version >= 411000 )
#define ACCEPT_FILTER_NAME "httpready"
#else
#define ACCEPT_FILTER_NAME "dataready"
#endif
    struct accept_filter_arg af;
    (void) bzero( &af, sizeof(af) );
    (void) strcpy( af.af_name, ACCEPT_FILTER_NAME );
    (void) setsockopt(
	listen_fd, SOL_SOCKET, SO_ACCEPTFILTER, (char*) &af, sizeof(af) );
    }
#endif /* SO_ACCEPTFILTER */

    return listen_fd;
    }


void
httpd_set_logfp( httpd_server* hs, FILE* logfp )
    {
    if ( hs->logfp != (FILE*) 0 )
	(void) fclose( hs->logfp );
    hs->logfp = logfp;
    }


void
httpd_terminate( httpd_server* hs )
    {
    httpd_unlisten( hs );
    if ( hs->logfp != (FILE*) 0 )
	(void) fclose( hs->logfp );
    free_httpd_server( hs );
    }


void
httpd_unlisten( httpd_server* hs )
    {
    if ( hs->listen4_fd != -1 )
	{
	(void) close( hs->listen4_fd );
	hs->listen4_fd = -1;
	}
    if ( hs->listen6_fd != -1 )
	{
	(void) close( hs->listen6_fd );
	hs->listen6_fd = -1;
	}
    }


/* Conditional macro to allow two alternate forms for use in the built-in
** error pages.  If EXPLICIT_ERROR_PAGES is defined, the second and more
** explicit error form is used; otherwise, the first and more generic
** form is used.
*/
#ifdef EXPLICIT_ERROR_PAGES
#define ERROR_FORM(a,b) b
#else /* EXPLICIT_ERROR_PAGES */
#define ERROR_FORM(a,b) a
#endif /* EXPLICIT_ERROR_PAGES */


static char* ok200title = "OK";
static char* ok206title = "Partial Content";

static char* err302title = "Found";
static char* err302form = "The actual URL is '%.80s'.\n";

static char* err304title = "Not Modified";

char* httpd_err400title = "Bad Request";
char* httpd_err400form =
    "Your request has bad syntax or is inherently impossible to satisfy.\n";

#ifdef AUTH_FILE
static char* err401title = "Unauthorized";
static char* err401form =
    "Authorization required for the URL '%.80s'.\n";
#endif /* AUTH_FILE */

static char* err403title = "Forbidden";
#ifndef EXPLICIT_ERROR_PAGES
static char* err403form =
    "You do not have permission to get URL '%.80s' from this server.\n";
#endif /* !EXPLICIT_ERROR_PAGES */

static char* err404title = "Not Found";
static char* err404form =
    "The requested URL '%.80s' was not found on this server.\n";

char* httpd_err408title = "Request Timeout";
char* httpd_err408form =
    "No request appeared within a reasonable time period.\n";

static char* err451title = "Unavailable For Legal Reasons";
static char* err451form =
    "You do not have legal permission to get URL '%.80s' from this server.\n";

static char* err500title = "Internal Error";
static char* err500form =
    "There was an unusual problem serving the requested URL '%.80s'.\n";

static char* err501title = "Not Implemented";
static char* err501form =
    "The requested method '%.80s' is not implemented by this server.\n";

char* httpd_err503title = "Service Temporarily Overloaded";
char* httpd_err503form =
    "The requested URL '%.80s' is temporarily overloaded.  Please try again later.\n";


/* Append a string to the buffer waiting to be sent as response. */
static void
add_response( httpd_conn* hc, char* str )
    {
    size_t len;

    len = strlen( str );
    httpd_realloc_str( &hc->response, &hc->maxresponse, hc->responselen + len );
    (void) memmove( &(hc->response[hc->responselen]), str, len );
    hc->responselen += len;
    }

/* Send the buffered response. */
void
httpd_write_response( httpd_conn* hc )
    {
    /* If we are in a sub-process, turn off no-delay mode. */
    if ( sub_process )
	httpd_clear_ndelay( hc->conn_fd );
    /* Send the response, if necessary. */
    if ( hc->responselen > 0 )
	{
	(void) httpd_write_fully( hc->conn_fd, hc->response, hc->responselen );
	hc->responselen = 0;
	}
    }


/* Set no-delay / non-blocking mode on a socket. */
void
httpd_set_ndelay( int fd )
    {
    int flags, newflags;

    flags = fcntl( fd, F_GETFL, 0 );
    if ( flags != -1 )
	{
	newflags = flags | (int) O_NDELAY;
	if ( newflags != flags )
	    (void) fcntl( fd, F_SETFL, newflags );
	}
    }


/* Clear no-delay / non-blocking mode on a socket. */
void
httpd_clear_ndelay( int fd )
    {
    int flags, newflags;

    flags = fcntl( fd, F_GETFL, 0 );
    if ( flags != -1 )
	{
	newflags = flags & ~ (int) O_NDELAY;
	if ( newflags != flags )
	    (void) fcntl( fd, F_SETFL, newflags );
	}
    }


static void
send_mime( httpd_conn* hc, int status, char* title, char* encodings, char* extraheads, char* type, off_t length, time_t mod )
    {
    time_t now, expires;
    const char* rfc1123fmt = "%a, %d %b %Y %H:%M:%S GMT";
    char nowbuf[100];
    char modbuf[100];
    char expbuf[100];
    char fixed_type[500];
    char buf[1000];
    int partial_content;
    int s100;

    hc->status = status;
    hc->bytes_to_send = length;
    if ( hc->mime_flag )
	{
	if ( status == 200 && hc->got_range &&
	     ( hc->last_byte_index >= hc->first_byte_index ) &&
	     ( ( hc->last_byte_index != length - 1 ) ||
	       ( hc->first_byte_index != 0 ) ) &&
	     ( hc->range_if == (time_t) -1 ||
	       hc->range_if == hc->sb.st_mtime ) )
	    {
	    partial_content = 1;
	    hc->status = status = 206;
	    title = ok206title;
	    }
	else
	    {
	    partial_content = 0;
	    hc->got_range = 0;
	    }

	now = time( (time_t*) 0 );
	if ( mod == (time_t) 0 )
	    mod = now;
	(void) strftime( nowbuf, sizeof(nowbuf), rfc1123fmt, gmtime( &now ) );
	(void) strftime( modbuf, sizeof(modbuf), rfc1123fmt, gmtime( &mod ) );
	(void) my_snprintf(
	    fixed_type, sizeof(fixed_type), type, hc->hs->charset );
	(void) my_snprintf( buf, sizeof(buf),
	    "%.20s %d %s\015\012Server: %s\015\012Content-Type: %s\015\012Date: %s\015\012Last-Modified: %s\015\012Accept-Ranges: bytes\015\012Connection: close\015\012",
	    hc->protocol, status, title, EXPOSED_SERVER_SOFTWARE, fixed_type,
	    nowbuf, modbuf );
	add_response( hc, buf );
	s100 = status / 100;
	if ( s100 != 2 && s100 != 3 )
	    {
	    (void) my_snprintf( buf, sizeof(buf),
		"Cache-Control: no-cache,no-store\015\012" );
	    add_response( hc, buf );
	    }
	if ( encodings[0] != '\0' )
	    {
	    (void) my_snprintf( buf, sizeof(buf),
		"Content-Encoding: %s\015\012", encodings );
	    add_response( hc, buf );
	    }
	if ( partial_content )
	    {
	    (void) my_snprintf( buf, sizeof(buf),
		"Content-Range: bytes %lld-%lld/%lld\015\012Content-Length: %lld\015\012",
		(long long) hc->first_byte_index,
		(long long) hc->last_byte_index,
		(long long) length,
		(long long) ( hc->last_byte_index - hc->first_byte_index + 1 ) );
	    add_response( hc, buf );
	    }
	else if ( length >= 0 )
	    {
	    (void) my_snprintf( buf, sizeof(buf),
		"Content-Length: %lld\015\012", (long long) length );
	    add_response( hc, buf );
	    }
	if ( hc->hs->p3p[0] != '\0' )
	    {
	    (void) my_snprintf( buf, sizeof(buf), "P3P: %s\015\012", hc->hs->p3p );
	    add_response( hc, buf );
	    }
	if ( hc->hs->max_age >= 0 )
	    {
	    expires = now + hc->hs->max_age;
	    (void) strftime(
		expbuf, sizeof(expbuf), rfc1123fmt, gmtime( &expires ) );
	    (void) my_snprintf( buf, sizeof(buf),
		"Cache-Control: max-age=%d\015\012Expires: %s\015\012",
		hc->hs->max_age, expbuf );
	    add_response( hc, buf );
	    }
	if ( extraheads[0] != '\0' )
	    add_response( hc, extraheads );
	add_response( hc, "\015\012" );
	}
    }


static int str_alloc_count = 0;
static size_t str_alloc_size = 0;

void
httpd_realloc_str( char** strP, size_t* maxsizeP, size_t size )
    {
    if ( *maxsizeP == 0 )
	{
	*maxsizeP = MAX( 200, size + 100 );
	*strP = NEW( char, *maxsizeP + 1 );
	++str_alloc_count;
	str_alloc_size += *maxsizeP;
	}
    else if ( size > *maxsizeP )
	{
	str_alloc_size -= *maxsizeP;
	*maxsizeP = MAX( *maxsizeP * 2, size * 5 / 4 );
	*strP = RENEW( *strP, char, *maxsizeP + 1 );
	str_alloc_size += *maxsizeP;
	}
    else
	return;
    if ( *strP == (char*) 0 )
	{
	syslog(
	    LOG_ERR, "out of memory reallocating a string to %ld bytes",
	    (long) *maxsizeP );
	exit( 1 );
	}
    }


static void
send_response( httpd_conn* hc, int status, char* title, char* extraheads, char* form, char* arg )
    {
    char defanged_arg[1000], buf[2000];

    send_mime(
	hc, status, title, "", extraheads, "text/html; charset=%s", (off_t) -1,
	(time_t) 0 );
    (void) my_snprintf( buf, sizeof(buf), "\
<!DOCTYPE html PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\" \"http://www.w3.org/TR/html4/loose.dtd\">\n\
\n\
<html>\n\
\n\
  <head>\n\
    <meta http-equiv=\"Content-type\" content=\"text/html;charset=UTF-8\">\n\
    <title>%d %s</title>\n\
  </head>\n\
\n\
  <body bgcolor=\"#cc9999\" text=\"#000000\" link=\"#2020ff\" vlink=\"#4040cc\">\n\
\n\
    <h2>%d %s</h2>\n",
	status, title, status, title );
    add_response( hc, buf );
    defang( arg, defanged_arg, sizeof(defanged_arg) );
    (void) my_snprintf( buf, sizeof(buf), form, defanged_arg );
    add_response( hc, buf );
    if ( match( "**MSIE**", hc->useragent ) )
	{
	int n;
	add_response( hc, "<!--\n" );
	for ( n = 0; n < 6; ++n )
	    add_response( hc, "Padding so that MSIE deigns to show this error instead of its own canned one.\n");
	add_response( hc, "-->\n" );
	}
    send_response_tail( hc );
    }


static void
send_response_tail( httpd_conn* hc )
    {
    char buf[1000];

    (void) my_snprintf( buf, sizeof(buf), "\
    <hr>\n\
\n\
    <address><a href=\"%s\">%s</a></address>\n\
\n\
  </body>\n\
\n\
</html>\n",
	SERVER_ADDRESS, EXPOSED_SERVER_SOFTWARE );
    add_response( hc, buf );
    }


static void
defang( char* str, char* dfstr, int dfsize )
    {
    char* cp1;
    char* cp2;

    for ( cp1 = str, cp2 = dfstr;
	  *cp1 != '\0' && cp2 - dfstr < dfsize - 5;
	  ++cp1, ++cp2 )
	{
	switch ( *cp1 )
	    {
	    case '<':
	    *cp2++ = '&';
	    *cp2++ = 'l';
	    *cp2++ = 't';
	    *cp2 = ';';
	    break;
	    case '>':
	    *cp2++ = '&';
	    *cp2++ = 'g';
	    *cp2++ = 't';
	    *cp2 = ';';
	    break;
	    default:
	    *cp2 = *cp1;
	    break;
	    }
	}
    *cp2 = '\0';
    }


void
httpd_send_err( httpd_conn* hc, int status, char* title, char* extraheads, char* form, char* arg )
    {
#ifdef ERR_DIR

    char filename[1000];

    /* Try virtual host error page. */
    if ( hc->hs->vhost && hc->hostdir[0] != '\0' )
	{
	(void) my_snprintf( filename, sizeof(filename),
	    "%s/%s/err%d.html", hc->hostdir, ERR_DIR, status );
	if ( send_err_file( hc, status, title, extraheads, filename ) )
	    return;
	}

    /* Try server-wide error page. */
    (void) my_snprintf( filename, sizeof(filename),
	"%s/err%d.html", ERR_DIR, status );
    if ( send_err_file( hc, status, title, extraheads, filename ) )
	return;

    /* Fall back on built-in error page. */
    send_response( hc, status, title, extraheads, form, arg );

#else /* ERR_DIR */

    send_response( hc, status, title, extraheads, form, arg );

#endif /* ERR_DIR */
    }


#ifdef ERR_DIR
static int
send_err_file( httpd_conn* hc, int status, char* title, char* extraheads, char* filename )
    {
    FILE* fp;
    char buf[1000];
    size_t r;

    fp = fopen( filename, "r" );
    if ( fp == (FILE*) 0 )
	return 0;
    send_mime(
	hc, status, title, "", extraheads, "text/html; charset=%s", (off_t) -1,
	(time_t) 0 );
    for (;;)
	{
	r = fread( buf, 1, sizeof(buf) - 1, fp );
	if ( r == 0 )
	    break;
	buf[r] = '\0';
	add_response( hc, buf );
	}
    (void) fclose( fp );

#ifdef ERR_APPEND_SERVER_INFO
    send_response_tail( hc );
#endif /* ERR_APPEND_SERVER_INFO */

    return 1;
    }
#endif /* ERR_DIR */


#ifdef AUTH_FILE

static void
send_authenticate( httpd_conn* hc, char* realm )
    {
    static char* header;
    static size_t maxheader = 0;
    static char headstr[] = "WWW-Authenticate: Basic realm=\"";

    httpd_realloc_str(
	&header, &maxheader, sizeof(headstr) + strlen( realm ) + 3 );
    (void) my_snprintf( header, maxheader, "%s%s\"\015\012", headstr, realm );
    httpd_send_err( hc, 401, err401title, header, err401form, hc->encodedurl );
    /* If the request was a POST then there might still be data to be read,
    ** so we need to do a lingering close.
    */
    if ( hc->method == METHOD_POST )
	hc->should_linger = 1;
    }


/* Base-64 decoding.  This represents binary data as printable ASCII
** characters.  Three 8-bit binary bytes are turned into four 6-bit
** values, like so:
**
**   [11111111]  [22222222]  [33333333]
**
**   [111111] [112222] [222233] [333333]
**
** Then the 6-bit values are represented using the characters "A-Za-z0-9+/".
*/

static int b64_decode_table[256] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* 00-0F */
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* 10-1F */
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,  /* 20-2F */
    52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,  /* 30-3F */
    -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,  /* 40-4F */
    15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,  /* 50-5F */
    -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,  /* 60-6F */
    41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,  /* 70-7F */
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* 80-8F */
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* 90-9F */
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* A0-AF */
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* B0-BF */
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* C0-CF */
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* D0-DF */
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* E0-EF */
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1   /* F0-FF */
    };

/* Do base-64 decoding on a string.  Ignore any non-base64 bytes.
** Return the actual number of bytes generated.  The decoded size will
** be at most 3/4 the size of the encoded, and may be smaller if there
** are padding characters (blanks, newlines).
*/
static int
b64_decode( const char* str, unsigned char* space, int size )
    {
    const char* cp;
    int space_idx, phase;
    int d, prev_d = 0;
    unsigned char c;

    space_idx = 0;
    phase = 0;
    for ( cp = str; *cp != '\0'; ++cp )
	{
	d = b64_decode_table[(int) ((unsigned char) *cp)];
	if ( d != -1 )
	    {
	    switch ( phase )
		{
		case 0:
		++phase;
		break;
		case 1:
		c = ( ( prev_d << 2 ) | ( ( d & 0x30 ) >> 4 ) );
		if ( space_idx < size )
		    space[space_idx++] = c;
		++phase;
		break;
		case 2:
		c = ( ( ( prev_d & 0xf ) << 4 ) | ( ( d & 0x3c ) >> 2 ) );
		if ( space_idx < size )
		    space[space_idx++] = c;
		++phase;
		break;
		case 3:
		c = ( ( ( prev_d & 0x03 ) << 6 ) | d );
		if ( space_idx < size )
		    space[space_idx++] = c;
		phase = 0;
		break;
		}
	    prev_d = d;
	    }
	}
    return space_idx;
    }


/* Returns -1 == unauthorized, 0 == no auth file, 1 = authorized. */
static int
auth_check( httpd_conn* hc, char* dirname  )
    {
    if ( hc->hs->global_passwd )
	{
	char* topdir;
	if ( hc->hs->vhost && hc->hostdir[0] != '\0' )
	    topdir = hc->hostdir;
	else
	    topdir = ".";
	switch ( auth_check2( hc, topdir ) )
	    {
	    case -1:
	    return -1;
	    case 1:
	    return 1;
	    }
	}
    return auth_check2( hc, dirname );
    }


/* Returns -1 == unauthorized, 0 == no auth file, 1 = authorized. */
static int
auth_check2( httpd_conn* hc, char* dirname  )
    {
    static char* authpath;
    static size_t maxauthpath = 0;
    struct stat sb;
    char authinfo[500];
    char* authpass;
    char* colon;
    int l;
    FILE* fp;
    char line[500];
    char* cryp;
    static char* prevauthpath;
    static size_t maxprevauthpath = 0;
    static time_t prevmtime;
    static char* prevuser;
    static size_t maxprevuser = 0;
    static char* prevcryp;
    static size_t maxprevcryp = 0;

    /* Construct auth filename. */
    httpd_realloc_str(
	&authpath, &maxauthpath, strlen( dirname ) + 1 + sizeof(AUTH_FILE) );
    (void) my_snprintf( authpath, maxauthpath, "%s/%s", dirname, AUTH_FILE );

    /* Does this directory have an auth file? */
    if ( stat( authpath, &sb ) < 0 )
	/* Nope, let the request go through. */
	return 0;

    /* Does this request contain basic authorization info? */
    if ( hc->authorization[0] == '\0' ||
	 strncmp( hc->authorization, "Basic ", 6 ) != 0 )
	{
	/* Nope, return a 401 Unauthorized. */
	send_authenticate( hc, dirname );
	return -1;
	}

    /* Decode it. */
    l = b64_decode(
	&(hc->authorization[6]), (unsigned char*) authinfo,
	sizeof(authinfo) - 1 );
    authinfo[l] = '\0';
    /* Split into user and password. */
    authpass = strchr( authinfo, ':' );
    if ( authpass == (char*) 0 )
	{
	/* No colon?  Bogus auth info. */
	send_authenticate( hc, dirname );
	return -1;
	}
    *authpass++ = '\0';
    /* If there are more fields, cut them off. */
    colon = strchr( authpass, ':' );
    if ( colon != (char*) 0 )
	*colon = '\0';

    /* See if we have a cached entry and can use it. */
    if ( maxprevauthpath != 0 &&
	 strcmp( authpath, prevauthpath ) == 0 &&
	 sb.st_mtime == prevmtime &&
	 strcmp( authinfo, prevuser ) == 0 )
	{
	/* Yes.  Check against the cached encrypted password. */
	if ( strcmp( crypt( authpass, prevcryp ), prevcryp ) == 0 )
	    {
	    /* Ok! */
	    httpd_realloc_str(
		&hc->remoteuser, &hc->maxremoteuser, strlen( authinfo ) );
	    (void) strcpy( hc->remoteuser, authinfo );
	    return 1;
	    }
	else
	    {
	    /* No. */
	    send_authenticate( hc, dirname );
	    return -1;
	    }
	}

    /* Open the password file. */
    fp = fopen( authpath, "r" );
    if ( fp == (FILE*) 0 )
	{
	/* The file exists but we can't open it?  Disallow access. */
	syslog(
	    LOG_ERR, "%.80s auth file %.80s could not be opened - %m",
	    httpd_ntoa( &hc->client_addr ), authpath );
	httpd_send_err(
	    hc, 403, err403title, "",
	    ERROR_FORM( err403form, "The requested URL '%.80s' is protected by an authentication file, but the authentication file cannot be opened.\n" ),
	    hc->encodedurl );
	return -1;
	}

    /* Read it. */
    while ( fgets( line, sizeof(line), fp ) != (char*) 0 )
	{
	/* Nuke newline. */
	l = strlen( line );
	if ( line[l - 1] == '\n' )
	    line[l - 1] = '\0';
	/* Split into user and encrypted password. */
	cryp = strchr( line, ':' );
	if ( cryp == (char*) 0 )
	    continue;
	*cryp++ = '\0';
	/* Is this the right user? */
	if ( strcmp( line, authinfo ) == 0 )
	    {
	    /* Yes. */
	    (void) fclose( fp );
	    /* So is the password right? */
	    if ( strcmp( crypt( authpass, cryp ), cryp ) == 0 )
		{
		/* Ok! */
		httpd_realloc_str(
		    &hc->remoteuser, &hc->maxremoteuser, strlen( line ) );
		(void) strcpy( hc->remoteuser, line );
		/* And cache this user's info for next time. */
		httpd_realloc_str(
		    &prevauthpath, &maxprevauthpath, strlen( authpath ) );
		(void) strcpy( prevauthpath, authpath );
		prevmtime = sb.st_mtime;
		httpd_realloc_str(
		    &prevuser, &maxprevuser, strlen( authinfo ) );
		(void) strcpy( prevuser, authinfo );
		httpd_realloc_str( &prevcryp, &maxprevcryp, strlen( cryp ) );
		(void) strcpy( prevcryp, cryp );
		return 1;
		}
	    else
		{
		/* No. */
		send_authenticate( hc, dirname );
		return -1;
		}
	    }
	}

    /* Didn't find that user.  Access denied. */
    (void) fclose( fp );
    send_authenticate( hc, dirname );
    return -1;
    }

#endif /* AUTH_FILE */


static void
send_dirredirect( httpd_conn* hc )
    {
    static char* location;
    static char* header;
    static size_t maxlocation = 0, maxheader = 0;
    static char headstr[] = "Location: ";

    if ( hc->query[0] != '\0')
	{
	char* cp = strchr( hc->encodedurl, '?' );
	if ( cp != (char*) 0 )	/* should always find it */
	    *cp = '\0';
	httpd_realloc_str(
	    &location, &maxlocation,
	    strlen( hc->encodedurl ) + 2 + strlen( hc->query ) );
	(void) my_snprintf( location, maxlocation,
	    "%s/?%s", hc->encodedurl, hc->query );
	}
    else
	{
	httpd_realloc_str(
	    &location, &maxlocation, strlen( hc->encodedurl ) + 1 );
	(void) my_snprintf( location, maxlocation,
	    "%s/", hc->encodedurl );
	}
    httpd_realloc_str(
	&header, &maxheader, sizeof(headstr) + strlen( location ) );
    (void) my_snprintf( header, maxheader,
	"%s%s\015\012", headstr, location );
    send_response( hc, 302, err302title, header, err302form, location );
    }


char*
httpd_method_str( int method )
    {
    switch ( method )
	{
	case METHOD_GET: return "GET";
	case METHOD_HEAD: return "HEAD";
	case METHOD_POST: return "POST";
	case METHOD_PUT: return "PUT";
	case METHOD_DELETE: return "DELETE";
	case METHOD_TRACE: return "TRACE";
	default: return "UNKNOWN";
	}
    }


static int
hexit( char c )
    {
    if ( c >= '0' && c <= '9' )
	return c - '0';
    if ( c >= 'a' && c <= 'f' )
	return c - 'a' + 10;
    if ( c >= 'A' && c <= 'F' )
	return c - 'A' + 10;
    return 0;           /* shouldn't happen, we're guarded by isxdigit() */
    }


/* Copies and decodes a string.  It's ok for from and to to be the
** same string.
*/
static void
strdecode( char* to, char* from )
    {
    for ( ; *from != '\0'; ++to, ++from )
	{
	if ( from[0] == '%' && isxdigit( from[1] ) && isxdigit( from[2] ) )
	    {
	    *to = hexit( from[1] ) * 16 + hexit( from[2] );
	    from += 2;
	    }
	else
	    *to = *from;
	}
    *to = '\0';
    }


#ifdef GENERATE_INDEXES
/* Copies and encodes a string. */
static void
strencode( char* to, int tosize, char* from )
    {
    int tolen;

    for ( tolen = 0; *from != '\0' && tolen + 4 < tosize; ++from )
	{
	if ( isalnum(*from) || strchr( "/_.-~", *from ) != (char*) 0 )
	    {
	    *to = *from;
	    ++to;
	    ++tolen;
	    }
	else
	    {
	    (void) sprintf( to, "%%%02x", (int) *from & 0xff );
	    to += 3;
	    tolen += 3;
	    }
	}
    *to = '\0';
    }
#endif /* GENERATE_INDEXES */


#ifdef TILDE_MAP_1
/* Map a ~username/whatever URL into <prefix>/username. */
static int
tilde_map_1( httpd_conn* hc )
    {
    static char* temp;
    static size_t maxtemp = 0;
    int len;
    static char* prefix = TILDE_MAP_1;

    len = strlen( hc->expnfilename ) - 1;
    httpd_realloc_str( &temp, &maxtemp, len );
    (void) strcpy( temp, &hc->expnfilename[1] );
    httpd_realloc_str(
	&hc->expnfilename, &hc->maxexpnfilename, strlen( prefix ) + 1 + len );
    (void) strcpy( hc->expnfilename, prefix );
    if ( prefix[0] != '\0' )
	(void) strcat( hc->expnfilename, "/" );
    (void) strcat( hc->expnfilename, temp );
    return 1;
    }
#endif /* TILDE_MAP_1 */

#ifdef TILDE_MAP_2
/* Map a ~username/whatever URL into <user's homedir>/<postfix>. */
static int
tilde_map_2( httpd_conn* hc )
    {
    static char* temp;
    static size_t maxtemp = 0;
    static char* postfix = TILDE_MAP_2;
    char* cp;
    struct passwd* pw;
    char* alt;
    char* rest;

    /* Get the username. */
    httpd_realloc_str( &temp, &maxtemp, strlen( hc->expnfilename ) - 1 );
    (void) strcpy( temp, &hc->expnfilename[1] );
    cp = strchr( temp, '/' );
    if ( cp != (char*) 0 )
	*cp++ = '\0';
    else
	cp = "";

    /* Get the passwd entry. */
    pw = getpwnam( temp );
    if ( pw == (struct passwd*) 0 )
	return 0;

    /* Set up altdir. */
    httpd_realloc_str(
	&hc->altdir, &hc->maxaltdir,
	strlen( pw->pw_dir ) + 1 + strlen( postfix ) );
    (void) strcpy( hc->altdir, pw->pw_dir );
    if ( postfix[0] != '\0' )
	{
	(void) strcat( hc->altdir, "/" );
	(void) strcat( hc->altdir, postfix );
	}
    alt = expand_symlinks( hc->altdir, &rest, 0, 1 );
    if ( rest[0] != '\0' )
	return 0;
    httpd_realloc_str( &hc->altdir, &hc->maxaltdir, strlen( alt ) );
    (void) strcpy( hc->altdir, alt );

    /* And the filename becomes altdir plus the post-~ part of the original. */
    httpd_realloc_str(
	&hc->expnfilename, &hc->maxexpnfilename,
	strlen( hc->altdir ) + 1 + strlen( cp ) );
    (void) my_snprintf( hc->expnfilename, hc->maxexpnfilename,
	"%s/%s", hc->altdir, cp );

    /* For this type of tilde mapping, we want to defeat vhost mapping. */
    hc->tildemapped = 1;

    return 1;
    }
#endif /* TILDE_MAP_2 */


/* Virtual host mapping. */
static int
vhost_map( httpd_conn* hc )
    {
    httpd_sockaddr sa;
    socklen_t sz;
    static char* tempfilename;
    static size_t maxtempfilename = 0;
    char* cp1;
    int len;
#ifdef VHOST_DIRLEVELS
    int i;
    char* cp2;
#endif /* VHOST_DIRLEVELS */

    /* Figure out the virtual hostname. */
    if ( hc->reqhost[0] != '\0' )
	hc->hostname = hc->reqhost;
    else if ( hc->hdrhost[0] != '\0' )
	hc->hostname = hc->hdrhost;
    else
	{
	sz = sizeof(sa);
	if ( getsockname( hc->conn_fd, &sa.sa, &sz ) < 0 )
	    {
	    syslog( LOG_ERR, "getsockname - %m" );
	    return 0;
	    }
	hc->hostname = httpd_ntoa( &sa );
	}
    /* Pound it to lower case. */
    for ( cp1 = hc->hostname; *cp1 != '\0'; ++cp1 )
	if ( isupper( *cp1 ) )
	    *cp1 = tolower( *cp1 );

    if ( hc->tildemapped )
	return 1;

    /* Figure out the host directory. */
#ifdef VHOST_DIRLEVELS
    httpd_realloc_str(
	&hc->hostdir, &hc->maxhostdir,
	strlen( hc->hostname ) + 2 * VHOST_DIRLEVELS );
    if ( strncmp( hc->hostname, "www.", 4 ) == 0 )
	cp1 = &hc->hostname[4];
    else
	cp1 = hc->hostname;
    for ( cp2 = hc->hostdir, i = 0; i < VHOST_DIRLEVELS; ++i )
	{
	/* Skip dots in the hostname.  If we don't, then we get vhost
	** directories in higher level of filestructure if dot gets
	** involved into path construction.  It's `while' used here instead
	** of `if' for it's possible to have a hostname formed with two
	** dots at the end of it.
	*/
	while ( *cp1 == '.' )
	    ++cp1;
	/* Copy a character from the hostname, or '_' if we ran out. */
	if ( *cp1 != '\0' )
	    *cp2++ = *cp1++;
	else
	    *cp2++ = '_';
	/* Copy a slash. */
	*cp2++ = '/';
	}
    (void) strcpy( cp2, hc->hostname );
#else /* VHOST_DIRLEVELS */
    httpd_realloc_str( &hc->hostdir, &hc->maxhostdir, strlen( hc->hostname ) );
    (void) strcpy( hc->hostdir, hc->hostname );
#endif /* VHOST_DIRLEVELS */

    /* Prepend hostdir to the filename. */
    len = strlen( hc->expnfilename );
    httpd_realloc_str( &tempfilename, &maxtempfilename, len );
    (void) strcpy( tempfilename, hc->expnfilename );
    httpd_realloc_str(
	&hc->expnfilename, &hc->maxexpnfilename,
	strlen( hc->hostdir ) + 1 + len );
    (void) strcpy( hc->expnfilename, hc->hostdir );
    (void) strcat( hc->expnfilename, "/" );
    (void) strcat( hc->expnfilename, tempfilename );
    return 1;
    }


/* Expands all symlinks in the given filename, eliding ..'s and leading /'s.
** Returns the expanded path (pointer to static string), or (char*) 0 on
** errors.  Also returns, in the string pointed to by restP, any trailing
** parts of the path that don't exist.
**
** This is a fairly nice little routine.  It handles any size filenames
** without excessive mallocs.
*/
static char*
expand_symlinks( char* path, char** restP, int no_symlink_check, int tildemapped )
    {
    static char* checked;
    static char* rest;
    char lnk[5000];
    static size_t maxchecked = 0, maxrest = 0;
    size_t checkedlen, restlen, linklen, prevcheckedlen, prevrestlen;
    int nlinks, i;
    char* r;
    char* cp1;
    char* cp2;

    if ( no_symlink_check )
	{
	/* If we are chrooted, we can actually skip the symlink-expansion,
	** since it's impossible to get out of the tree.  However, we still
	** need to do the pathinfo check, and the existing symlink expansion
	** code is a pretty reasonable way to do this.  So, what we do is
	** a single stat() of the whole filename - if it exists, then we
	** return it as is with nothing in restP.  If it doesn't exist, we
	** fall through to the existing code.
	**
	** One side-effect of this is that users can't symlink to central
	** approved CGIs any more.  The workaround is to use the central
	** URL for the CGI instead of a local symlinked one.
	*/
	struct stat sb;
	if ( stat( path, &sb ) != -1 )
	    {
	    checkedlen = strlen( path );
	    httpd_realloc_str( &checked, &maxchecked, checkedlen );
	    (void) strcpy( checked, path );
	    /* Trim trailing slashes. */
	    while ( checked[checkedlen - 1] == '/' )
		{
		checked[checkedlen - 1] = '\0';
		--checkedlen;
		}
	    httpd_realloc_str( &rest, &maxrest, 0 );
	    rest[0] = '\0';
	    *restP = rest;
	    return checked;
	    }
	}

    /* Start out with nothing in checked and the whole filename in rest. */
    httpd_realloc_str( &checked, &maxchecked, 1 );
    checked[0] = '\0';
    checkedlen = 0;
    restlen = strlen( path );
    httpd_realloc_str( &rest, &maxrest, restlen );
    (void) strcpy( rest, path );
    if ( rest[restlen - 1] == '/' )
	rest[--restlen] = '\0';         /* trim trailing slash */
    if ( ! tildemapped )
	/* Remove any leading slashes. */
	while ( rest[0] == '/' )
	    {
	    (void) ol_strcpy( rest, &(rest[1]) );
	    --restlen;
	    }
    r = rest;
    nlinks = 0;

    /* While there are still components to check... */
    while ( restlen > 0 )
	{
	/* Save current checkedlen in case we get a symlink.  Save current
	** restlen in case we get a non-existant component.
	*/
	prevcheckedlen = checkedlen;
	prevrestlen = restlen;

	/* Grab one component from r and transfer it to checked. */
	cp1 = strchr( r, '/' );
	if ( cp1 != (char*) 0 )
	    {
	    i = cp1 - r;
	    if ( i == 0 )
		{
		/* Special case for absolute paths. */
		httpd_realloc_str( &checked, &maxchecked, checkedlen + 1 );
		(void) strncpy( &checked[checkedlen], r, 1 );
		checkedlen += 1;
		}
	    else if ( strncmp( r, "..", MAX( i, 2 ) ) == 0 )
		{
		/* Ignore ..'s that go above the start of the path. */
		if ( checkedlen != 0 )
		    {
		    cp2 = strrchr( checked, '/' );
		    if ( cp2 == (char*) 0 )
			checkedlen = 0;
		    else if ( cp2 == checked )
			checkedlen = 1;
		    else
			checkedlen = cp2 - checked;
		    }
		}
	    else
		{
		httpd_realloc_str( &checked, &maxchecked, checkedlen + 1 + i );
		if ( checkedlen > 0 && checked[checkedlen-1] != '/' )
		    checked[checkedlen++] = '/';
		(void) strncpy( &checked[checkedlen], r, i );
		checkedlen += i;
		}
	    checked[checkedlen] = '\0';
	    r += i + 1;
	    restlen -= i + 1;
	    }
	else
	    {
	    /* No slashes remaining, r is all one component. */
	    if ( strcmp( r, ".." ) == 0 )
		{
		/* Ignore ..'s that go above the start of the path. */
		if ( checkedlen != 0 )
		    {
		    cp2 = strrchr( checked, '/' );
		    if ( cp2 == (char*) 0 )
			checkedlen = 0;
		    else if ( cp2 == checked )
			checkedlen = 1;
		    else
			checkedlen = cp2 - checked;
		    checked[checkedlen] = '\0';
		    }
		}
	    else
		{
		httpd_realloc_str(
		    &checked, &maxchecked, checkedlen + 1 + restlen );
		if ( checkedlen > 0 && checked[checkedlen-1] != '/' )
		    checked[checkedlen++] = '/';
		(void) strcpy( &checked[checkedlen], r );
		checkedlen += restlen;
		}
	    r += restlen;
	    restlen = 0;
	    }

	/* Try reading the current filename as a symlink */
	if ( checked[0] == '\0' )
	    continue;
	linklen = readlink( checked, lnk, sizeof(lnk) - 1 );
	if ( linklen == -1 )
	    {
	    if ( errno == EINVAL )
		continue;               /* not a symlink */
	    if ( errno == EACCES || errno == ENOENT || errno == ENOTDIR )
		{
		/* That last component was bogus.  Restore and return. */
		*restP = r - ( prevrestlen - restlen );
		if ( prevcheckedlen == 0 )
		    (void) strcpy( checked, "." );
		else
		    checked[prevcheckedlen] = '\0';
		return checked;
		}
	    syslog( LOG_ERR, "readlink %.80s - %m", checked );
	    return (char*) 0;
	    }
	++nlinks;
	if ( nlinks > MAX_LINKS )
	    {
	    syslog( LOG_ERR, "too many symlinks in %.80s", path );
	    return (char*) 0;
	    }
	lnk[linklen] = '\0';
	if ( lnk[linklen - 1] == '/' )
	    lnk[--linklen] = '\0';     /* trim trailing slash */

	/* Insert the link contents in front of the rest of the filename. */
	if ( restlen != 0 )
	    {
	    (void) ol_strcpy( rest, r );
	    httpd_realloc_str( &rest, &maxrest, restlen + linklen + 1 );
	    for ( i = restlen; i >= 0; --i )
		rest[i + linklen + 1] = rest[i];
	    (void) strcpy( rest, lnk );
	    rest[linklen] = '/';
	    restlen += linklen + 1;
	    r = rest;
	    }
	else
	    {
	    /* There's nothing left in the filename, so the link contents
	    ** becomes the rest.
	    */
	    httpd_realloc_str( &rest, &maxrest, linklen );
	    (void) strcpy( rest, lnk );
	    restlen = linklen;
	    r = rest;
	    }

	if ( rest[0] == '/' )
	    {
	    /* There must have been an absolute symlink - zero out checked. */
	    checked[0] = '\0';
	    checkedlen = 0;
	    }
	else
	    {
	    /* Re-check this component. */
	    checkedlen = prevcheckedlen;
	    checked[checkedlen] = '\0';
	    }
	}

    /* Ok. */
    *restP = r;
    if ( checked[0] == '\0' )
	(void) strcpy( checked, "." );
    return checked;
    }


int
httpd_get_conn( httpd_server* hs, int listen_fd, httpd_conn* hc )
    {
    httpd_sockaddr sa;
    socklen_t sz;

    if ( ! hc->initialized )
	{
	hc->read_size = 0;
	httpd_realloc_str( &hc->read_buf, &hc->read_size, 500 );
	hc->maxdecodedurl =
	    hc->maxorigfilename = hc->maxexpnfilename = hc->maxencodings =
	    hc->maxpathinfo = hc->maxquery = hc->maxaccept =
	    hc->maxaccepte = hc->maxreqhost = hc->maxhostdir =
	    hc->maxremoteuser = hc->maxresponse = 0;
#ifdef TILDE_MAP_2
	hc->maxaltdir = 0;
#endif /* TILDE_MAP_2 */
	httpd_realloc_str( &hc->decodedurl, &hc->maxdecodedurl, 1 );
	httpd_realloc_str( &hc->origfilename, &hc->maxorigfilename, 1 );
	httpd_realloc_str( &hc->expnfilename, &hc->maxexpnfilename, 0 );
	httpd_realloc_str( &hc->encodings, &hc->maxencodings, 0 );
	httpd_realloc_str( &hc->pathinfo, &hc->maxpathinfo, 0 );
	httpd_realloc_str( &hc->query, &hc->maxquery, 0 );
	httpd_realloc_str( &hc->accept, &hc->maxaccept, 0 );
	httpd_realloc_str( &hc->accepte, &hc->maxaccepte, 0 );
	httpd_realloc_str( &hc->reqhost, &hc->maxreqhost, 0 );
	httpd_realloc_str( &hc->hostdir, &hc->maxhostdir, 0 );
	httpd_realloc_str( &hc->remoteuser, &hc->maxremoteuser, 0 );
	httpd_realloc_str( &hc->response, &hc->maxresponse, 0 );
#ifdef TILDE_MAP_2
	httpd_realloc_str( &hc->altdir, &hc->maxaltdir, 0 );
#endif /* TILDE_MAP_2 */
	hc->initialized = 1;
	}

    /* Accept the new connection. */
    sz = sizeof(sa);
    hc->conn_fd = accept( listen_fd, &sa.sa, &sz );
    if ( hc->conn_fd < 0 )
	{
	if ( errno == EWOULDBLOCK )
	    return GC_NO_MORE;
	/* ECONNABORTED means the connection was closed by the client while
	** it was waiting in the listen queue.  It's not worth logging.
	*/
	if ( errno != ECONNABORTED )
	    syslog( LOG_ERR, "accept - %m" );
	return GC_FAIL;
	}
    if ( ! sockaddr_check( &sa ) )
	{
	syslog( LOG_ERR, "unknown sockaddr family" );
	close( hc->conn_fd );
	hc->conn_fd = -1;
	return GC_FAIL;
	}
    (void) fcntl( hc->conn_fd, F_SETFD, 1 );
    hc->hs = hs;
    (void) memset( &hc->client_addr, 0, sizeof(hc->client_addr) );
    (void) memmove( &hc->client_addr, &sa, sockaddr_len( &sa ) );
    hc->read_idx = 0;
    hc->checked_idx = 0;
    hc->checked_state = CHST_FIRSTWORD;
    hc->method = METHOD_UNKNOWN;
    hc->status = 0;
    hc->bytes_to_send = 0;
    hc->bytes_sent = 0;
    hc->encodedurl = "";
    hc->decodedurl[0] = '\0';
    hc->protocol = "UNKNOWN";
    hc->origfilename[0] = '\0';
    hc->expnfilename[0] = '\0';
    hc->encodings[0] = '\0';
    hc->pathinfo[0] = '\0';
    hc->query[0] = '\0';
    hc->referrer = "";
    hc->useragent = "";
    hc->accept[0] = '\0';
    hc->accepte[0] = '\0';
    hc->acceptl = "";
    hc->cookie = "";
    hc->contenttype = "";
    hc->reqhost[0] = '\0';
    hc->hdrhost = "";
    hc->hostdir[0] = '\0';
    hc->authorization = "";
    hc->remoteuser[0] = '\0';
    hc->response[0] = '\0';
#ifdef TILDE_MAP_2
    hc->altdir[0] = '\0';
#endif /* TILDE_MAP_2 */
    hc->responselen = 0;
    hc->if_modified_since = (time_t) -1;
    hc->range_if = (time_t) -1;
    hc->contentlength = -1;
    hc->type = "";
    hc->hostname = (char*) 0;
    hc->mime_flag = 1;
    hc->one_one = 0;
    hc->got_range = 0;
    hc->tildemapped = 0;
    hc->first_byte_index = 0;
    hc->last_byte_index = -1;
    hc->keep_alive = 0;
    hc->should_linger = 0;
    hc->file_address = (char*) 0;
    return GC_OK;
    }


/* Checks hc->read_buf to see whether a complete request has been read so far;
** either the first line has two words (an HTTP/0.9 request), or the first
** line has three words and there's a blank line present.
**
** hc->read_idx is how much has been read in; hc->checked_idx is how much we
** have checked so far; and hc->checked_state is the current state of the
** finite state machine.
*/
int
httpd_got_request( httpd_conn* hc )
    {
    char c;

    for ( ; hc->checked_idx < hc->read_idx; ++hc->checked_idx )
	{
	c = hc->read_buf[hc->checked_idx];
	switch ( hc->checked_state )
	    {
	    case CHST_FIRSTWORD:
	    switch ( c )
		{
		case ' ': case '\t':
		hc->checked_state = CHST_FIRSTWS;
		break;
		case '\012': case '\015':
		hc->checked_state = CHST_BOGUS;
		return GR_BAD_REQUEST;
		}
	    break;
	    case CHST_FIRSTWS:
	    switch ( c )
		{
		case ' ': case '\t':
		break;
		case '\012': case '\015':
		hc->checked_state = CHST_BOGUS;
		return GR_BAD_REQUEST;
		default:
		hc->checked_state = CHST_SECONDWORD;
		break;
		}
	    break;
	    case CHST_SECONDWORD:
	    switch ( c )
		{
		case ' ': case '\t':
		hc->checked_state = CHST_SECONDWS;
		break;
		case '\012': case '\015':
		/* The first line has only two words - an HTTP/0.9 request. */
		return GR_GOT_REQUEST;
		}
	    break;
	    case CHST_SECONDWS:
	    switch ( c )
		{
		case ' ': case '\t':
		break;
		case '\012': case '\015':
		hc->checked_state = CHST_BOGUS;
		return GR_BAD_REQUEST;
		default:
		hc->checked_state = CHST_THIRDWORD;
		break;
		}
	    break;
	    case CHST_THIRDWORD:
	    switch ( c )
		{
		case ' ': case '\t':
		hc->checked_state = CHST_THIRDWS;
		break;
		case '\012':
		hc->checked_state = CHST_LF;
		break;
		case '\015':
		hc->checked_state = CHST_CR;
		break;
		}
	    break;
	    case CHST_THIRDWS:
	    switch ( c )
		{
		case ' ': case '\t':
		break;
		case '\012':
		hc->checked_state = CHST_LF;
		break;
		case '\015':
		hc->checked_state = CHST_CR;
		break;
		default:
		hc->checked_state = CHST_BOGUS;
		return GR_BAD_REQUEST;
		}
	    break;
	    case CHST_LINE:
	    switch ( c )
		{
		case '\012':
		hc->checked_state = CHST_LF;
		break;
		case '\015':
		hc->checked_state = CHST_CR;
		break;
		}
	    break;
	    case CHST_LF:
	    switch ( c )
		{
		case '\012':
		/* Two newlines in a row - a blank line - end of request. */
		return GR_GOT_REQUEST;
		case '\015':
		hc->checked_state = CHST_CR;
		break;
		default:
		hc->checked_state = CHST_LINE;
		break;
		}
	    break;
	    case CHST_CR:
	    switch ( c )
		{
		case '\012':
		hc->checked_state = CHST_CRLF;
		break;
		case '\015':
		/* Two returns in a row - end of request. */
		return GR_GOT_REQUEST;
		default:
		hc->checked_state = CHST_LINE;
		break;
		}
	    break;
	    case CHST_CRLF:
	    switch ( c )
		{
		case '\012':
		/* Two newlines in a row - end of request. */
		return GR_GOT_REQUEST;
		case '\015':
		hc->checked_state = CHST_CRLFCR;
		break;
		default:
		hc->checked_state = CHST_LINE;
		break;
		}
	    break;
	    case CHST_CRLFCR:
	    switch ( c )
		{
		case '\012': case '\015':
		/* Two CRLFs or two CRs in a row - end of request. */
		return GR_GOT_REQUEST;
		default:
		hc->checked_state = CHST_LINE;
		break;
		}
	    break;
	    case CHST_BOGUS:
	    return GR_BAD_REQUEST;
	    }
	}
    return GR_NO_REQUEST;
    }


int
httpd_parse_request( httpd_conn* hc )
    {
    char* buf;
    char* method_str;
    char* url;
    char* protocol;
    char* reqhost;
    char* eol;
    char* cp;
    char* pi;

    hc->checked_idx = 0;	/* reset */
    method_str = bufgets( hc );
    url = strpbrk( method_str, " \t\012\015" );
    if ( url == (char*) 0 )
	{
	httpd_send_err( hc, 400, httpd_err400title, "", httpd_err400form, "" );
	return -1;
	}
    *url++ = '\0';
    url += strspn( url, " \t\012\015" );
    protocol = strpbrk( url, " \t\012\015" );
    if ( protocol == (char*) 0 )
	{
	protocol = "HTTP/0.9";
	hc->mime_flag = 0;
	}
    else
	{
	*protocol++ = '\0';
	protocol += strspn( protocol, " \t\012\015" );
	if ( *protocol != '\0' )
	    {
	    eol = strpbrk( protocol, " \t\012\015" );
	    if ( eol != (char*) 0 )
		*eol = '\0';
	    if ( strcasecmp( protocol, "HTTP/1.0" ) != 0 )
		hc->one_one = 1;
	    }
	}
    hc->protocol = protocol;

    /* Check for HTTP/1.1 absolute URL. */
    if ( strncasecmp( url, "http://", 7 ) == 0 )
	{
	if ( ! hc->one_one )
	    {
	    httpd_send_err( hc, 400, httpd_err400title, "", httpd_err400form, "" );
	    return -1;
	    }
	reqhost = url + 7;
	url = strchr( reqhost, '/' );
	if ( url == (char*) 0 )
	    {
	    httpd_send_err( hc, 400, httpd_err400title, "", httpd_err400form, "" );
	    return -1;
	    }
	*url = '\0';
	if ( strchr( reqhost, '/' ) != (char*) 0 || reqhost[0] == '.' )
	    {
	    httpd_send_err( hc, 400, httpd_err400title, "", httpd_err400form, "" );
	    return -1;
	    }
	httpd_realloc_str( &hc->reqhost, &hc->maxreqhost, strlen( reqhost ) );
	(void) strcpy( hc->reqhost, reqhost );
	*url = '/';
	}

    if ( *url != '/' )
	{
	httpd_send_err( hc, 400, httpd_err400title, "", httpd_err400form, "" );
	return -1;
	}

    if ( strcasecmp( method_str, httpd_method_str( METHOD_GET ) ) == 0 )
	hc->method = METHOD_GET;
    else if ( strcasecmp( method_str, httpd_method_str( METHOD_HEAD ) ) == 0 )
	hc->method = METHOD_HEAD;
    else if ( strcasecmp( method_str, httpd_method_str( METHOD_POST ) ) == 0 )
	hc->method = METHOD_POST;
    else if ( strcasecmp( method_str, httpd_method_str( METHOD_PUT ) ) == 0 )
	hc->method = METHOD_PUT;
    else if ( strcasecmp( method_str, httpd_method_str( METHOD_DELETE ) ) == 0 )
	hc->method = METHOD_DELETE;
    else if ( strcasecmp( method_str, httpd_method_str( METHOD_TRACE ) ) == 0 )
	hc->method = METHOD_TRACE;
    else
	{
	httpd_send_err( hc, 501, err501title, "", err501form, method_str );
	return -1;
	}

    hc->encodedurl = url;
    httpd_realloc_str(
	&hc->decodedurl, &hc->maxdecodedurl, strlen( hc->encodedurl ) );
    strdecode( hc->decodedurl, hc->encodedurl );

    httpd_realloc_str(
	&hc->origfilename, &hc->maxorigfilename, strlen( hc->decodedurl ) );
    (void) strcpy( hc->origfilename, &hc->decodedurl[1] );
    /* Special case for top-level URL. */
    if ( hc->origfilename[0] == '\0' )
	(void) strcpy( hc->origfilename, "." );

    /* Extract query string from encoded URL. */
    cp = strchr( hc->encodedurl, '?' );
    if ( cp != (char*) 0 )
	{
	++cp;
	httpd_realloc_str( &hc->query, &hc->maxquery, strlen( cp ) );
	(void) strcpy( hc->query, cp );
	/* Remove query from (decoded) origfilename. */
	cp = strchr( hc->origfilename, '?' );
	if ( cp != (char*) 0 )
	    *cp = '\0';
	}

    de_dotdot( hc->origfilename );
    if ( hc->origfilename[0] == '/' ||
	 ( hc->origfilename[0] == '.' && hc->origfilename[1] == '.' &&
	   ( hc->origfilename[2] == '\0' || hc->origfilename[2] == '/' ) ) )
	{
	httpd_send_err( hc, 400, httpd_err400title, "", httpd_err400form, "" );
	return -1;
	}

    if ( hc->mime_flag )
	{
	/* Read the MIME headers. */
	while ( ( buf = bufgets( hc ) ) != (char*) 0 )
	    {
	    if ( buf[0] == '\0' )
		break;
	    if ( strncasecmp( buf, "Referer:", 8 ) == 0 )
		{
		cp = &buf[8];
		cp += strspn( cp, " \t" );
		hc->referrer = cp;
		}
	    else if ( strncasecmp( buf, "Referrer:", 9 ) == 0 )
		{
		cp = &buf[9];
		cp += strspn( cp, " \t" );
		hc->referrer = cp;
		}
	    else if ( strncasecmp( buf, "User-Agent:", 11 ) == 0 )
		{
		cp = &buf[11];
		cp += strspn( cp, " \t" );
		hc->useragent = cp;
		}
	    else if ( strncasecmp( buf, "Host:", 5 ) == 0 )
		{
		cp = &buf[5];
		cp += strspn( cp, " \t" );
		hc->hdrhost = cp;
		cp = strchr( hc->hdrhost, ':' );
		if ( cp != (char*) 0 )
		    *cp = '\0';
		if ( strchr( hc->hdrhost, '/' ) != (char*) 0 || hc->hdrhost[0] == '.' )
		    {
		    httpd_send_err( hc, 400, httpd_err400title, "", httpd_err400form, "" );
		    return -1;
		    }
		}
	    else if ( strncasecmp( buf, "Accept:", 7 ) == 0 )
		{
		cp = &buf[7];
		cp += strspn( cp, " \t" );
		if ( hc->accept[0] != '\0' )
		    {
		    if ( strlen( hc->accept ) > 5000 )
			{
			syslog(
			    LOG_ERR, "%.80s way too much Accept: data",
			    httpd_ntoa( &hc->client_addr ) );
			continue;
			}
		    httpd_realloc_str(
			&hc->accept, &hc->maxaccept,
			strlen( hc->accept ) + 2 + strlen( cp ) );
		    (void) strcat( hc->accept, ", " );
		    }
		else
		    httpd_realloc_str(
			&hc->accept, &hc->maxaccept, strlen( cp ) );
		(void) strcat( hc->accept, cp );
		}
	    else if ( strncasecmp( buf, "Accept-Encoding:", 16 ) == 0 )
		{
		cp = &buf[16];
		cp += strspn( cp, " \t" );
		if ( hc->accepte[0] != '\0' )
		    {
		    if ( strlen( hc->accepte ) > 5000 )
			{
			syslog(
			    LOG_ERR, "%.80s way too much Accept-Encoding: data",
			    httpd_ntoa( &hc->client_addr ) );
			continue;
			}
		    httpd_realloc_str(
			&hc->accepte, &hc->maxaccepte,
			strlen( hc->accepte ) + 2 + strlen( cp ) );
		    (void) strcat( hc->accepte, ", " );
		    }
		else
		    httpd_realloc_str(
			&hc->accepte, &hc->maxaccepte, strlen( cp ) );
		(void) strcpy( hc->accepte, cp );
		}
	    else if ( strncasecmp( buf, "Accept-Language:", 16 ) == 0 )
		{
		cp = &buf[16];
		cp += strspn( cp, " \t" );
		hc->acceptl = cp;
		}
	    else if ( strncasecmp( buf, "If-Modified-Since:", 18 ) == 0 )
		{
		cp = &buf[18];
		hc->if_modified_since = tdate_parse( cp );
		if ( hc->if_modified_since == (time_t) -1 )
		    syslog( LOG_DEBUG, "unparsable time: %.80s", cp );
		}
	    else if ( strncasecmp( buf, "Cookie:", 7 ) == 0 )
		{
		cp = &buf[7];
		cp += strspn( cp, " \t" );
		hc->cookie = cp;
		}
	    else if ( strncasecmp( buf, "Range:", 6 ) == 0 )
		{
		/* Only support %d- and %d-%d, not %d-%d,%d-%d or -%d. */
		if ( strchr( buf, ',' ) == (char*) 0 )
		    {
		    char* cp_dash;
		    cp = strpbrk( buf, "=" );
		    if ( cp != (char*) 0 )
			{
			cp_dash = strchr( cp + 1, '-' );
			if ( cp_dash != (char*) 0 && cp_dash != cp + 1 )
			    {
			    *cp_dash = '\0';
			    hc->got_range = 1;
			    hc->first_byte_index = atoll( cp + 1 );
			    if ( hc->first_byte_index < 0 )
				hc->first_byte_index = 0;
			    if ( isdigit( (int) cp_dash[1] ) )
				{
				hc->last_byte_index = atoll( cp_dash + 1 );
				if ( hc->last_byte_index < 0 )
				    hc->last_byte_index = -1;
				}
			    }
			}
		    }
		}
	    else if ( strncasecmp( buf, "Range-If:", 9 ) == 0 ||
		      strncasecmp( buf, "If-Range:", 9 ) == 0 )
		{
		cp = &buf[9];
		hc->range_if = tdate_parse( cp );
		if ( hc->range_if == (time_t) -1 )
		    syslog( LOG_DEBUG, "unparsable time: %.80s", cp );
		}
	    else if ( strncasecmp( buf, "Content-Type:", 13 ) == 0 )
		{
		cp = &buf[13];
		cp += strspn( cp, " \t" );
		hc->contenttype = cp;
		}
	    else if ( strncasecmp( buf, "Content-Length:", 15 ) == 0 )
		{
		cp = &buf[15];
		hc->contentlength = atol( cp );
		}
	    else if ( strncasecmp( buf, "Authorization:", 14 ) == 0 )
		{
		cp = &buf[14];
		cp += strspn( cp, " \t" );
		hc->authorization = cp;
		}
	    else if ( strncasecmp( buf, "Connection:", 11 ) == 0 )
		{
		cp = &buf[11];
		cp += strspn( cp, " \t" );
		if ( strcasecmp( cp, "keep-alive" ) == 0 )
		    hc->keep_alive = 1;
		}
#ifdef LOG_UNKNOWN_HEADERS
	    else if ( strncasecmp( buf, "Accept-Charset:", 15 ) == 0 ||
		      strncasecmp( buf, "Accept-Language:", 16 ) == 0 ||
		      strncasecmp( buf, "Agent:", 6 ) == 0 ||
		      strncasecmp( buf, "Cache-Control:", 14 ) == 0 ||
		      strncasecmp( buf, "Cache-Info:", 11 ) == 0 ||
		      strncasecmp( buf, "Charge-To:", 10 ) == 0 ||
		      strncasecmp( buf, "Client-IP:", 10 ) == 0 ||
		      strncasecmp( buf, "Date:", 5 ) == 0 ||
		      strncasecmp( buf, "Extension:", 10 ) == 0 ||
		      strncasecmp( buf, "Forwarded:", 10 ) == 0 ||
		      strncasecmp( buf, "From:", 5 ) == 0 ||
		      strncasecmp( buf, "HTTP-Version:", 13 ) == 0 ||
		      strncasecmp( buf, "Max-Forwards:", 13 ) == 0 ||
		      strncasecmp( buf, "Message-Id:", 11 ) == 0 ||
		      strncasecmp( buf, "MIME-Version:", 13 ) == 0 ||
		      strncasecmp( buf, "Negotiate:", 10 ) == 0 ||
		      strncasecmp( buf, "Pragma:", 7 ) == 0 ||
		      strncasecmp( buf, "Proxy-Agent:", 12 ) == 0 ||
		      strncasecmp( buf, "Proxy-Connection:", 17 ) == 0 ||
		      strncasecmp( buf, "Security-Scheme:", 16 ) == 0 ||
		      strncasecmp( buf, "Session-Id:", 11 ) == 0 ||
		      strncasecmp( buf, "UA-Color:", 9 ) == 0 ||
		      strncasecmp( buf, "UA-CPU:", 7 ) == 0 ||
		      strncasecmp( buf, "UA-Disp:", 8 ) == 0 ||
		      strncasecmp( buf, "UA-OS:", 6 ) == 0 ||
		      strncasecmp( buf, "UA-Pixels:", 10 ) == 0 ||
		      strncasecmp( buf, "User:", 5 ) == 0 ||
		      strncasecmp( buf, "Via:", 4 ) == 0 ||
		      strncasecmp( buf, "X-", 2 ) == 0 )
		; /* ignore */
	    else
		syslog( LOG_DEBUG, "unknown request header: %.80s", buf );
#endif /* LOG_UNKNOWN_HEADERS */
	    }
	}

    if ( hc->one_one )
	{
	/* Check that HTTP/1.1 requests specify a host, as required. */
	if ( hc->reqhost[0] == '\0' && hc->hdrhost[0] == '\0' )
	    {
	    httpd_send_err( hc, 400, httpd_err400title, "", httpd_err400form, "" );
	    return -1;
	    }

	/* If the client wants to do keep-alives, it might also be doing
	** pipelining.  There's no way for us to tell.  Since we don't
	** implement keep-alives yet, if we close such a connection there
	** might be unread pipelined requests waiting.  So, we have to
	** do a lingering close.
	*/
	if ( hc->keep_alive )
	    hc->should_linger = 1;
	}

    /* Ok, the request has been parsed.  Now we resolve stuff that
    ** may require the entire request.
    */

    /* Copy original filename to expanded filename. */
    httpd_realloc_str(
	&hc->expnfilename, &hc->maxexpnfilename, strlen( hc->origfilename ) );
    (void) strcpy( hc->expnfilename, hc->origfilename );

    /* Tilde mapping. */
    if ( hc->expnfilename[0] == '~' )
	{
#ifdef TILDE_MAP_1
	if ( ! tilde_map_1( hc ) )
	    {
	    httpd_send_err( hc, 404, err404title, "", err404form, hc->encodedurl );
	    return -1;
	    }
#endif /* TILDE_MAP_1 */
#ifdef TILDE_MAP_2
	if ( ! tilde_map_2( hc ) )
	    {
	    httpd_send_err( hc, 404, err404title, "", err404form, hc->encodedurl );
	    return -1;
	    }
#endif /* TILDE_MAP_2 */
	}

    /* Virtual host mapping. */
    if ( hc->hs->vhost )
	if ( ! vhost_map( hc ) )
	    {
	    httpd_send_err( hc, 500, err500title, "", err500form, hc->encodedurl );
	    return -1;
	    }

    /* Expand all symbolic links in the filename.  This also gives us
    ** any trailing non-existing components, for pathinfo.
    */
    cp = expand_symlinks( hc->expnfilename, &pi, hc->hs->no_symlink_check, hc->tildemapped );
    if ( cp == (char*) 0 )
	{
	httpd_send_err( hc, 500, err500title, "", err500form, hc->encodedurl );
	return -1;
	}
    httpd_realloc_str( &hc->expnfilename, &hc->maxexpnfilename, strlen( cp ) );
    (void) strcpy( hc->expnfilename, cp );
    httpd_realloc_str( &hc->pathinfo, &hc->maxpathinfo, strlen( pi ) );
    (void) strcpy( hc->pathinfo, pi );

    /* Remove pathinfo stuff from the original filename too. */
    if ( hc->pathinfo[0] != '\0' )
	{
	int i;
	i = strlen( hc->origfilename ) - strlen( hc->pathinfo );
	if ( i > 0 && strcmp( &hc->origfilename[i], hc->pathinfo ) == 0 )
	    hc->origfilename[i - 1] = '\0';
	}

    /* If the expanded filename is an absolute path, check that it's still
    ** within the current directory or the alternate directory.
    */
    if ( hc->expnfilename[0] == '/' )
	{
	if ( strncmp(
		 hc->expnfilename, hc->hs->cwd, strlen( hc->hs->cwd ) ) == 0 )
	    {
	    /* Elide the current directory. */
	    (void) ol_strcpy(
		hc->expnfilename, &hc->expnfilename[strlen( hc->hs->cwd )] );
	    }
#ifdef TILDE_MAP_2
	else if ( hc->altdir[0] != '\0' &&
		  ( strncmp(
		       hc->expnfilename, hc->altdir,
		       strlen( hc->altdir ) ) == 0 &&
		    ( hc->expnfilename[strlen( hc->altdir )] == '\0' ||
		      hc->expnfilename[strlen( hc->altdir )] == '/' ) ) )
	    {}
#endif /* TILDE_MAP_2 */
	else
	    {
	    syslog(
		LOG_NOTICE, "%.80s URL \"%.80s\" goes outside the web tree",
		httpd_ntoa( &hc->client_addr ), hc->encodedurl );
	    httpd_send_err(
		hc, 403, err403title, "",
		ERROR_FORM( err403form, "The requested URL '%.80s' resolves to a file outside the permitted web server directory tree.\n" ),
		hc->encodedurl );
	    return -1;
	    }
	}

    return 0;
    }


static char*
bufgets( httpd_conn* hc )
    {
    int i;
    char c;

    for ( i = hc->checked_idx; hc->checked_idx < hc->read_idx; ++hc->checked_idx )
	{
	c = hc->read_buf[hc->checked_idx];
	if ( c == '\012' || c == '\015' )
	    {
	    hc->read_buf[hc->checked_idx] = '\0';
	    ++hc->checked_idx;
	    if ( c == '\015' && hc->checked_idx < hc->read_idx &&
		 hc->read_buf[hc->checked_idx] == '\012' )
		{
		hc->read_buf[hc->checked_idx] = '\0';
		++hc->checked_idx;
		}
	    return &(hc->read_buf[i]);
	    }
	}
    return (char*) 0;
    }


static void
de_dotdot( char* file )
    {
    char* cp;
    char* cp2;
    int l;

    /* Collapse any multiple / sequences. */
    while ( ( cp = strstr( file, "//") ) != (char*) 0 )
	{
	for ( cp2 = cp + 2; *cp2 == '/'; ++cp2 )
	    continue;
	(void) ol_strcpy( cp + 1, cp2 );
	}

    /* Remove leading ./ and any /./ sequences. */
    while ( strncmp( file, "./", 2 ) == 0 )
	(void) ol_strcpy( file, file + 2 );
    while ( ( cp = strstr( file, "/./") ) != (char*) 0 )
	(void) ol_strcpy( cp, cp + 2 );

    /* Alternate between removing leading ../ and removing xxx/../ */
    for (;;)
	{
	while ( strncmp( file, "../", 3 ) == 0 )
	    (void) ol_strcpy( file, file + 3 );
	cp = strstr( file, "/../" );
	if ( cp == (char*) 0 )
	    break;
	for ( cp2 = cp - 1; cp2 >= file && *cp2 != '/'; --cp2 )
	    continue;
	(void) ol_strcpy( cp2 + 1, cp + 4 );
	}

    /* Also elide any xxx/.. at the end. */
    while ( ( l = strlen( file ) ) > 3 &&
	    strcmp( ( cp = file + l - 3 ), "/.." ) == 0 )
	{
	for ( cp2 = cp - 1; cp2 >= file && *cp2 != '/'; --cp2 )
	    continue;
	if ( cp2 < file )
	    break;
	*cp2 = '\0';
	}
    }


void
httpd_close_conn( httpd_conn* hc, struct timeval* nowP )
    {
    make_log_entry( hc, nowP );

    if ( hc->file_address != (char*) 0 )
	{
	mmc_unmap( hc->file_address, &(hc->sb), nowP );
	hc->file_address = (char*) 0;
	}
    if ( hc->conn_fd >= 0 )
	{
	(void) close( hc->conn_fd );
	hc->conn_fd = -1;
	}
    }

void
httpd_destroy_conn( httpd_conn* hc )
    {
    if ( hc->initialized )
	{
	free( (void*) hc->read_buf );
	free( (void*) hc->decodedurl );
	free( (void*) hc->origfilename );
	free( (void*) hc->expnfilename );
	free( (void*) hc->encodings );
	free( (void*) hc->pathinfo );
	free( (void*) hc->query );
	free( (void*) hc->accept );
	free( (void*) hc->accepte );
	free( (void*) hc->reqhost );
	free( (void*) hc->hostdir );
	free( (void*) hc->remoteuser );
	free( (void*) hc->response );
#ifdef TILDE_MAP_2
	free( (void*) hc->altdir );
#endif /* TILDE_MAP_2 */
	hc->initialized = 0;
	}
    }


struct mime_entry {
    char* ext;
    size_t ext_len;
    char* val;
    size_t val_len;
    };
static struct mime_entry enc_tab[] = {
#include "mime_encodings.h"
    };
static const int n_enc_tab = sizeof(enc_tab) / sizeof(*enc_tab);
static struct mime_entry typ_tab[] = {
#include "mime_types.h"
    };
static const int n_typ_tab = sizeof(typ_tab) / sizeof(*typ_tab);


/* qsort comparison routine */
static int
ext_compare( const void* v1, const void* v2 )
    {
    const struct mime_entry* m1 = (const struct mime_entry*) v1;
    const struct mime_entry* m2 = (const struct mime_entry*) v2;

    return strcmp( m1->ext, m2->ext );
    }


static void
init_mime( void )
    {
    int i;

    /* Sort the tables so we can do binary search. */
    qsort( enc_tab, n_enc_tab, sizeof(*enc_tab), ext_compare );
    qsort( typ_tab, n_typ_tab, sizeof(*typ_tab), ext_compare );

    /* Fill in the lengths. */
    for ( i = 0; i < n_enc_tab; ++i )
	{
	enc_tab[i].ext_len = strlen( enc_tab[i].ext );
	enc_tab[i].val_len = strlen( enc_tab[i].val );
	}
    for ( i = 0; i < n_typ_tab; ++i )
	{
	typ_tab[i].ext_len = strlen( typ_tab[i].ext );
	typ_tab[i].val_len = strlen( typ_tab[i].val );
	}

    }


/* Figure out MIME encodings and type based on the filename.  Multiple
** encodings are separated by commas, and are listed in the order in
** which they were applied to the file.
*/
static void
figure_mime( httpd_conn* hc )
    {
    char* prev_dot;
    char* dot;
    char* ext;
    int me_indexes[100], n_me_indexes;
    size_t ext_len, encodings_len;
    int i, top, bot, mid;
    int r;
    char* default_type = "text/plain; charset=%s";

    /* Peel off encoding extensions until there aren't any more. */
    n_me_indexes = 0;
    for ( prev_dot = &hc->expnfilename[strlen(hc->expnfilename)]; ; prev_dot = dot )
	{
	for ( dot = prev_dot - 1; dot >= hc->expnfilename && *dot != '.'; --dot )
	    ;
	if ( dot < hc->expnfilename )
	    {
	    /* No dot found.  No more encoding extensions, and no type
	    ** extension either.
	    */
	    hc->type = default_type;
	    goto done;
	    }
	ext = dot + 1;
	ext_len = prev_dot - ext;
	/* Search the encodings table.  Linear search is fine here, there
	** are only a few entries.
	*/
	for ( i = 0; i < n_enc_tab; ++i )
	    {
	    if ( ext_len == enc_tab[i].ext_len && strncasecmp( ext, enc_tab[i].ext, ext_len ) == 0 )
		{
		if ( n_me_indexes < sizeof(me_indexes)/sizeof(*me_indexes) )
		    {
		    me_indexes[n_me_indexes] = i;
		    ++n_me_indexes;
		    }
		goto next;
		}
	    }
	/* No encoding extension found.  Break and look for a type extension. */
	break;

	next: ;
	}

    /* Binary search for a matching type extension. */
    top = n_typ_tab - 1;
    bot = 0;
    while ( top >= bot )
	{
	mid = ( top + bot ) / 2;
	r = strncasecmp( ext, typ_tab[mid].ext, ext_len );
	if ( r < 0 )
	    top = mid - 1;
	else if ( r > 0 )
	    bot = mid + 1;
	else
	    if ( ext_len < typ_tab[mid].ext_len )
		top = mid - 1;
	    else if ( ext_len > typ_tab[mid].ext_len )
		bot = mid + 1;
	    else
		{
		hc->type = typ_tab[mid].val;
		goto done;
		}
	}
    hc->type = default_type;

    done:

    /* The last thing we do is actually generate the mime-encoding header. */
    hc->encodings[0] = '\0';
    encodings_len = 0;
    for ( i = n_me_indexes - 1; i >= 0; --i )
	{
	httpd_realloc_str(
	    &hc->encodings, &hc->maxencodings,
	    encodings_len + enc_tab[me_indexes[i]].val_len + 1 );
	if ( hc->encodings[0] != '\0' )
	    {
	    (void) strcpy( &hc->encodings[encodings_len], "," );
	    ++encodings_len;
	    }
	(void) strcpy( &hc->encodings[encodings_len], enc_tab[me_indexes[i]].val );
	encodings_len += enc_tab[me_indexes[i]].val_len;
	}

    }


#ifdef CGI_TIMELIMIT
static void
cgi_kill2( ClientData client_data, struct timeval* nowP )
    {
    pid_t pid;

    pid = (pid_t) client_data.i;
    if ( kill( pid, SIGKILL ) == 0 )
	syslog( LOG_WARNING, "hard-killed CGI process %d", pid );
    }

static void
cgi_kill( ClientData client_data, struct timeval* nowP )
    {
    pid_t pid;

    pid = (pid_t) client_data.i;
    if ( kill( pid, SIGINT ) == 0 )
	{
	syslog( LOG_WARNING, "killed CGI process %d", pid );
	/* In case this isn't enough, schedule an uncatchable kill. */
	if ( tmr_create( nowP, cgi_kill2, client_data, 5 * 1000L, 0 ) == (Timer*) 0 )
	    {
	    syslog( LOG_CRIT, "tmr_create(cgi_kill2) failed" );
	    exit( 1 );
	    }
	}
    }
#endif /* CGI_TIMELIMIT */


#ifdef GENERATE_INDEXES

/* qsort comparison routine */
static int
name_compare( const void* v1, const void* v2 )
    {
    const char** c1 = (const char**) v1;
    const char** c2 = (const char**) v2;
    return strcmp( *c1, *c2 );
    }


static int
ls( httpd_conn* hc )
    {
    DIR* dirp;
    struct dirent* de;
    int namlen;
    static int maxnames = 0;
    int nnames;
    static char* names;
    static char** nameptrs;
    static char* name;
    static size_t maxname = 0;
    static char* rname;
    static size_t maxrname = 0;
    static char* encrname;
    static size_t maxencrname = 0;
    FILE* fp;
    int i, r;
    struct stat sb;
    struct stat lsb;
    char modestr[20];
    char* linkprefix;
    char lnk[MAXPATHLEN+1];
    int linklen;
    char* fileclass;
    time_t now;
    char* timestr;
    ClientData client_data;

    dirp = opendir( hc->expnfilename );
    if ( dirp == (DIR*) 0 )
	{
	syslog( LOG_ERR, "opendir %.80s - %m", hc->expnfilename );
	httpd_send_err( hc, 404, err404title, "", err404form, hc->encodedurl );
	return -1;
	}

    if ( hc->method == METHOD_HEAD )
	{
	closedir( dirp );
	send_mime(
	    hc, 200, ok200title, "", "", "text/html; charset=%s", (off_t) -1,
	    hc->sb.st_mtime );
	}
    else if ( hc->method == METHOD_GET )
	{
	if ( hc->hs->cgi_limit != 0 && hc->hs->cgi_count >= hc->hs->cgi_limit )
	    {
	    closedir( dirp );
	    httpd_send_err(
		hc, 503, httpd_err503title, "", httpd_err503form,
		hc->encodedurl );
	    return -1;
	    }
	++hc->hs->cgi_count;
	r = fork( );
	if ( r < 0 )
	    {
	    syslog( LOG_ERR, "fork - %m" );
	    closedir( dirp );
	    httpd_send_err(
		hc, 500, err500title, "", err500form, hc->encodedurl );
	    return -1;
	    }
	if ( r == 0 )
	    {
	    /* Child process. */
	    sub_process = 1;
	    httpd_unlisten( hc->hs );
	    send_mime(
		hc, 200, ok200title, "", "", "text/html; charset=%s",
		(off_t) -1, hc->sb.st_mtime );
	    httpd_write_response( hc );

#ifdef CGI_NICE
	    /* Set priority. */
	    (void) nice( CGI_NICE );
#endif /* CGI_NICE */

	    /* Open a stdio stream so that we can use fprintf, which is more
	    ** efficient than a bunch of separate write()s.  We don't have
	    ** to worry about double closes or file descriptor leaks cause
	    ** we're in a subprocess.
	    */
	    fp = fdopen( hc->conn_fd, "w" );
	    if ( fp == (FILE*) 0 )
		{
		syslog( LOG_ERR, "fdopen - %m" );
		httpd_send_err(
		    hc, 500, err500title, "", err500form, hc->encodedurl );
		httpd_write_response( hc );
		closedir( dirp );
		exit( 1 );
		}

	    (void) fprintf( fp, "\
<!DOCTYPE html PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\" \"http://www.w3.org/TR/html4/loose.dtd\">\n\
\n\
<html>\n\
\n\
  <head>\n\
    <meta http-equiv=\"Content-type\" content=\"text/html;charset=UTF-8\">\n\
    <title>Index of %.80s</title>\n\
  </head>\n\
\n\
  <body bgcolor=\"#99cc99\" text=\"#000000\" link=\"#2020ff\" vlink=\"#4040cc\">\n\
\n\
    <h2>Index of %.80s</h2>\n\
\n\
    <pre>\n\
mode  links    bytes  last-changed  name\n\
    <hr>",
		hc->encodedurl, hc->encodedurl );

	    /* Read in names. */
	    nnames = 0;
	    while ( ( de = readdir( dirp ) ) != 0 )     /* dirent or direct */
		{
		if ( nnames >= maxnames )
		    {
		    if ( maxnames == 0 )
			{
			maxnames = 100;
			names = NEW( char, maxnames * ( MAXPATHLEN + 1 ) );
			nameptrs = NEW( char*, maxnames );
			}
		    else
			{
			maxnames *= 2;
			names = RENEW( names, char, maxnames * ( MAXPATHLEN + 1 ) );
			nameptrs = RENEW( nameptrs, char*, maxnames );
			}
		    if ( names == (char*) 0 || nameptrs == (char**) 0 )
			{
			syslog( LOG_ERR, "out of memory reallocating directory names" );
			exit( 1 );
			}
		    for ( i = 0; i < maxnames; ++i )
			nameptrs[i] = &names[i * ( MAXPATHLEN + 1 )];
		    }
		namlen = NAMLEN(de);
		(void) strncpy( nameptrs[nnames], de->d_name, namlen );
		nameptrs[nnames][namlen] = '\0';
		++nnames;
		}
	    closedir( dirp );

	    /* Sort the names. */
	    qsort( nameptrs, nnames, sizeof(*nameptrs), name_compare );

	    /* Generate output. */
	    for ( i = 0; i < nnames; ++i )
		{
		httpd_realloc_str(
		    &name, &maxname,
		    strlen( hc->expnfilename ) + 1 + strlen( nameptrs[i] ) );
		httpd_realloc_str(
		    &rname, &maxrname,
		    strlen( hc->origfilename ) + 1 + strlen( nameptrs[i] ) );
		if ( hc->expnfilename[0] == '\0' ||
		     strcmp( hc->expnfilename, "." ) == 0 )
		    {
		    (void) strcpy( name, nameptrs[i] );
		    (void) strcpy( rname, nameptrs[i] );
		    }
		else
		    {
		    (void) my_snprintf( name, maxname,
			"%s/%s", hc->expnfilename, nameptrs[i] );
		    if ( strcmp( hc->origfilename, "." ) == 0 )
			(void) my_snprintf( rname, maxrname,
			    "%s", nameptrs[i] );
		    else
			(void) my_snprintf( rname, maxrname,
			    "%s%s", hc->origfilename, nameptrs[i] );
		    }
		httpd_realloc_str(
		    &encrname, &maxencrname, 3 * strlen( rname ) + 1 );
		strencode( encrname, maxencrname, rname );

		if ( stat( name, &sb ) < 0 || lstat( name, &lsb ) < 0 )
		    continue;

		linkprefix = "";
		lnk[0] = '\0';
		/* Break down mode word.  First the file type. */
		switch ( lsb.st_mode & S_IFMT )
		    {
		    case S_IFIFO:  modestr[0] = 'p'; break;
		    case S_IFCHR:  modestr[0] = 'c'; break;
		    case S_IFDIR:  modestr[0] = 'd'; break;
		    case S_IFBLK:  modestr[0] = 'b'; break;
		    case S_IFREG:  modestr[0] = '-'; break;
		    case S_IFSOCK: modestr[0] = 's'; break;
		    case S_IFLNK:  modestr[0] = 'l';
		    linklen = readlink( name, lnk, sizeof(lnk) - 1 );
		    if ( linklen != -1 )
			{
			lnk[linklen] = '\0';
			linkprefix = " -&gt; ";
			}
		    break;
		    default:       modestr[0] = '?'; break;
		    }
		/* Now the world permissions.  Owner and group permissions
		** are not of interest to web clients.
		*/
		modestr[1] = ( lsb.st_mode & S_IROTH ) ? 'r' : '-';
		modestr[2] = ( lsb.st_mode & S_IWOTH ) ? 'w' : '-';
		modestr[3] = ( lsb.st_mode & S_IXOTH ) ? 'x' : '-';
		modestr[4] = '\0';

		/* We also leave out the owner and group name, they are
		** also not of interest to web clients.  Plus if we're
		** running under chroot(), they would require a copy
		** of /etc/passwd and /etc/group, which we want to avoid.
		*/

		/* Get time string. */
		now = time( (time_t*) 0 );
		timestr = ctime( &lsb.st_mtime );
		timestr[ 0] = timestr[ 4];
		timestr[ 1] = timestr[ 5];
		timestr[ 2] = timestr[ 6];
		timestr[ 3] = ' ';
		timestr[ 4] = timestr[ 8];
		timestr[ 5] = timestr[ 9];
		timestr[ 6] = ' ';
		if ( now - lsb.st_mtime > 60*60*24*182 )        /* 1/2 year */
		    {
		    timestr[ 7] = ' ';
		    timestr[ 8] = timestr[20];
		    timestr[ 9] = timestr[21];
		    timestr[10] = timestr[22];
		    timestr[11] = timestr[23];
		    }
		else
		    {
		    timestr[ 7] = timestr[11];
		    timestr[ 8] = timestr[12];
		    timestr[ 9] = ':';
		    timestr[10] = timestr[14];
		    timestr[11] = timestr[15];
		    }
		timestr[12] = '\0';

		/* The ls -F file class. */
		switch ( sb.st_mode & S_IFMT )
		    {
		    case S_IFDIR:  fileclass = "/"; break;
		    case S_IFSOCK: fileclass = "="; break;
		    case S_IFLNK:  fileclass = "@"; break;
		    default:
		    fileclass = ( sb.st_mode & S_IXOTH ) ? "*" : "";
		    break;
		    }

		/* And print. */
		(void)  fprintf( fp,
		   "%s %3ld  %10lld  %s  <a href=\"/%.500s%s\">%.80s</a>%s%s%s\n",
		    modestr, (long) lsb.st_nlink, (long long) lsb.st_size,
		    timestr, encrname, S_ISDIR(sb.st_mode) ? "/" : "",
		    nameptrs[i], linkprefix, lnk, fileclass );
		}

	    (void) fprintf( fp, "    </pre>\n  </body>\n</html>\n" );
	    (void) fclose( fp );
	    exit( 0 );
	    }

	/* Parent process. */
	closedir( dirp );
	syslog( LOG_DEBUG, "spawned indexing process %d for directory '%.200s'", r, hc->expnfilename );
#ifdef CGI_TIMELIMIT
	/* Schedule a kill for the child process, in case it runs too long */
	client_data.i = r;
	if ( tmr_create( (struct timeval*) 0, cgi_kill, client_data, CGI_TIMELIMIT * 1000L, 0 ) == (Timer*) 0 )
	    {
	    syslog( LOG_CRIT, "tmr_create(cgi_kill ls) failed" );
	    exit( 1 );
	    }
#endif /* CGI_TIMELIMIT */
	hc->status = 200;
	hc->bytes_sent = CGI_BYTECOUNT;
	hc->should_linger = 0;
	}
    else
	{
	closedir( dirp );
	httpd_send_err(
	    hc, 501, err501title, "", err501form, httpd_method_str( hc->method ) );
	return -1;
	}

    return 0;
    }

#endif /* GENERATE_INDEXES */


static char*
build_env( char* fmt, char* arg )
    {
    char* cp;
    size_t size;
    static char* buf;
    static size_t maxbuf = 0;

    size = strlen( fmt ) + strlen( arg );
    if ( size > maxbuf )
	httpd_realloc_str( &buf, &maxbuf, size );
    (void) my_snprintf( buf, maxbuf, fmt, arg );
    cp = strdup( buf );
    if ( cp == (char*) 0 )
	{
	syslog( LOG_ERR, "out of memory copying environment variable" );
	exit( 1 );
	}
    return cp;
    }


#ifdef SERVER_NAME_LIST
static char*
hostname_map( char* hostname )
    {
    int len, n;
    static char* list[] = { SERVER_NAME_LIST };

    len = strlen( hostname );
    for ( n = sizeof(list) / sizeof(*list) - 1; n >= 0; --n )
	if ( strncasecmp( hostname, list[n], len ) == 0 )
	    if ( list[n][len] == '/' )  /* check in case of a substring match */
		return &list[n][len + 1];
    return (char*) 0;
    }
#endif /* SERVER_NAME_LIST */


/* Set up environment variables. Be real careful here to avoid
** letting malicious clients overrun a buffer.  We don't have
** to worry about freeing stuff since we're a sub-process.
*/
static char**
make_envp( httpd_conn* hc )
    {
    static char* envp[50];
    int envn;
    char* cp;
    char buf[256];

    envn = 0;
    envp[envn++] = build_env( "PATH=%s", CGI_PATH );
#ifdef CGI_LD_LIBRARY_PATH
    envp[envn++] = build_env( "LD_LIBRARY_PATH=%s", CGI_LD_LIBRARY_PATH );
#endif /* CGI_LD_LIBRARY_PATH */
    envp[envn++] = build_env( "SERVER_SOFTWARE=%s", SERVER_SOFTWARE );
    if ( hc->hs->vhost && hc->hostname != (char*) 0 && hc->hostname[0] != '\0' )
	cp = hc->hostname;
    else if ( hc->hdrhost != (char*) 0 && hc->hdrhost[0] != '\0' )
	cp = hc->hdrhost;
    else if ( hc->reqhost != (char*) 0 && hc->reqhost[0] != '\0' )
	cp = hc->reqhost;
    else
	cp = hc->hs->server_hostname;
    if ( cp != (char*) 0 )
	envp[envn++] = build_env( "SERVER_NAME=%s", cp );
    envp[envn++] = "GATEWAY_INTERFACE=CGI/1.1";
    envp[envn++] = build_env("SERVER_PROTOCOL=%s", hc->protocol);
    (void) my_snprintf( buf, sizeof(buf), "%d", (int) hc->hs->port );
    envp[envn++] = build_env( "SERVER_PORT=%s", buf );
    envp[envn++] = build_env(
	"REQUEST_METHOD=%s", httpd_method_str( hc->method ) );
    if ( hc->pathinfo[0] != '\0' )
	{
	char* cp2;
	size_t l;
	envp[envn++] = build_env( "PATH_INFO=/%s", hc->pathinfo );
	l = strlen( hc->hs->cwd ) + strlen( hc->pathinfo ) + 1;
	cp2 = NEW( char, l );
	if ( cp2 != (char*) 0 )
	    {
	    (void) my_snprintf( cp2, l, "%s%s", hc->hs->cwd, hc->pathinfo );
	    envp[envn++] = build_env( "PATH_TRANSLATED=%s", cp2 );
	    }
	}
    envp[envn++] = build_env(
	"SCRIPT_NAME=/%s", strcmp( hc->origfilename, "." ) == 0 ?
	"" : hc->origfilename );
    if ( hc->query[0] != '\0')
	envp[envn++] = build_env( "QUERY_STRING=%s", hc->query );
    envp[envn++] = build_env(
	"REMOTE_ADDR=%s", httpd_ntoa( &hc->client_addr ) );
    if ( hc->referrer[0] != '\0' )
	{
	envp[envn++] = build_env( "HTTP_REFERER=%s", hc->referrer );
	envp[envn++] = build_env( "HTTP_REFERRER=%s", hc->referrer );
	}
    if ( hc->useragent[0] != '\0' )
	envp[envn++] = build_env( "HTTP_USER_AGENT=%s", hc->useragent );
    if ( hc->accept[0] != '\0' )
	envp[envn++] = build_env( "HTTP_ACCEPT=%s", hc->accept );
    if ( hc->accepte[0] != '\0' )
	envp[envn++] = build_env( "HTTP_ACCEPT_ENCODING=%s", hc->accepte );
    if ( hc->acceptl[0] != '\0' )
	envp[envn++] = build_env( "HTTP_ACCEPT_LANGUAGE=%s", hc->acceptl );
    if ( hc->cookie[0] != '\0' )
	envp[envn++] = build_env( "HTTP_COOKIE=%s", hc->cookie );
    if ( hc->contenttype[0] != '\0' )
	envp[envn++] = build_env( "CONTENT_TYPE=%s", hc->contenttype );
    if ( hc->hdrhost[0] != '\0' )
	envp[envn++] = build_env( "HTTP_HOST=%s", hc->hdrhost );
    if ( hc->contentlength != -1 )
	{
	(void) my_snprintf(
	    buf, sizeof(buf), "%lu", (unsigned long) hc->contentlength );
	envp[envn++] = build_env( "CONTENT_LENGTH=%s", buf );
	}
    if ( hc->remoteuser[0] != '\0' )
	envp[envn++] = build_env( "REMOTE_USER=%s", hc->remoteuser );
    if ( hc->authorization[0] != '\0' )
	envp[envn++] = build_env( "AUTH_TYPE=%s", "Basic" );
	/* We only support Basic auth at the moment. */
    if ( getenv( "TZ" ) != (char*) 0 )
	envp[envn++] = build_env( "TZ=%s", getenv( "TZ" ) );
    envp[envn++] = build_env( "CGI_PATTERN=%s", hc->hs->cgi_pattern );

    envp[envn] = (char*) 0;
    return envp;
    }


/* Set up argument vector.  Again, we don't have to worry about freeing stuff
** since we're a sub-process.  This gets done after make_envp() because we
** scribble on hc->query.
*/
static char**
make_argp( httpd_conn* hc )
    {
    char** argp;
    int argn;
    char* cp1;
    char* cp2;

    /* By allocating an arg slot for every character in the query, plus
    ** one for the filename and one for the NULL, we are guaranteed to
    ** have enough.  We could actually use strlen/2.
    */
    argp = NEW( char*, strlen( hc->query ) + 2 );
    if ( argp == (char**) 0 )
	return (char**) 0;

    argp[0] = strrchr( hc->expnfilename, '/' );
    if ( argp[0] != (char*) 0 )
	++argp[0];
    else
	argp[0] = hc->expnfilename;

    argn = 1;
    /* According to the CGI spec at http://hoohoo.ncsa.uiuc.edu/cgi/cl.html,
    ** "The server should search the query information for a non-encoded =
    ** character to determine if the command line is to be used, if it finds
    ** one, the command line is not to be used."
    */
    if ( strchr( hc->query, '=' ) == (char*) 0 )
	{
	for ( cp1 = cp2 = hc->query; *cp2 != '\0'; ++cp2 )
	    {
	    if ( *cp2 == '+' )
		{
		*cp2 = '\0';
		strdecode( cp1, cp1 );
		argp[argn++] = cp1;
		cp1 = cp2 + 1;
		}
	    }
	if ( cp2 != cp1 )
	    {
	    strdecode( cp1, cp1 );
	    argp[argn++] = cp1;
	    }
	}

    argp[argn] = (char*) 0;
    return argp;
    }


/* This routine is used only for POST requests.  It reads the data
** from the request and sends it to the child process.  The only reason
** we need to do it this way instead of just letting the child read
** directly is that we have already read part of the data into our
** buffer.
*/
static void
cgi_interpose_input( httpd_conn* hc, int wfd )
    {
    size_t c;
    ssize_t r;
    char buf[1024];

    c = hc->read_idx - hc->checked_idx;
    if ( c > 0 )
	{
	if ( httpd_write_fully( wfd, &(hc->read_buf[hc->checked_idx]), c ) != c )
	    return;
	}
    while ( c < hc->contentlength )
	{
	r = read( hc->conn_fd, buf, MIN( sizeof(buf), hc->contentlength - c ) );
	if ( r < 0 && ( errno == EINTR || errno == EAGAIN ) )
	    {
	    sleep( 1 );
	    continue;
	    }
	if ( r <= 0 )
	    return;
	if ( httpd_write_fully( wfd, buf, r ) != r )
	    return;
	c += r;
	}
    post_post_garbage_hack( hc );
    }


/* Special hack to deal with broken browsers that send a LF or CRLF
** after POST data, causing TCP resets - we just read and discard up
** to 2 bytes.  Unfortunately this doesn't fix the problem for CGIs
** which avoid the interposer process due to their POST data being
** short.  Creating an interposer process for all POST CGIs is
** unacceptably expensive.  The eventual fix will come when interposing
** gets integrated into the main loop as a tasklet instead of a process.
*/
static void
post_post_garbage_hack( httpd_conn* hc )
    {
    char buf[2];

    /* If we are in a sub-process, turn on no-delay mode in case we
    ** previously cleared it.
    */
    if ( sub_process )
	httpd_set_ndelay( hc->conn_fd );
    /* And read up to 2 bytes. */
    (void) read( hc->conn_fd, buf, sizeof(buf) );
    }


/* This routine is used for parsed-header CGIs.  The idea here is that the
** CGI can return special headers such as "Status:" and "Location:" which
** change the return status of the response.  Since the return status has to
** be the very first line written out, we have to accumulate all the headers
** and check for the special ones before writing the status.  Then we write
** out the saved headers and proceed to echo the rest of the response.
*/
static void
cgi_interpose_output( httpd_conn* hc, int rfd )
    {
    int r;
    char buf[1024];
    size_t headers_size, headers_len;
    char* headers;
    char* br;
    int status;
    char* title;
    char* cp;

    /* Make sure the connection is in blocking mode.  It should already
    ** be blocking, but we might as well be sure.
    */
    httpd_clear_ndelay( hc->conn_fd );

    /* Slurp in all headers. */
    headers_size = 0;
    httpd_realloc_str( &headers, &headers_size, 500 );
    headers_len = 0;
    for (;;)
	{
	r = read( rfd, buf, sizeof(buf) );
	if ( r < 0 && ( errno == EINTR || errno == EAGAIN ) )
	    {
	    sleep( 1 );
	    continue;
	    }
	if ( r <= 0 )
	    {
	    br = &(headers[headers_len]);
	    break;
	    }
	httpd_realloc_str( &headers, &headers_size, headers_len + r );
	(void) memmove( &(headers[headers_len]), buf, r );
	headers_len += r;
	headers[headers_len] = '\0';
	if ( ( br = strstr( headers, "\015\012\015\012" ) ) != (char*) 0 ||
	     ( br = strstr( headers, "\012\012" ) ) != (char*) 0 )
	    break;
	}

    /* If there were no headers, bail. */
    if ( headers[0] == '\0' )
	return;

    /* Figure out the status.  Look for a Status: or Location: header;
    ** else if there's an HTTP header line, get it from there; else
    ** default to 200.
    */
    status = 200;
    if ( strncmp( headers, "HTTP/", 5 ) == 0 )
	{
	cp = headers;
	cp += strcspn( cp, " \t" );
	status = atoi( cp );
	}
    if ( ( cp = strstr( headers, "Location:" ) ) != (char*) 0 &&
	 cp < br &&
	 ( cp == headers || *(cp-1) == '\012' ) )
	status = 302;
    if ( ( cp = strstr( headers, "Status:" ) ) != (char*) 0 &&
	 cp < br &&
	 ( cp == headers || *(cp-1) == '\012' ) )
	{
	cp += 7;
	cp += strspn( cp, " \t" );
	status = atoi( cp );
	}

    /* Write the status line. */
    switch ( status )
	{
	case 200: title = ok200title; break;
	case 302: title = err302title; break;
	case 304: title = err304title; break;
	case 400: title = httpd_err400title; break;
#ifdef AUTH_FILE
	case 401: title = err401title; break;
#endif /* AUTH_FILE */
	case 403: title = err403title; break;
	case 404: title = err404title; break;
	case 408: title = httpd_err408title; break;
	case 451: title = err451title; break;
	case 500: title = err500title; break;
	case 501: title = err501title; break;
	case 503: title = httpd_err503title; break;
	default: title = "Something"; break;
	}
    (void) my_snprintf( buf, sizeof(buf), "HTTP/1.0 %d %s\015\012", status, title );
    (void) httpd_write_fully( hc->conn_fd, buf, strlen( buf ) );

    /* Write the saved headers. */
    (void) httpd_write_fully( hc->conn_fd, headers, headers_len );

    /* Echo the rest of the output. */
    for (;;)
	{
	r = read( rfd, buf, sizeof(buf) );
	if ( r < 0 && ( errno == EINTR || errno == EAGAIN ) )
	    {
	    sleep( 1 );
	    continue;
	    }
	if ( r <= 0 )
	    break;
	if ( httpd_write_fully( hc->conn_fd, buf, r ) != r )
	    break;
	}
    shutdown( hc->conn_fd, SHUT_WR );
    }


/* CGI child process. */
static void
cgi_child( httpd_conn* hc )
    {
    int r;
    char** argp;
    char** envp;
    char* binary;
    char* directory;

    /* Unset close-on-exec flag for this socket.  This actually shouldn't
    ** be necessary, according to POSIX a dup()'d file descriptor does
    ** *not* inherit the close-on-exec flag, its flag is always clear.
    ** However, Linux messes this up and does copy the flag to the
    ** dup()'d descriptor, so we have to clear it.  This could be
    ** ifdeffed for Linux only.
    */
    (void) fcntl( hc->conn_fd, F_SETFD, 0 );

    /* Close the syslog descriptor so that the CGI program can't
    ** mess with it.  All other open descriptors should be either
    ** the listen socket(s), sockets from accept(), or the file-logging
    ** fd, and all of those are set to close-on-exec, so we don't
    ** have to close anything else.
    */
    closelog();

    /* If the socket happens to be using one of the stdin/stdout/stderr
    ** descriptors, move it to another descriptor so that the dup2 calls
    ** below don't screw things up.  We arbitrarily pick fd 3 - if there
    ** was already something on it, we clobber it, but that doesn't matter
    ** since at this point the only fd of interest is the connection.
    ** All others will be closed on exec.
    */
    if ( hc->conn_fd == STDIN_FILENO || hc->conn_fd == STDOUT_FILENO || hc->conn_fd == STDERR_FILENO )
	{
	int newfd = dup2( hc->conn_fd, STDERR_FILENO + 1 );
	if ( newfd >= 0 )
	    hc->conn_fd = newfd;
	/* If the dup2 fails, shrug.  We'll just take our chances.
	** Shouldn't happen though.
	*/
	}

    /* Make the environment vector. */
    envp = make_envp( hc );

    /* Make the argument vector. */
    argp = make_argp( hc );

    /* Set up stdin.  For POSTs we may have to set up a pipe from an
    ** interposer process, depending on if we've read some of the data
    ** into our buffer.
    */
    if ( hc->method == METHOD_POST && hc->read_idx > hc->checked_idx )
	{
	int p[2];

	if ( pipe( p ) < 0 )
	    {
	    syslog( LOG_ERR, "pipe - %m" );
	    httpd_send_err( hc, 500, err500title, "", err500form, hc->encodedurl );
	    httpd_write_response( hc );
	    exit( 1 );
	    }
	r = fork( );
	if ( r < 0 )
	    {
	    syslog( LOG_ERR, "fork - %m" );
	    httpd_send_err( hc, 500, err500title, "", err500form, hc->encodedurl );
	    httpd_write_response( hc );
	    exit( 1 );
	    }
	if ( r == 0 )
	    {
	    /* Interposer process. */
	    sub_process = 1;
	    (void) close( p[0] );
	    cgi_interpose_input( hc, p[1] );
	    exit( 0 );
	    }
	/* Need to schedule a kill for process r; but in the main process! */
	(void) close( p[1] );
	if ( p[0] != STDIN_FILENO )
	    {
	    (void) dup2( p[0], STDIN_FILENO );
	    (void) close( p[0] );
	    }
	}
    else
	{
	/* Otherwise, the request socket is stdin. */
	if ( hc->conn_fd != STDIN_FILENO )
	    (void) dup2( hc->conn_fd, STDIN_FILENO );
	}

    /* Set up stdout/stderr.  If we're doing CGI header parsing,
    ** we need an output interposer too.
    */
    if ( strncmp( argp[0], "nph-", 4 ) != 0 && hc->mime_flag )
	{
	int p[2];

	if ( pipe( p ) < 0 )
	    {
	    syslog( LOG_ERR, "pipe - %m" );
	    httpd_send_err( hc, 500, err500title, "", err500form, hc->encodedurl );
	    httpd_write_response( hc );
	    exit( 1 );
	    }
	r = fork( );
	if ( r < 0 )
	    {
	    syslog( LOG_ERR, "fork - %m" );
	    httpd_send_err( hc, 500, err500title, "", err500form, hc->encodedurl );
	    httpd_write_response( hc );
	    exit( 1 );
	    }
	if ( r == 0 )
	    {
	    /* Interposer process. */
	    sub_process = 1;
	    (void) close( p[1] );
	    cgi_interpose_output( hc, p[0] );
	    exit( 0 );
	    }
	/* Need to schedule a kill for process r; but in the main process! */
	(void) close( p[0] );
	if ( p[1] != STDOUT_FILENO )
	    (void) dup2( p[1], STDOUT_FILENO );
	if ( p[1] != STDERR_FILENO )
	    (void) dup2( p[1], STDERR_FILENO );
	if ( p[1] != STDOUT_FILENO && p[1] != STDERR_FILENO )
	    (void) close( p[1] );
	}
    else
	{
	/* Otherwise, the request socket is stdout/stderr. */
	if ( hc->conn_fd != STDOUT_FILENO )
	    (void) dup2( hc->conn_fd, STDOUT_FILENO );
	if ( hc->conn_fd != STDERR_FILENO )
	    (void) dup2( hc->conn_fd, STDERR_FILENO );
	}

    /* At this point we would like to set close-on-exec again for hc->conn_fd
    ** (see previous comments on Linux's broken behavior re: close-on-exec
    ** and dup.)  Unfortunately there seems to be another Linux problem, or
    ** perhaps a different aspect of the same problem - if we do this
    ** close-on-exec in Linux, the socket stays open but stderr gets
    ** closed - the last fd duped from the socket.  What a mess.  So we'll
    ** just leave the socket as is, which under other OSs means an extra
    ** file descriptor gets passed to the child process.  Since the child
    ** probably already has that file open via stdin stdout and/or stderr,
    ** this is not a problem.
    */
    /* (void) fcntl( hc->conn_fd, F_SETFD, 1 ); */

#ifdef CGI_NICE
    /* Set priority. */
    (void) nice( CGI_NICE );
#endif /* CGI_NICE */

    /* Split the program into directory and binary, so we can chdir()
    ** to the program's own directory.  This isn't in the CGI 1.1
    ** spec, but it's what other HTTP servers do.
    */
    directory = strdup( hc->expnfilename );
    if ( directory == (char*) 0 )
	binary = hc->expnfilename;      /* ignore errors */
    else
	{
	binary = strrchr( directory, '/' );
	if ( binary == (char*) 0 )
	    binary = hc->expnfilename;
	else
	    {
	    *binary++ = '\0';
	    (void) chdir( directory );  /* ignore errors */
	    }
	}

    /* Default behavior for SIGPIPE. */
#ifdef HAVE_SIGSET
    (void) sigset( SIGPIPE, SIG_DFL );
#else /* HAVE_SIGSET */
    (void) signal( SIGPIPE, SIG_DFL );
#endif /* HAVE_SIGSET */

    /* Run the program. */
    (void) execve( binary, argp, envp );

    /* Something went wrong. */
    syslog( LOG_ERR, "execve %.80s - %m", hc->expnfilename );
    httpd_send_err( hc, 500, err500title, "", err500form, hc->encodedurl );
    httpd_write_response( hc );
    _exit( 1 );
    }


static int
cgi( httpd_conn* hc )
    {
    int r;
    ClientData client_data;

    if ( hc->hs->cgi_limit != 0 && hc->hs->cgi_count >= hc->hs->cgi_limit )
	{
	httpd_send_err(
	    hc, 503, httpd_err503title, "", httpd_err503form,
	    hc->encodedurl );
	return -1;
	}
    ++hc->hs->cgi_count;
    httpd_clear_ndelay( hc->conn_fd );
    r = fork( );
    if ( r < 0 )
	{
	syslog( LOG_ERR, "fork - %m" );
	httpd_send_err(
	    hc, 500, err500title, "", err500form, hc->encodedurl );
	return -1;
	}
    if ( r == 0 )
	{
	/* Child process. */
	sub_process = 1;
	httpd_unlisten( hc->hs );
	cgi_child( hc );
	}

    /* Parent process. */
    syslog( LOG_DEBUG, "spawned CGI process %d for file '%.200s'", r, hc->expnfilename );
#ifdef CGI_TIMELIMIT
    /* Schedule a kill for the child process, in case it runs too long */
    client_data.i = r;
    if ( tmr_create( (struct timeval*) 0, cgi_kill, client_data, CGI_TIMELIMIT * 1000L, 0 ) == (Timer*) 0 )
	{
	syslog( LOG_CRIT, "tmr_create(cgi_kill child) failed" );
	exit( 1 );
	}
#endif /* CGI_TIMELIMIT */
    hc->status = 200;
    hc->bytes_sent = CGI_BYTECOUNT;
    hc->should_linger = 0;

    return 0;
    }


static int
really_start_request( httpd_conn* hc, struct timeval* nowP )
    {
    static char* indexname;
    static size_t maxindexname = 0;
    static const char* index_names[] = { INDEX_NAMES };
    int i;
#ifdef AUTH_FILE
    static char* dirname;
    static size_t maxdirname = 0;
#endif /* AUTH_FILE */
    size_t expnlen, indxlen;
    char* cp;
    char* pi;

    expnlen = strlen( hc->expnfilename );

    /* Stat the file. */
    if ( stat( hc->expnfilename, &hc->sb ) < 0 )
	{
	httpd_send_err( hc, 500, err500title, "", err500form, hc->encodedurl );
	return -1;
	}

    /* Is it world-readable or world-executable?  We check explicitly instead
    ** of just trying to open it, so that no one ever gets surprised by
    ** a file that's not set world-readable and yet somehow is
    ** readable by the HTTP server and therefore the *whole* world.
    */
    if ( ! ( hc->sb.st_mode & ( S_IROTH | S_IXOTH ) ) )
	{
	syslog(
	    LOG_INFO,
	    "%.80s URL \"%.80s\" resolves to a non world-readable file",
	    httpd_ntoa( &hc->client_addr ), hc->encodedurl );
	httpd_send_err(
	    hc, 403, err403title, "",
	    ERROR_FORM( err403form, "The requested URL '%.80s' resolves to a file that is not world-readable.\n" ),
	    hc->encodedurl );
	return -1;
	}

    /* Is it a directory? */
    if ( S_ISDIR(hc->sb.st_mode) )
	{
	/* If there's pathinfo, it's just a non-existent file. */
	if ( hc->pathinfo[0] != '\0' )
	    {
	    httpd_send_err( hc, 404, err404title, "", err404form, hc->encodedurl );
	    return -1;
	    }

	/* Special handling for directory URLs that don't end in a slash.
	** We send back an explicit redirect with the slash, because
	** otherwise many clients can't build relative URLs properly.
	*/
	if ( strcmp( hc->origfilename, "" ) != 0 &&
	     strcmp( hc->origfilename, "." ) != 0 &&
	     hc->origfilename[strlen( hc->origfilename ) - 1] != '/' )
	    {
	    send_dirredirect( hc );
	    return -1;
	    }

	/* Check for an index file. */
	for ( i = 0; i < sizeof(index_names) / sizeof(char*); ++i )
	    {
	    httpd_realloc_str(
		&indexname, &maxindexname,
		expnlen + 1 + strlen( index_names[i] ) );
	    (void) strcpy( indexname, hc->expnfilename );
	    indxlen = strlen( indexname );
	    if ( indxlen == 0 || indexname[indxlen - 1] != '/' )
		(void) strcat( indexname, "/" );
	    if ( strcmp( indexname, "./" ) == 0 )
		indexname[0] = '\0';
	    (void) strcat( indexname, index_names[i] );
	    if ( stat( indexname, &hc->sb ) >= 0 )
		goto got_one;
	    }

	/* Nope, no index file, so it's an actual directory request. */
#ifdef GENERATE_INDEXES
	/* Directories must be readable for indexing. */
	if ( ! ( hc->sb.st_mode & S_IROTH ) )
	    {
	    syslog(
		LOG_INFO,
		"%.80s URL \"%.80s\" tried to index a directory with indexing disabled",
		httpd_ntoa( &hc->client_addr ), hc->encodedurl );
	    httpd_send_err(
		hc, 403, err403title, "",
		ERROR_FORM( err403form, "The requested URL '%.80s' resolves to a directory that has indexing disabled.\n" ),
		hc->encodedurl );
	    return -1;
	    }
#ifdef AUTH_FILE
	/* Check authorization for this directory. */
	if ( auth_check( hc, hc->expnfilename ) == -1 )
	    return -1;
#endif /* AUTH_FILE */
	/* Referrer check. */
	if ( ! check_referrer( hc ) )
	    return -1;
	/* Ok, generate an index. */
	return ls( hc );
#else /* GENERATE_INDEXES */
	syslog(
	    LOG_INFO, "%.80s URL \"%.80s\" tried to index a directory",
	    httpd_ntoa( &hc->client_addr ), hc->encodedurl );
	httpd_send_err(
	    hc, 403, err403title, "",
	    ERROR_FORM( err403form, "The requested URL '%.80s' is a directory, and directory indexing is disabled on this server.\n" ),
	    hc->encodedurl );
	return -1;
#endif /* GENERATE_INDEXES */

	got_one: ;
	/* Got an index file.  Expand symlinks again.  More pathinfo means
	** something went wrong.
	*/
	cp = expand_symlinks( indexname, &pi, hc->hs->no_symlink_check, hc->tildemapped );
	if ( cp == (char*) 0 || pi[0] != '\0' )
	    {
	    httpd_send_err( hc, 500, err500title, "", err500form, hc->encodedurl );
	    return -1;
	    }
	expnlen = strlen( cp );
	httpd_realloc_str( &hc->expnfilename, &hc->maxexpnfilename, expnlen );
	(void) strcpy( hc->expnfilename, cp );

	/* Now, is the index version world-readable or world-executable? */
	if ( ! ( hc->sb.st_mode & ( S_IROTH | S_IXOTH ) ) )
	    {
	    syslog(
		LOG_INFO,
		"%.80s URL \"%.80s\" resolves to a non-world-readable index file",
		httpd_ntoa( &hc->client_addr ), hc->encodedurl );
	    httpd_send_err(
		hc, 403, err403title, "",
		ERROR_FORM( err403form, "The requested URL '%.80s' resolves to an index file that is not world-readable.\n" ),
		hc->encodedurl );
	    return -1;
	    }
	}

#ifdef AUTH_FILE
    /* Check authorization for this directory. */
    httpd_realloc_str( &dirname, &maxdirname, expnlen );
    (void) strcpy( dirname, hc->expnfilename );
    cp = strrchr( dirname, '/' );
    if ( cp == (char*) 0 )
	(void) strcpy( dirname, "." );
    else
	*cp = '\0';
    if ( auth_check( hc, dirname ) == -1 )
	return -1;

    /* Check if the filename is the AUTH_FILE itself - that's verboten. */
    if ( expnlen == sizeof(AUTH_FILE) - 1 )
	{
	if ( strcmp( hc->expnfilename, AUTH_FILE ) == 0 )
	    {
	    syslog(
		LOG_NOTICE,
		"%.80s URL \"%.80s\" tried to retrieve an auth file",
		httpd_ntoa( &hc->client_addr ), hc->encodedurl );
	    httpd_send_err(
		hc, 403, err403title, "",
		ERROR_FORM( err403form, "The requested URL '%.80s' is an authorization file, retrieving it is not permitted.\n" ),
		hc->encodedurl );
	    return -1;
	    }
	}
    else if ( expnlen >= sizeof(AUTH_FILE) &&
	      strcmp( &(hc->expnfilename[expnlen - sizeof(AUTH_FILE) + 1]), AUTH_FILE ) == 0 &&
	      hc->expnfilename[expnlen - sizeof(AUTH_FILE)] == '/' )
	{
	syslog(
	    LOG_NOTICE,
	    "%.80s URL \"%.80s\" tried to retrieve an auth file",
	    httpd_ntoa( &hc->client_addr ), hc->encodedurl );
	httpd_send_err(
	    hc, 403, err403title, "",
	    ERROR_FORM( err403form, "The requested URL '%.80s' is an authorization file, retrieving it is not permitted.\n" ),
	    hc->encodedurl );
	return -1;
	}
#endif /* AUTH_FILE */

    /* Referrer check. */
    if ( ! check_referrer( hc ) )
	return -1;

    /* Is it world-executable and in the CGI area? */
    if ( hc->hs->cgi_pattern != (char*) 0 &&
	 ( hc->sb.st_mode & S_IXOTH ) &&
	 match( hc->hs->cgi_pattern, hc->expnfilename ) )
	return cgi( hc );

    /* It's not CGI.  If it's executable or there's pathinfo, someone's
    ** trying to either serve or run a non-CGI file as CGI.   Either case
    ** is prohibited.
    */
    if ( hc->sb.st_mode & S_IXOTH )
	{
	syslog(
	    LOG_NOTICE, "%.80s URL \"%.80s\" is executable but isn't CGI",
	    httpd_ntoa( &hc->client_addr ), hc->encodedurl );
	httpd_send_err(
	    hc, 403, err403title, "",
	    ERROR_FORM( err403form, "The requested URL '%.80s' resolves to a file which is marked executable but is not a CGI file; retrieving it is forbidden.\n" ),
	    hc->encodedurl );
	return -1;
	}
    if ( hc->pathinfo[0] != '\0' )
	{
	syslog(
	    LOG_INFO, "%.80s URL \"%.80s\" has pathinfo but isn't CGI",
	    httpd_ntoa( &hc->client_addr ), hc->encodedurl );
	httpd_send_err(
	    hc, 403, err403title, "",
	    ERROR_FORM( err403form, "The requested URL '%.80s' resolves to a file plus CGI-style pathinfo, but the file is not a valid CGI file.\n" ),
	    hc->encodedurl );
	return -1;
	}

    if ( hc->method != METHOD_GET && hc->method != METHOD_HEAD )
	{
	httpd_send_err(
	    hc, 501, err501title, "", err501form, httpd_method_str( hc->method ) );
	return -1;
	}

    /* Fill in last_byte_index, if necessary. */
    if ( hc->got_range &&
	 ( hc->last_byte_index == -1 || hc->last_byte_index >= hc->sb.st_size ) )
	hc->last_byte_index = hc->sb.st_size - 1;

    figure_mime( hc );

    if ( hc->method == METHOD_HEAD )
	{
	send_mime(
	    hc, 200, ok200title, hc->encodings, "", hc->type, hc->sb.st_size,
	    hc->sb.st_mtime );
	}
    else if ( hc->if_modified_since != (time_t) -1 &&
	 hc->if_modified_since >= hc->sb.st_mtime )
	{
	send_mime(
	    hc, 304, err304title, hc->encodings, "", hc->type, (off_t) -1,
	    hc->sb.st_mtime );
	}
    else
	{
	hc->file_address = mmc_map( hc->expnfilename, &(hc->sb), nowP );
	if ( hc->file_address == (char*) 0 )
	    {
	    httpd_send_err( hc, 500, err500title, "", err500form, hc->encodedurl );
	    return -1;
	    }
	send_mime(
	    hc, 200, ok200title, hc->encodings, "", hc->type, hc->sb.st_size,
	    hc->sb.st_mtime );
	}

    return 0;
    }


int
httpd_start_request( httpd_conn* hc, struct timeval* nowP )
    {
    int r;

    /* Really start the request. */
    r = really_start_request( hc, nowP );

    /* And return the status. */
    return r;
    }


static void
make_log_entry( httpd_conn* hc, struct timeval* nowP )
    {
    char* ru;
    char url[305];
    char bytes[40];

    if ( hc->hs->no_log )
	return;

    /* This is straight CERN Combined Log Format - the only tweak
    ** being that if we're using syslog() we leave out the date, because
    ** syslogd puts it in.  The included syslogtocern script turns the
    ** results into true CERN format.
    */

    /* Format remote user. */
    if ( hc->remoteuser[0] != '\0' )
	ru = hc->remoteuser;
    else
	ru = "-";
    /* If we're vhosting, prepend the hostname to the url.  This is
    ** a little weird, perhaps writing separate log files for
    ** each vhost would make more sense.
    */
    if ( hc->hs->vhost && ! hc->tildemapped )
	(void) my_snprintf( url, sizeof(url),
	    "/%.100s%.200s",
	    hc->hostname == (char*) 0 ? hc->hs->server_hostname : hc->hostname,
	    hc->encodedurl );
    else
	(void) my_snprintf( url, sizeof(url),
	    "%.200s", hc->encodedurl );
    /* Format the bytes. */
    if ( hc->bytes_sent >= 0 )
	(void) my_snprintf(
	    bytes, sizeof(bytes), "%lld", (long long) hc->bytes_sent );
    else
	(void) strcpy( bytes, "-" );

    /* Logfile or syslog? */
    if ( hc->hs->logfp != (FILE*) 0 )
	{
	time_t now;
	struct tm* t;
	const char* cernfmt_nozone = "%d/%b/%Y:%H:%M:%S";
	char date_nozone[100];
	int zone;
	char sign;
	char date[100];

	/* Get the current time, if necessary. */
	if ( nowP != (struct timeval*) 0 )
	    now = nowP->tv_sec;
	else
	    now = time( (time_t*) 0 );
	/* Format the time, forcing a numeric timezone (some log analyzers
	** are stoooopid about this).
	*/
	t = localtime( &now );
	(void) strftime( date_nozone, sizeof(date_nozone), cernfmt_nozone, t );
#ifdef HAVE_TM_GMTOFF
	zone = t->tm_gmtoff / 60L;
#else
	zone = -timezone / 60L;
	/* Probably have to add something about daylight time here. */
#endif
	if ( zone >= 0 )
	    sign = '+';
	else
	    {
	    sign = '-';
	    zone = -zone;
	    }
	zone = ( zone / 60 ) * 100 + zone % 60;
	(void) my_snprintf( date, sizeof(date),
	    "%s %c%04d", date_nozone, sign, zone );
	/* And write the log entry. */
	(void) fprintf( hc->hs->logfp,
	    "%.80s - %.80s [%s] \"%.80s %.300s %.80s\" %d %s \"%.200s\" \"%.200s\"\n",
	    httpd_ntoa( &hc->client_addr ), ru, date,
	    httpd_method_str( hc->method ), url, hc->protocol,
	    hc->status, bytes, hc->referrer, hc->useragent );
#ifdef FLUSH_LOG_EVERY_TIME
	(void) fflush( hc->hs->logfp );
#endif
	}
    else
	syslog( LOG_INFO,
	    "%.80s - %.80s \"%.80s %.200s %.80s\" %d %s \"%.200s\" \"%.200s\"",
	    httpd_ntoa( &hc->client_addr ), ru,
	    httpd_method_str( hc->method ), url, hc->protocol,
	    hc->status, bytes, hc->referrer, hc->useragent );
    }


/* Returns 1 if ok to serve the url, 0 if not. */
static int
check_referrer( httpd_conn* hc )
    {
    int r;
    char* cp;

    /* Are we doing referrer checking at all? */
    if ( hc->hs->url_pattern == (char*) 0 )
	return 1;

    r = really_check_referrer( hc );

    if ( ! r )
	{
	if ( hc->hs->vhost && hc->hostname != (char*) 0 )
	    cp = hc->hostname;
	else
	    cp = hc->hs->server_hostname;
	if ( cp == (char*) 0 )
	    cp = "";
	syslog(
	    LOG_INFO, "%.80s non-local referrer \"%.80s%.80s\" \"%.80s\"",
	    httpd_ntoa( &hc->client_addr ), cp, hc->encodedurl, hc->referrer );
	httpd_send_err(
	    hc, 403, err403title, "",
	    ERROR_FORM( err403form, "You must supply a local referrer to get URL '%.80s' from this server.\n" ),
	    hc->encodedurl );
	}
    return r;
    }


/* Returns 1 if ok to serve the url, 0 if not. */
static int
really_check_referrer( httpd_conn* hc )
    {
    httpd_server* hs;
    char* cp1;
    char* cp2;
    char* cp3;
    static char* refhost = (char*) 0;
    static size_t refhost_size = 0;
    char *lp;

    hs = hc->hs;

    /* Check for an empty referrer. */
    if ( hc->referrer == (char*) 0 || hc->referrer[0] == '\0' ||
	 ( cp1 = strstr( hc->referrer, "//" ) ) == (char*) 0 )
	{
	/* Disallow if we require a referrer and the url matches. */
	if ( hs->no_empty_referrers && match( hs->url_pattern, hc->origfilename ) )
	    return 0;
	/* Otherwise ok. */
	return 1;
	}

    /* Extract referrer host. */
    cp1 += 2;
    for ( cp2 = cp1; *cp2 != '/' && *cp2 != ':' && *cp2 != '\0'; ++cp2 )
	continue;
    httpd_realloc_str( &refhost, &refhost_size, cp2 - cp1 );
    for ( cp3 = refhost; cp1 < cp2; ++cp1, ++cp3 )
	if ( isupper(*cp1) )
	    *cp3 = tolower(*cp1);
	else
	    *cp3 = *cp1;
    *cp3 = '\0';

    /* Local pattern? */
    if ( hs->local_pattern != (char*) 0 )
	lp = hs->local_pattern;
    else
	{
	/* No local pattern.  What's our hostname? */
	if ( ! hs->vhost )
	    {
	    /* Not vhosting, use the server name. */
	    lp = hs->server_hostname;
	    if ( lp == (char*) 0 )
		/* Couldn't figure out local hostname - give up. */
		return 1;
	    }
	else
	    {
	    /* We are vhosting, use the hostname on this connection. */
	    lp = hc->hostname;
	    if ( lp == (char*) 0 )
		/* Oops, no hostname.  Maybe it's an old browser that
		** doesn't send a Host: header.  We could figure out
		** the default hostname for this IP address, but it's
		** not worth it for the few requests like this.
		*/
		return 1;
	    }
	}

    /* If the referrer host doesn't match the local host pattern, and
    ** the filename does match the url pattern, it's an illegal reference.
    */
    if ( ! match( lp, refhost ) && match( hs->url_pattern, hc->origfilename ) )
	return 0;
    /* Otherwise ok. */
    return 1;
    }


char*
httpd_ntoa( httpd_sockaddr* saP )
    {
#ifdef USE_IPV6
    static char str[200];

    if ( getnameinfo( &saP->sa, sockaddr_len( saP ), str, sizeof(str), 0, 0, NI_NUMERICHOST ) != 0 )
	{
	str[0] = '?';
	str[1] = '\0';
	}
    else if ( IN6_IS_ADDR_V4MAPPED( &saP->sa_in6.sin6_addr ) && strncmp( str, "::ffff:", 7 ) == 0 )
	/* Elide IPv6ish prefix for IPv4 addresses. */
	(void) ol_strcpy( str, &str[7] );

    return str;

#else /* USE_IPV6 */

    return inet_ntoa( saP->sa_in.sin_addr );

#endif /* USE_IPV6 */
    }


static int
sockaddr_check( httpd_sockaddr* saP )
    {
    switch ( saP->sa.sa_family )
	{
	case AF_INET: return 1;
#ifdef USE_IPV6
	case AF_INET6: return 1;
#endif /* USE_IPV6 */
	default:
	return 0;
	}
    }


static size_t
sockaddr_len( httpd_sockaddr* saP )
    {
    switch ( saP->sa.sa_family )
	{
	case AF_INET: return sizeof(struct sockaddr_in);
#ifdef USE_IPV6
	case AF_INET6: return sizeof(struct sockaddr_in6);
#endif /* USE_IPV6 */
	default:
	return 0;	/* shouldn't happen */
	}
    }


/* Some systems don't have snprintf(), so we make our own that uses
** either vsnprintf() or vsprintf().  If your system doesn't have
** vsnprintf(), it is probably vulnerable to buffer overruns.
** Upgrade!
*/
static int
my_snprintf( char* str, size_t size, const char* format, ... )
    {
    va_list ap;
    int r;

    va_start( ap, format );
#ifdef HAVE_VSNPRINTF
    r = vsnprintf( str, size, format, ap );
#else /* HAVE_VSNPRINTF */
    r = vsprintf( str, format, ap );
#endif /* HAVE_VSNPRINTF */
    va_end( ap );
    return r;
    }


#ifndef HAVE_ATOLL
static long long
atoll( const char* str )
    {
    long long value;
    long long sign;

    while ( isspace( *str ) )
	++str;
    switch ( *str )
	{
	case '-': sign = -1; ++str; break;
	case '+': sign = 1; ++str; break;
	default: sign = 1; break;
	}
    value = 0;
    while ( isdigit( *str ) )
	{
	value = value * 10 + ( *str - '0' );
	++str;
	}
    return sign * value;
    }
#endif /* HAVE_ATOLL */


/* Read the requested buffer completely, accounting for interruptions. */
int
httpd_read_fully( int fd, void* buf, size_t nbytes )
    {
    int nread;

    nread = 0;
    while ( nread < nbytes )
	{
	int r;

	r = read( fd, (char*) buf + nread, nbytes - nread );
	if ( r < 0 && ( errno == EINTR || errno == EAGAIN ) )
	    {
	    sleep( 1 );
	    continue;
	    }
	if ( r < 0 )
	    return r;
	if ( r == 0 )
	    break;
	nread += r;
	}

    return nread;
    }


/* Write the requested buffer completely, accounting for interruptions. */
int
httpd_write_fully( int fd, const char* buf, size_t nbytes )
    {
    int nwritten;

    nwritten = 0;
    while ( nwritten < nbytes )
	{
	int r;

	r = write( fd, buf + nwritten, nbytes - nwritten );
	if ( r < 0 && ( errno == EINTR || errno == EAGAIN ) )
	    {
	    sleep( 1 );
	    continue;
	    }
	if ( r < 0 )
	    return r;
	if ( r == 0 )
	    break;
	nwritten += r;
	}

    return nwritten;
    }


/* Generate debugging statistics syslog message. */
void
httpd_logstats( long secs )
    {
    if ( str_alloc_count > 0 )
	syslog( LOG_NOTICE,
	    "  libhttpd - %d strings allocated, %lu bytes (%g bytes/str)",
	    str_alloc_count, (unsigned long) str_alloc_size,
	    (float) str_alloc_size / str_alloc_count );
    }
