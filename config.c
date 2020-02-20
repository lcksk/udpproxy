/*
 * handle config file(s).
 * format is:
 *      section-name1:
 *          keyword1 (scalar)   = value1
 *          keyword2            = value2
 *          keyword3            = 'this is a really big multi-line \
 *                                 value with spaces on the end   '
 *          keyword4 (array)    = val1, val2, 'val 3   ', val4
 *          keyword5 (hash)     = v1 = this, \
 *                                v2 = " that ", \
 *                                v3 = fooey
 *      section-name2:
 *          keyword4 = value1
 *          ...
 * Copyright (c) 2015-2019 rlandjon <rlandjon@gmail.com>
 *
 * This file is part of udpproxy.
 *
 * udpproxy is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * udpproxy is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with ijkPlayer; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * The 'type' of a value defaults to scalar and does not need to be given.
 * Continuation lines have a backslash as the last character on a line.
 * Supports #include files to any depth via recursion.
 * Can (single or double) quote values to maintain whitespace.
 * Multiple values use a comma as a separator.
 * You can provide commas in your values by escaping them with a backslash.
 * ie:   keyword = 'This has a comma here \, as part of this sentence'
 * To get a backslash in your values, escape it with another backslash.
 * Can handle multiple concurrent config files.  cfg_read_config_file()
 * returns a config descriptor index you use in subsequent calls.
 *
 * Sample usage to print out sections, keywords and values of foobar.conf
 * assuming all keywords are of type scalar (otherwise, use cfg_get_type_str()
 * to find the type of a value so you can use the correct cfg_get_*() calls):
 *
 *      char **sp, **kp, **sections, **keywords, *value, *error ;
 *      int  cfg_index ;
 *
 *      cfg_index = cfg_read_config_file( "foobar.conf" ) ;
 *      if ( cfg_error_msg( cfg_index )) {      // check for errors
 *          error = cfg_error_msg( cfg_index ) ;
 *          fprintf( stderr,  "Error: %s", error ) ;
 *          exit(1) ;
 *      }
 *
 *      sections = cfg_get_sections( cfg_index ) ;
 *      for ( sp = sections ; *sp ; sp++ ) {
 *          printf( "\t%s\n", *sp ) ;
 *          keywords = cfg_get_keywords( cfg_index, *sp ) ;
 *          for ( kp = keywords ; *kp ; kp++ ) {
 *              value = cfg_get_value( cfg_index, *sp, *kp ) ;
 *              printf( "\t\t\%s = \'%s\'\n", *kp, value ) ;
 *          }
 *      }
 *
 * User callable functions begin with 'cfg_':
 *      char    cfg_get_type_str( int, char *, char * )
 *      int     cfg_get_type( int, char *, char * )
 *      int     cfg_read_config_file( char * )
 *      void    cfg_show_configs()
 *      int     cfg_set_debug( int )
 *      void    cfg_debug( const char *fmt, ... )
 *      char    *cfg_get_value( int indx, char *section, char *keyword )
 *      char    *cfg_get_hash_value( int, char *, char *, char * )
 *      char    *cfg_get_filename( int )
 *      char    *cfg_error_msg( int )
 *      char    **cfg_get_sections( int )
 *      char    **cfg_get_keywords( int, char * )
 *      char    **cfg_get_values( int indx, char *section, char *keyword )
 *      char    **cfg_get_hash_keys( int, char *, char * )
 *
 * all other functions should not be called directly
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include "config.h"


// globals

static int      debug_flag = 0 ;
static int      config_entry_num = 0  ;
static struct config *config_head = (struct config *)NULL ;

// internal functions
static  void set_err_msg( const char *whoami, int, const char *fmt, ... ) ;
static  struct config *make_config_entry() ;
static  struct hash *make_hash_value_entry( char *, char *) ;
static  struct section *make_section_entry( char * ) ;
static  int     process_file( int, struct config *, char * ) ;
static  int     store_values( int, short, char *, char *) ;
static  void    trim_whitespace( char *p ) ;
static  int     translate_escape_chars( char *, char *, int ) ;
static  int     translate_back_escape_chars( char *, char * ) ;

// user-callable internal functions
void    cfg_debug( const char *fmt, ... ) ;
void    cfg_show_configs() ;
int     cfg_set_debug( int ) ;
int     cfg_read_config_file( char * ) ;
char    **cfg_get_sections( int ) ;
char    **cfg_get_hash_keys( int, char *, char *) ;
char    *cfg_get_hash_value( int, char *, char *, char *) ;
char    **cfg_get_keywords( int, char * ) ;
char    *cfg_get_value( int, char *, char * ) ;
char    **cfg_get_values( int, char *, char * ) ;
char    *cfg_get_filename( int ) ;
char    *cfg_error_msg( int ) ;
char    *cfg_get_type_str( int, char *, char * ) ;
int     cfg_get_type( int, char *, char * ) ;

// extern functions
extern void *malloc( size_t );
extern void exit( int );
extern int strncmp( const char *, const char *, size_t );
extern size_t strlen( const char * );


/*
 * Turn debugging on or off.
 * Return the previous value.
 *
 * Inputs:
 *      value:  0 (off) | !0 (on)
 * Returns:
 *      previous (integer) value
 */

int
cfg_set_debug( int value )
{
    int old_debug = debug_flag ;

    debug_flag = value ;
    return( old_debug ) ;
}


/*
 * check and get error message.
 *
 * Inputs:
 *      config-index-number returned by cfg_read_config_file()
 * Returns:
 *      string (char *) if there was an error
 *      NULL if there is no error
 */

char *
cfg_error_msg( int indx )
{
    struct config *ptr ;
    char *i_am = "cfg_error_msg()" ;

    for ( ptr = config_head ; ptr ; ptr = ptr->next ) {
        if (( ptr->index == indx ) && ( ptr->error )) {
            cfg_debug( "%s: Found error msg for config index %d\n",
                i_am, indx ) ;
            return( ptr->error ) ;
        }
    }
    return( (char *)NULL ) ;
}


/*
 * get the config filename
 *
 * Inputs:
 *      integer index number returned by cfg_read_config_file()
 * Returns:
 *      filename (char *)
 */

char *
cfg_get_filename( int indx )
{
    struct config *ptr ;
    char *i_am = "cfg_filename()" ;

    for ( ptr = config_head ; ptr ; ptr = ptr->next ) {
        if (( ptr->index == indx ) && ( ptr->file )) {
            return( ptr->file ) ;
        }
    }
    return( (char *)NULL ) ;
}




/*
 * read a config file.
 * format is:
 *      section-name1:
 *          keyword1 = value1
 *          keyword2 = value2
 *          ...
 * continuation lines have a backslash as the last character on a line.
 * supports #include files to any depth via recursion.
 *
 * Inputs:
 *      filename
 * Returns:
 *      integer index number used in other cfg_() calls
 */

int
cfg_read_config_file( char *file )
{
    char    *p ;
    char    *i_am = "cfg_read_config_file()" ;
    struct  config *ptr ;
    int     indx ;

    cfg_debug( "%s: file = %s\n", i_am, file ) ;

    // allocate a new config entry

    ptr = make_config_entry() ;
    ptr->index = config_entry_num ;
    indx = config_entry_num++ ;

    /* now copy the filename into the config struct  */
    if (( p = (char *)malloc( strlen( file )+1 )) == NULL ) {
        fprintf( stderr, "Can\'t malloc for config filename\n" ) ;
        exit(1) ;
    }
    strcpy( p, file ) ;
    ptr->file = p ;

    config_head = ptr ;

    // now finally read the file

    if ( process_file( indx, ptr, file )) {
        return( indx ) ;
    }

    return( indx ) ;
}


/*
 * process a config file.
 * NOT to be called by user.
 *
 * Inputs:
 *      integer index number returned by cfg_read_config_file
 *      filename of config file.  Could be a #include'd file
 *
 * Returns:
 *      0 = ok, 1 = not ok
 */

static int
process_file( int indx, struct config *ptr, char *file )
{
    char    *i_am = "process_file()" ;
    char    line[ LINE_LEN ], total_line[ BUFFER_SIZE ], *equals_p ;
    char    input_line[ LINE_LEN ] ;
    char    keyword[ KEYWORD_LEN ], value[ LINE_LEN ], type_str[ TYPE_LEN ] ;
    char    *bp1, *bp2, *p, *f ;
    struct  section *sp ;
    FILE    *fp ;
    short   whitespace_flag, continuation_flag, len, total_len ;
    short   line_number, type ;

    if (( fp = fopen( file, "r" )) == NULL ) {
        set_err_msg( i_am, indx, "Can\'t open: %s\n", file ) ;
        return(1) ;  // error
    }

    total_line[0] = '\0' ;      // for dealing with continuation lines
    total_len     = 0 ;
    line_number   = 0 ;

    while ( fgets( input_line, BUFFER_SIZE, fp ) != NULL ) {

        // we need to handle escape characters
        if ( translate_escape_chars( input_line, line, BUFFER_SIZE )) {
            set_err_msg( i_am, indx,
                "Translated buffer too long on line number %d in %s\n",
                line_number, file ) ;
            return(1) ;     // error
        }

        line_number++ ;
        if (( p = index( line, '\n' )) != NULL )
            *p = '\0' ;

        // skip blank lines
        if ( line[0] == '\0' )
            continue ;

        // skip lines that are just whitespace
        whitespace_flag = 1 ;
        for ( p=line ; *p ; p++ ) {
            if (( *p != SPACE ) && ( *p != TAB )) {
                whitespace_flag = 0 ;
                break ;
            }
        }
        if ( whitespace_flag == 1 ) {
            continue ;
        }

        // look for include files
        if ( strncmp( line, "#include", sizeof( "#include" )-1) == 0 ) {
            // skip over spaces and get filename
            p = &line[ sizeof( "#include" ) ] ;
            for ( ; ( *p == SPACE ) || ( *p == TAB ) ; p++ ) ;
            f = p ;
            cfg_debug( "%s: Got include in file %s (%s)\n", i_am, file, f ) ;

            if ( process_file( indx, ptr, f )) {
                return(1) ;  // error
            }

        } else {
            // skip comments
            if ( line[0] == '#' )
                continue ;

            // see if it is a new section
            if (( line[0] != SPACE ) && ( line[0] != TAB )) {
                cfg_debug( "%s: Got a section: %s\n", i_am, line ) ;
                // its a section
                if (( p = index( line, ':' )) != NULL ) {
                    *p = '\0' ;
                }
                // see if section name already exists
                for ( sp=ptr->section_ptr ; sp ; sp=sp->next ) {
                    if ( strcmp( line, sp->name ) == 0 ) {
                        ptr->current_section = sp->name ;
                        break ;
                    }
                }
                if ( sp == (struct section *)NULL ) {
                    // allocate space for section structure
                    sp = make_section_entry( line ) ;
                    ptr->current_section = sp->name ;

                    // link into config list
                    sp->next = ptr->section_ptr ;
                    ptr->section_ptr = sp ;
                }
            } else {
                // not in a section anymore.
                cfg_debug( "%s: Got a NON-section: %s\n", i_am, line ) ;

                // trim trailing whitespace
                for ( p=line ; *p ; p++ ) ;     // get to end of line
                p-- ;                           // last character
                while (( p >= &line[0] ) &&
                       (( *p == SPACE ) || ( *p == TAB ))) {
                    *p-- = '\0' ;     // replace whitespace with NULL
                }

                /*
                 * see if a continuation line, which ends in a '\'.
                 * If so, strip it, and add all the lines together.
                 * Keep any whitespace immediately before the '\',
                 * but strip leading whitespace on each line.
                 */

                len = strlen( line ) ;
                if ( line[ len-1 ] == ESCAPE ) {
                    cfg_debug( "%s: continuation line %s\n", i_am, line ) ;
                    line[ len-1 ] = '\0' ;
                    continuation_flag = 1 ;
                    total_len += len + 1 ;
                } else {
                    continuation_flag = 0 ;
                }

                /*
                 * skip over leading whitespace so that
                 * p is now pointing to start of real data
                 */
                for ( p=line ; (( *p == SPACE ) || ( *p == TAB )) ; p++ ) ;

                if ( continuation_flag ) {
                    strcat( total_line, p ) ;
                    continue ;          // get the next line
                } else {
                    /*
                     * see if it is the last line of a continuation.
                     * If so, take care of last line and initialize
                     * everything again.
                     * Check that we aren't going to overrun our buffer.
                     */

                    if ( total_line[0] ) {
                        // do a length check
                        len = strlen(p) ;
                        total_len += len + 1 ;
                        if ( total_len >= BUFFER_SIZE ) {
                            set_err_msg(
                                i_am, indx,
                                "Line too long on line number %d in %s\n",
                                line_number, file ) ;
                            return(1) ;     // error
                        }

                        strcat( total_line, p ) ;
                        strcpy( line, total_line ) ;

                        total_line[0] = '\0' ;
                        total_len = 0 ;
                        continuation_flag = 0 ;
                    }
                }

                /*
                 * ok, we're done with the line continuation stuff.
                 * now skip over leading whitespace again with the line
                 * of data - which might be several lines added together
                 */

                for ( p=line ; (( *p == SPACE ) || ( *p == TAB )) ; p++ ) ;
                cfg_debug( "%s: FINAL line = \'%s\'\n", i_am, p ) ;

                // we now expect to have a line that is a keyword line.

                equals_p = index( p, '=' ) ;
                if ( ! equals_p ) {
                    set_err_msg( i_am, indx,
                        "Invalid keyword entry (missing =) on line number %d in %s\n",
                        line_number, file ) ;
                    return(1) ;     // error
                }
                // separate the keyword and value(s)
                *equals_p++ = '\0' ;

                // get the keyword.  We've already skipped over whitespace
                len = strlen( p ) ;
                if ((len + 1) > KEYWORD_LEN ) {
                    set_err_msg( i_am, indx,
                        "keyword (\'%s\') too long on line number %d in %s\n",
                        p, line_number, file ) ;
                    return(1) ;     // error
                }
                strcpy( keyword, p ) ;
                trim_whitespace( keyword ) ;

                /*
                 * We now have a keyword.
                 * If it doesn't have a type hint, then default to TYPE_SCALAR
                 */
                type = TYPE_SCALAR ;        // default
                if (( bp1 = index( keyword, '(' )) &&
                    ( bp2 = index( keyword, ')' )) &&
                    ( bp2 > bp1 )) {

                    /*
                     * separate out the type string from the
                     * keyword. we'll need to trim the keyword string
                     * again to get rid of whitespace between the keyword
                     * and the type string
                     */
                    *bp1 = '\0' ;   // clobber leading ')'
                    *bp2 = '\0' ;   // clobber trailing ')'
                    trim_whitespace( keyword ) ;

                    // check that we have room to store the type string
                    len = bp2 - bp1 - 1 ;
                    if ((len + 1) > TYPE_LEN ) {
                        set_err_msg( i_am, indx,
                            "Type (%s) too long on line number %d in %s\n",
                            bp1+1, line_number, file ) ;
                        return(1) ;     // error
                    }
                    strcpy( type_str, bp1+1 ) ;
                    // make lower case
                    for ( bp2=type_str ; *bp2 ; bp2++ ) {
                        *bp2 = tolower( *bp2 ) ;
                    }

                    // see what type of 'type' value we have
                    if ( ! strcmp( type_str, TYPE_SCALAR_STR )) {
                        type = TYPE_SCALAR ;
                    } else if ( ! strcmp( type_str, TYPE_ARRAY_STR )) {
                        type = TYPE_ARRAY ;
                    } else if ( ! strcmp( type_str, TYPE_HASH_STR )) {
                        type = TYPE_HASH ;
                    } else {
                        set_err_msg( i_am, indx,
                            "Unknown Type (%s) on line number %d in %s\n",
                            type_str, line_number, file ) ;
                        return(1) ;     // error
                    }
                }

                // now get the value.  make sure there is something there
                if ( ! *equals_p ) {
                    set_err_msg( i_am, indx,
                        "Invalid keyword entry on line number %d in %s\n",
                        line_number, file ) ;
                    return(1) ;     // error
                }
                for ( p=equals_p ; ((*p == SPACE) || (*p == TAB)) ; p++ ) ;
                strcpy( value, p ) ;

                // now store it.
                if ( store_values( indx, type, keyword, p ))
                    return(1) ;
            }
        }
    }
    fclose( fp ) ;
    return(0) ;     // ok
}


/*
 * store keyword/value(s)
 * This function is overloaded so it it handles scalars, arrays
 * and hashes.  Which inouyt argument it uses depends on the
 * value 'type'
 *
 * Inputs:
 *      integer index number returned by cfg_read_config_file()
 *      type    type of value (TYPE_SCALAR, TYPE_ARRAY, TYPE_HASH)
 *      keyword (char *)
 *      values  (char **)
 *      value   (char *)
 * Returns:
 *      integer 0=ok, 1=not-ok
 */

static int
store_values( int indx, short type, char *key, char *val )
{
    struct  config *ptr ;
    struct  section *sp ;
    struct  keyword *kp ;
    struct  hash    *hash_p ;
    short   found, len, last, value_count ;
    char    *i_am, *section, *p, *p2, *p3, *next_p,  *real_val_p ;
    char    **array_p, real_value[ BUFFER_SIZE ] ;
    char    *hash_name_p, *hash_value_p ;

    i_am = "store_values()" ;

    /*
     * find the correct section.
     * We have to go through all this hassle because we can't just use
     * a global to track the current session since there can be multiple
     * concurrent configs being processed.  We know which config we want
     * by the index (Arg 1)
     * XXX is this true?  can probably use a global...  -rj
     */

    found = 0 ;
    for ( ptr = config_head ; ptr ; ptr = ptr->next ) {
        if ( ptr->index == indx ) {
            found++ ;
            break ;
        }
    }
    if ( ! found ) {
        set_err_msg( i_am, indx,
            "Could not find config entry for keyword=\'%s\', cfg indx=%d\n",
            key, indx ) ;
        return(1) ;
    }
    section = ptr->current_section ;
    cfg_debug( "%s: WANT section = \'%s\'\n", i_am, section ) ;

    // find the right section structure
    for ( sp=ptr->section_ptr ; sp ; sp=sp->next ) {
        if ( ! strcmp( sp->name, section )) {
            cfg_debug( "%s: FOUND section = \'%s\'\n", i_am, section ) ;
            break ;
        }
    }

    if ( sp == (struct section *)NULL ) {
        set_err_msg( i_am, indx,
            "Can\'t find section struct for kywd=\'%s\', cfg indx=%d\n",
            key, indx ) ;
        return(1) ;
    }

    /*
     * now store the keyword.  See if it already exists.
     * If it does, we'll end up overwriting the old value.
     */

    for ( kp=sp->keyword_ptr ; kp ; kp=kp->next ) {
        if ( ! strcmp( kp->name, key )) {
            cfg_debug( "%s: FOUND key = \'%s\'\n", i_am, key ) ;
            break ;
        }
    }
    if ( kp == ( struct keyword *)NULL ) {
        cfg_debug( "%s: did NOT find key = \'%s\'\n", i_am, key ) ;

        kp = (struct keyword *)malloc( sizeof( struct keyword )) ;
        if ( kp == NULL ) {
            fprintf( stderr, "Can\'t malloc for keyword structure\n" ) ;
            exit(1) ;
        }
        kp->next         = sp->keyword_ptr ;
        sp->keyword_ptr  = kp ;

        // get space for the name
        len = strlen( key ) ;
        if (( p = (char *)malloc( len + 1 )) == NULL ) {
            fprintf( stderr, "Can\'t malloc for keyword name\n" ) ;
            exit(1) ;
        }
        strcpy( p, key ) ;
        kp->name = p ;
        kp->value      = (char *)NULL ;
        kp->values     = (char **)NULL ;
        kp->hash_value = (struct hash *)NULL ;
    }

    trim_whitespace( val ) ;

    // finally...  store the value(s)

    if ( type == TYPE_SCALAR ) {
        // free up the old value if it exists
        if ( kp->value )
            free( kp->value ) ;

        /*
        * see if value quoted and if so, strip.
        * Assume they may have used balanced single
        * OR double quotes but NOT both.  ie:  '"foobar   "'
        */

        real_val_p = val ;
        len = strlen( val ) ;
        if (( val[0] == SINGLE_QUOTE ) &&
            ( val[ len-1 ] == SINGLE_QUOTE )) {
                val[len-1] = '\0' ;
                real_val_p++ ;
        }
        if (( val[0] == DOUBLE_QUOTE ) &&
            ( val[ len-1 ] == DOUBLE_QUOTE )) {
                val[len-1] = '\0' ;
                real_val_p++ ;
        }

        // translate back escape and separator characters
        translate_back_escape_chars( real_val_p, real_value ) ;

        // get space for the new value
        len = strlen( real_value ) ;
        if (( p = (char *)malloc( len + 1 )) == NULL ) {
            fprintf( stderr, "Can\'t malloc for value name\n" ) ;
            exit(1) ;
        }
        strcpy( p, real_value ) ;
        kp->value      = p ;
        kp->values     = (char **)NULL ;
        kp->hash_value = (struct hash *)NULL ;
        kp->type = TYPE_SCALAR ;

    } else if ( type == TYPE_ARRAY ) {
        p = val ;
        last = 0 ;
        /*
         * need a count of how many entries we'll have so we can allocate
         * an array of pointers the right size.  It will be terminated
         * with a NULL entry
         */

        value_count = 1 ;      // we have at least 1 value
        for ( p2=p ; *p2 ; p2++ ) {
            if ( *p2 == SEPARATOR )
                value_count++ ;
        }

        // need to allocate space for array.  Don't forget NULL termination (+1)
        array_p = (char **)malloc( sizeof( char *) * (value_count + 1)) ;
        if ( array_p == (char **)NULL ) {
            fprintf( stderr, "Can\'t malloc for section names\n" ) ;
            exit(1) ;
        }
        kp->values     = array_p  ;     // point to our new (empty) array
        kp->value      = (char *)NULL ;
        kp->hash_value = (struct hash *)NULL ;

        while ( *p ) {
            if ( p2 = index( p, SEPARATOR )) {
                *p2 = '\0' ;
                next_p = p2 + 1 ;
                while ( *next_p && ( *next_p == SPACE ) || ( *next_p == TAB ))
                    next_p++ ;
            } else {
                last++ ;
            }

            trim_whitespace( p ) ;

            // handle quoted values
            real_val_p = p ;
            len = strlen( p ) ;
            if (( p[0] == SINGLE_QUOTE ) &&
                ( p[ len-1 ] == SINGLE_QUOTE )) {
                    p[len-1] = '\0' ;
                    real_val_p++ ;
            }
            if (( p[0] == DOUBLE_QUOTE ) &&
                ( p[ len-1 ] == DOUBLE_QUOTE )) {
                    p[len-1] = '\0' ;
                    real_val_p++ ;
            }

            // translate back escape and separator characters
            translate_back_escape_chars( real_val_p, real_value ) ;

            // get space for the new value and store value
            len = strlen( real_value ) ;
            if (( p3 = (char *)malloc( len + 1 )) == NULL ) {
                fprintf( stderr, "Can\'t malloc for value name\n" ) ;
                exit(1) ;
            }
            strcpy( p3, real_value ) ;

            *array_p = p3 ;         // store pointer in our values array
            array_p++ ;             // prepare to store next entry

            if ( last ) break ;     // get out if last value

            p = next_p ;            // point to start of next value
        }
        *array_p = (char *)NULL ;   // terminate array
        kp->type = TYPE_ARRAY ;

    } else if ( type == TYPE_HASH ) {
        last = 0 ;
        p = val ;
        while ( *p ) {
            if ( p2 = index( p, SEPARATOR )) {
                *p2 = '\0' ;
                next_p = p2 + 1 ;
                while ( *next_p && ( *next_p == SPACE ) || ( *next_p == TAB ))
                    next_p++ ;
            } else {
                last++ ;
            }

            // we should have a 'name = value' string pointed to by p

            if (( p2 = index( p, '=' )) == NULL ) {
                set_err_msg( i_am, indx,
                    "did not get name = value for hash with cfg indx=%d\n",
                    key, indx ) ;
                return(1) ;
            }
            // separate out the name
            *p2 = '\0' ;
            hash_name_p = p ;
            trim_whitespace( hash_name_p ) ;

            // separate out the value.  skip over whitespace
            for ( p2++ ; ( *p2 == SPACE ) || ( *p2 == TAB ) ; p2++ ) ;
            hash_value_p  = p2 ;
            trim_whitespace( hash_value_p ) ;

            // handle quoted values
            len = strlen( hash_value_p ) ;
            if (( hash_value_p[0] == SINGLE_QUOTE ) &&
                ( hash_value_p[ len-1 ] == SINGLE_QUOTE )) {
                    hash_value_p[len-1] = '\0' ;
                    hash_value_p++ ;
            }
            if (( hash_value_p[0] == DOUBLE_QUOTE ) &&
                ( hash_value_p[ len-1 ] == DOUBLE_QUOTE )) {
                    hash_value_p[len-1] = '\0' ;
                    hash_value_p++ ;
            }

            // translate back escape and separator characters
            translate_back_escape_chars( hash_value_p, real_value ) ;

            // get space for the new name and value and store
            hash_p = make_hash_value_entry( hash_name_p, real_value ) ;

            // link into keyword structure
            hash_p->next   = kp->hash_value ;
            kp->hash_value = hash_p ;

            if ( last ) break ;     // get out if last value

            p = next_p ;            // point to start of next value
        }

    } else {
        set_err_msg( i_am, indx,
            "Invalid type (%d) for keyword=\'%s\', cfg indx=%d\n",
            type, key, indx ) ;
        return(1) ;
    }
    kp->type  = type ;

    return(0) ;
}

/*
 * Show config entries.  Used for debugging.
 * Writes to stdout
 *
 * Inputs:
 *      None
 * Returns:
 *      nothing (void).
 */

void
cfg_show_configs()
{
    struct  config  *ptr ;
    struct  section *sp ;
    struct  keyword *kp ;
    char    *err, *file, *type_str, **vp ;
    short   count, type ;

    for ( ptr = config_head ; ptr ; ptr = ptr->next ) {
        err = ptr->error ;
        if ( err == (char *)NULL ) err = "<not set>" ;

        file = ptr->file ;
        if ( file == (char *)NULL ) file = "<not set>" ;

        printf( "%-20s %s\n",  "Filename:", file ) ;
        printf( "%-20s %d\n",  "Index:", ptr->index ) ;
        printf( "%-20s %s\n",  "Error Msg:", err ) ;
        printf( "%-20s\n",  "Data:" ) ;

        // now for each section

        count = 0 ;
        for ( sp=ptr->section_ptr ; sp ; sp=sp->next ) {
            count++ ;
            printf( "   %s\n", sp->name ) ;

            // now print the keyword/value(s)
            for ( kp=sp->keyword_ptr ; kp ; kp=kp->next ) {

                // convert the type to a string
                type = kp->type ;
                switch ( type ) {
                case TYPE_SCALAR:
                    type_str = "scalar" ;
                    break ;
                case TYPE_ARRAY:
                    type_str = "array" ;
                    break ;
                case TYPE_HASH:
                    type_str = "hash" ;
                    break ;
                default:
                    type_str = "unknown" ;
                    break ;
                }

                type = kp->type ;
                if ( type == TYPE_SCALAR ) {
                    printf( "      %-20s \'%s\'\n", kp->name, kp->value ) ;
                } else if ( type == TYPE_ARRAY ) {
                    if ( kp->values ) {
                        int first = 1 ;
                        for ( vp = kp->values ; *vp ; vp++ ) {
                            if ( first ) {
                                printf( "      %-20s \'%s\'\n", kp->name, *vp ) ;
                            } else {
                                printf( "      %-20s \'%s\'\n", "", *vp ) ;
                            }
                            first = 0 ;
                        }
                    }
                } else if ( type == TYPE_HASH ) {
                    if ( kp->hash_value ) {
                        int first = 1 ;
                        struct hash *hp ;
                        for ( hp = kp->hash_value ; hp ; hp = hp->next ) {
                            if ( first ) {
                                printf( "      %-20s %s = \'%s\'\n",
                                    kp->name, hp->name, hp->value ) ;
                            } else {
                                printf( "      %-20s %s = \'%s\'\n",
                                    "", hp->name, hp->value ) ;
                            }
                            first = 0 ;
                        }
                    }
                }
            }
        }
        if ( count == 0 ) {
            printf( "   <none found>\n" ) ;
        }
        printf( "\n" ) ;
    }
}


/*
 * debug output.
 *
 * Inputs:
 *      formatting string
 *      zero or more arguments referred to by format string.
 * Returns:
 *      Nothing (void)
 */

void
cfg_debug( const char *fmt, ... )
{
    va_list args ;
    char    str[ 512 ], *p ;
    char    *prefix = "Debug: " ;

    if ( debug_flag == 0 )
        return ;

    va_start( args, fmt ) ;

    p = str ;
    sprintf( p, "%s", prefix ) ;
    p += strlen( prefix ) ;
    vsprintf( p, fmt, args ) ;
    fprintf( stderr, "%s", str ) ;

    va_end( args ) ;
}


/*
 * error message
 *
 * Inputs:
 *      identifier string printed if debug_flag turned on
 *      integer index number returned by cfg_read_config_file()
 *      formatting string
 *      zero or more arguments referred to by format string.
 * Returns:
 *      Nothing (void)
 */

static void
set_err_msg( const char *whoami, int indx, const char *fmt, ... )
{
    va_list args ;
    char    error[ 512 ], *p ;
    struct  config *ptr ;
    int     found ;
    char    *i_am = "set_err_msg()" ;

    va_start( args, fmt ) ;

    p = error ;
    *p = '\0' ;

    if ( debug_flag ) {
        sprintf(  p, "%s: ", i_am ) ;
        p += strlen( i_am ) + 2 ;
        sprintf(  p, "%s: ", whoami ) ;
        p += strlen( whoami ) + 2 ;
    }
    vsprintf( p, fmt, args ) ;

    va_end( args ) ;

    // see if we have a config entry built yet.
    found = 0 ;
    for ( ptr = config_head ; ptr ; ptr = ptr->next ) {
        if ( ptr->index == indx ) {
            found++ ;
            break ;
        }
    }
    if ( found == 0 ) {
        // this shouldn't happen...
        cfg_debug( "%s: Need to build config entry for index %d\n",
            i_am, indx ) ;

        ptr = make_config_entry() ;
        ptr->index = indx ;

        config_head = ptr ;
    }

    /* now copy the error message ptr into the config struct  */
    if (( p = (char *)malloc( strlen( error )+1 )) == NULL ) {
        fprintf( stderr, "Can\'t malloc for config error msg\n" ) ;
        exit(1) ;
    }
    strcpy( p, error ) ;
    ptr->error = p ;
}



/*
 * make a hash value entry
 *
 * Inputs:
 *      name  (string)
 *      value (string)
 * Returns:
 *      pointer to new hash value structure.
 */

static struct hash *
make_hash_value_entry( char *name, char *value )
{
    struct hash *ptr ;
    char *p ;

    ptr = (struct hash *)malloc( sizeof( struct hash )) ;
    if ( ptr == NULL ) {
        fprintf( stderr, "Can\'t malloc for hash structure\n" ) ;
        exit(1) ;
    }

    // allocate space for name and store
    if (( p = (char *)malloc( strlen( name )+1 )) == NULL ) {
        fprintf( stderr, "Can\'t malloc for hash name\n" ) ;
        exit(1) ;
    }
    strcpy( p, name ) ;
    ptr->name = p ;

    // allocate space for value and store
    if (( p = (char *)malloc( strlen( value )+1 )) == NULL ) {
        fprintf( stderr, "Can\'t malloc for hash value\n" ) ;
        exit(1) ;
    }
    strcpy( p, value ) ;

    ptr->value = p ;
    ptr->next  = (struct hash *)NULL ;

    return( ptr ) ;
}


/*
 * make a config entry
 *
 * Inputs:
 *      None
 * Returns:
 *      pointer to new config structure.
 * Globals used:
 *      config_head
 */

static struct config *
make_config_entry()
{
    struct config *ptr ;

    ptr = (struct config *)malloc( sizeof( struct config )) ;
    if ( ptr == NULL ) {
        fprintf( stderr, "Can\'t malloc for config structure\n" ) ;
        exit(1) ;
    }
    ptr->next               = config_head ;
    ptr->error              = '\0' ;
    ptr->file               = '\0' ;
    ptr->index              = -1 ;
    ptr->current_section    = (char *)NULL ;
    ptr->section_ptr        = (struct section *)NULL ;
    ptr->section_names      = (char **)NULL ;

    return( ptr ) ;
}


/*
 * make a section entry
 *
 * Inputs:
 *      name (char *) of section
 * Returns:
 *      pointer to new section structure.
 */

static struct section *
make_section_entry( char *name )
{
    struct section *sp ;
    char    *p ;

    sp = (struct section *)malloc( sizeof( struct section )) ;
    if ( sp == NULL ) {
        fprintf( stderr, "Can\'t malloc for section structure\n" ) ;
        exit(1) ;
    }

    sp->keyword_ptr    = (struct keyword *)NULL ;
    sp->keyword_names  = (char **)NULL ;

    // allocate space for name and store
    if (( p = (char *)malloc( strlen( name )+1 )) == NULL ) {
        fprintf( stderr, "Can\'t malloc for section name\n" ) ;
        exit(1) ;
    }
    strcpy( p, name ) ;
    sp->name = p ;

    return( sp ) ;
}


/*
 * get the type of a value(s) and return a string
 *
 * Inputs:
 *      integer index number returned by cfg_read_config_file()
 *      section (char *)
 *      keyword (char *)
 * Returns:
 *      string
 * Usage:
 *      type = cfg_get_type_str( cfg_index, section, keyword ) ;
 */

char *
cfg_get_type_str( int indx, char *section, char *keyword )
{
    struct  config  *ptr ;
    struct  section *sp ;
    struct  keyword *kp ;

    short   type ;

    type = cfg_get_type( indx, section, keyword ) ;

    switch( type ) {
    case TYPE_SCALAR:
        return( TYPE_SCALAR_STR ) ;
    case TYPE_ARRAY :
        return( TYPE_ARRAY_STR ) ;
    case TYPE_HASH:
        return( TYPE_HASH_STR ) ;
    default:
        return( TYPE_UNKNOWN_STR ) ;
    }
}


/*
 * get the type of a value(s)
 *
 * Inputs:
 *      integer index number returned by cfg_read_config_file()
 *      section (char *)
 *      keyword (char *)
 * Returns:
 *      type (integer > 0)
 *      0 = unknown
 * Usage:
 *      type = cfg_get_type( cfg_index, section, keyword ) ;
 */

int
cfg_get_type( int indx, char *section, char *keyword )
{
    struct  config  *ptr ;
    struct  section *sp ;
    struct  keyword *kp ;

    for ( ptr = config_head ; ptr ; ptr = ptr->next ) {
        if ( ptr->index == indx ) {
            for ( sp=ptr->section_ptr ; sp ; sp=sp->next ) {
                if ( ! strcmp( sp->name, section )) {
                    for ( kp=sp->keyword_ptr ; kp ; kp=kp->next ) {
                        if ( ! strcmp( kp->name, keyword )) {
                            return( kp->type ) ;
                            break ;
                        }
                    }
                    break ;
                }
            }
            break ;
        }
    }
    return(0) ;
}



/*
 * get the hash keys of a hash key value
 *
 * Inputs:
 *      integer index number returned by cfg_read_config_file()
 *      section (char *)
 *      keyword (char *)
 * Returns:
 *      pointer to array of strings (char **).
 * Usage:
 *      char **keys = cfg_get_hash_keys( cfg_index, section, keyword ) ;
 *      char **p ;
 *      for ( p = keys ; *p ; p++ ) {
 *          printf( "key = %s\n", *p ) ;
 *      }
 */

char **
cfg_get_hash_keys( int indx, char *section, char *keyword )
{
    struct  config  *ptr ;
    struct  section *sp ;
    struct  keyword *kp ;
    struct  hash    *hp ;
    short   found, count ;
    char    **p, **array_p, *name ;

    found = 0 ;
    for ( ptr = config_head ; ptr ; ptr = ptr->next ) {
        if ( ptr->index == indx ) {
            for ( sp=ptr->section_ptr ; sp ; sp=sp->next ) {
                if ( ! strcmp( sp->name, section )) {
                    for ( kp=sp->keyword_ptr ; kp ; kp=kp->next ) {
                        if ( ! strcmp( kp->name, keyword )) {
                            found++ ;
                            break ;
                        }
                    }
                    break ;
                }
            }
            break ;
        }
    }

    /*
     * This will happen if we have no list to return.
     * Need to create a dummy empty list to return
     */
    if ( found == 0 ) {
        // need to allocate space for dummy empty list of strings
        p = (char **)malloc( sizeof( char *)) ;
        if ( p == (char **)NULL ) {
            fprintf( stderr, "Can\'t malloc for empty hash values list\n" ) ;
            exit(1) ;
        }
        *p = (char *)NULL ;         // empty list
        return(p) ;
    }

    /*
    * need a count of how many entries we'll have so we can allocate
    * an array of pointers the right size.  It will be terminated
    * with a NULL entry
    */
    count = 0 ;
    for ( hp = kp->hash_value ; hp ; hp = hp->next ) {
        count++ ;
    }

    // need to allocate space for array.  Don't forget NULL termination (+1)
    array_p = (char **)malloc( sizeof( char *) * (count + 1)) ;
    if ( array_p == (char **)NULL ) {
        fprintf( stderr, "Can\'t malloc for section names\n" ) ;
        exit(1) ;
    }

    // now fill the memory with addresses of section names
    p = array_p ;
    for ( hp = kp->hash_value ; hp ; hp = hp->next ) {
        name = hp->name ;
        *p = name ;
        p++ ;
    }
    *p = (char *)NULL ;         // terminate it

    return( array_p ) ;
}



/*
 * get a value
 *
 * Inputs:
 *      integer index number returned by cfg_read_config_file()
 *      section (char *)
 *      keyword (char *)
 * Returns:
 *      value (char *)
 * Usage:
 *     char **sp, **kp, **sections, **keywords, *value, *file ;
 *     file = cfg_get_filename( cfg_index ) ;
 *     printf( "%s:\n", file ) ;
 *     sections = cfg_get_sections( cfg_index ) ;
 *     for ( sp = sections ; *sp ; sp++ ) {
 *         printf( "\t%s\n", *sp ) ;
 *         keywords = cfg_get_keywords( cfg_index, *sp ) ;
 *         for ( kp = keywords ; *kp ; kp++ ) {
 *             value = cfg_get_value( cfg_index, *sp, *kp ) ;
 *             printf( "\t\t\%s = \'%s\'\n", *kp, value ) ;
 *         }
 *     }
 */

char *
cfg_get_value( int indx, char *section, char *keyword )
{
    struct  config  *ptr ;
    struct  section *sp ;
    struct  keyword *kp ;

    for ( ptr = config_head ; ptr ; ptr = ptr->next ) {
        if ( ptr->index == indx ) {
            for ( sp=ptr->section_ptr ; sp ; sp=sp->next ) {
                if ( ! strcmp( sp->name, section )) {
                    for ( kp=sp->keyword_ptr ; kp ; kp=kp->next ) {
                        if ( ! strcmp( kp->name, keyword )) {
                            return( kp->value ) ;
                            break ;
                        }
                    }
                    break ;
                }
            }
            break ;
        }
    }
    return( (char *)NULL ) ;
}


/*
 * get values
 *
 * Inputs:
 *      integer index number returned by cfg_read_config_file()
 *      section (char *)
 *      keyword (char *)
 * Returns:
 *      values (char **)
 * Usage:
 *      int  cfg_index ;
 *      char **values, **vp, *section, *keyword ;
 *      values = cfg_get_values( cfg_index, section, keyword ) ;
 *       if ( values == (char **)NULL ) {
 *           printf( "No values found for section %s, keyword %s\n",
                section, keyword ) ;
 *       } else {
 *           for ( vp = values ; *vp ; vp++ ) {
 *               printf( "\'%s\'\n ", *vp ) ;
 *           }
 *       }
 */

char **
cfg_get_values( int indx, char *section, char *keyword )
{
    struct  config  *ptr ;
    struct  section *sp ;
    struct  keyword *kp ;

    for ( ptr = config_head ; ptr ; ptr = ptr->next ) {
        if ( ptr->index == indx ) {
            for ( sp=ptr->section_ptr ; sp ; sp=sp->next ) {
                if ( ! strcmp( sp->name, section )) {
                    for ( kp=sp->keyword_ptr ; kp ; kp=kp->next ) {
                        if ( ! strcmp( kp->name, keyword )) {
                            return( kp->values ) ;
                            break ;
                        }
                    }
                    break ;
                }
            }
            break ;
        }
    }
    return( (char **)NULL ) ;
}

/*
 * return or create (if missing) an array of keyword names for
 * the section.  The first time we are called, we crawl through
 * the linked list to get the keyword names, but we build a array
 * of strings to return on subsequent calls.
 *
 * Inputs:
 *      integer index number returned by cfg_read_config_file()
 *      section (char *)
 * Returns:
 *      pointer to array of strings (char **).
 * Usage:
 *      char **keywords = cfg_get_keywords( cfg_index, section ) ;
 *      char **p ;
 *      for ( p = keywords ; *p ; p++ ) {
 *          printf( "keyword = %s\n", *p ) ;
 *      }
 */

char **
cfg_get_keywords( int indx, char* section )
{
    struct config  *ptr ;
    struct section *sp ;
    struct keyword *kp ;
    char *name, **p, *i_am ;
    short   num_keywords = 0, found = 0 ;

    i_am = "cfg_get_keywords()" ;
    found = 0 ;
    for ( ptr = config_head ; ptr ; ptr = ptr->next ) {
        if ( ptr->index == indx ) {
            for ( sp=ptr->section_ptr ; sp ; sp=sp->next ) {
                if ( ! strcmp( sp->name, section )) {
                    // ok ,we have the right section in the right config
                    found++ ;
                    break ;
                }
            }
        }
    }

    /*
     * This will happen if we have no list to return.
     * Need to create a dummy empty list to return
     */
    if ( found == 0 ) {
        // need to allocate space for dummy empty list of strings
        p = (char **)malloc( sizeof( char *)) ;
        if ( p == (char **)NULL ) {
            fprintf( stderr, "Can\'t malloc for empty keyword list\n" ) ;
            exit(1) ;
        }
        *p = (char *)NULL ;         // empty list
        return(p) ;
    }

    if ( sp->keyword_names != (char **)NULL ) {
        cfg_debug( "%s: returning keyword names ptr for section %s cfg indx %d\n",
            i_am, section, indx ) ;
        return( sp->keyword_names ) ;
    }
    /*
     * We don't already have the array built.  Do it now.
     * First find out how many keywords there are.
     */
    for ( kp=sp->keyword_ptr ; kp ; kp=kp->next ) {
        num_keywords++ ;
    }
    cfg_debug( "%s: building keyword names (%d) struct for section %s cfg indx %d\n",
        i_am, num_keywords, section, indx ) ;

    // need to allocate space for array.  Don't forget NULL termination
    p = (char **)malloc( sizeof( char *) * (num_keywords + 1)) ;
    if ( p == (char **)NULL ) {
        fprintf( stderr, "Can\'t malloc for section names\n" ) ;
        exit(1) ;
    }
    sp->keyword_names = p ;

    // now fill the memory with addresses of section names
    for ( kp=sp->keyword_ptr ; kp ; kp=kp->next ) {
        name = kp->name ;
        *p = name ;
        p++ ;
    }
    *p = (char *)NULL ;         // terminate it

    return( sp->keyword_names ) ;
}



/*
 * return or create (if missing) an array of section names for
 * the config file.  The first time we are called, we crawl through
 * the linked list to get the section names, but we build a array
 * of strings to return on subsequent calls.
 *
 * Inputs:
 *      integer index number returned by cfg_read_config_file()
 * Returns:
 *      pointer to array of strings (char **).
 * Usage:
 *      char **sections = cfg_get_sections( cfg_index ) ;
 *      char **p ;
 *      for ( p = sections ; *p ; p++ ) {
 *          printf( "section = %s\n", *p ) ;
 *      }
 */

char **
cfg_get_sections( int indx )
{
    struct section *sp ;
    struct config *ptr ;
    char *name, **p, *i_am ;
    int num_sections = 0 ;

    i_am = "cfg_get_sections()" ;
    for ( ptr = config_head ; ptr ; ptr = ptr->next ) {
        if ( ptr->index == indx ) {
            if ( ptr->section_names != (char **)NULL ) {
                cfg_debug( "%s: returning section names pointer for cfg indx %d\n",
                    i_am, indx ) ;
                return( ptr->section_names ) ;
            }

            /*
             * We don't already have the array built.  Do it now.
             * First find out how many section names there are.
             */
            for ( sp=ptr->section_ptr ; sp ; sp=sp->next ) {
                num_sections++ ;
            }
            cfg_debug( "%s: building section names (%d) struct for cfg indx %d\n",
                i_am, num_sections, indx ) ;

            // need to allocate space for array.  Don't forget NULL termination
            p = (char **)malloc( sizeof( char *) * (num_sections + 1) ) ;
            if ( p == (char **)NULL ) {
                fprintf( stderr, "Can\'t malloc for section names\n" ) ;
                exit(1) ;
            }
            ptr->section_names = p ;

            // now fill the memory with addresses of section names
            for ( sp=ptr->section_ptr ; sp ; sp=sp->next ) {
                name = sp->name ;
                *p = name ;
                p++ ;
            }
            *p = (char *)NULL ;         // terminate it

            break ;
        }
    }

    /*
     * This will happen if we have no list or a bogus index number
     * was passed to us.  Need to create a dummy empty list to return
     */
    if ( ptr == NULL ) {
        // need to allocate space for dummy empty list of strings
        p = (char **)malloc( sizeof( char *)) ;
        if ( p == (char **)NULL ) {
            fprintf( stderr, "Can\'t malloc for empty section list\n" ) ;
            exit(1) ;
        }
        *p = (char *)NULL ;         // empty list
        return(p) ;
    }

    return( ptr->section_names ) ;
}


/*
 * get a hash value
 *
 * Inputs:
 *      integer index number returned by cfg_read_config_file()
 *      section (char *)
 *      keyword (char *)
 *      key     (char *)
 * Returns:
 *      value   (string)
 */

char *
cfg_get_hash_value( int indx, char *section, char *keyword, char *key )
{
    struct  config  *ptr ;
    struct  section *sp ;
    struct  keyword *kp ;
    struct  hash    *hp ;

    for ( ptr = config_head ; ptr ; ptr = ptr->next ) {
        if ( ptr->index == indx ) {
            for ( sp=ptr->section_ptr ; sp ; sp=sp->next ) {
                if ( ! strcmp( sp->name, section )) {
                    for ( kp=sp->keyword_ptr ; kp ; kp=kp->next ) {
                        if ( ! strcmp( kp->name, keyword )) {
                            for ( hp = kp->hash_value ; hp ; hp = hp->next ) {
                                if ( ! strcmp( key, hp->name )) {
                                    return( hp->value ) ;
                                }
                            }
                        }
                    }
                    break ;
                }
            }
            break ;
        }
    }

    return( (char *)NULL ) ;        // not found
}



/*
 * trim the whitespace off a string
 *
 * Inputs:
 *      string (char *)
 * Returns:
 *     nothing (void)
 */

static void
trim_whitespace( char *p )
{
    char    *orig_p ;

    orig_p = p ;
    // trim trailing whitespace
    for ( ; *p ; p++ ) ;     // get to end of line
    p-- ;                         // last character
    while (( p >= orig_p ) && (( *p == SPACE ) || ( *p == TAB ))) {
        *p-- = '\0' ;               // replace whitespace with NULL
    }
}


/*
 * We need to disquise our escape and separator characters to
 * allow us those characters as legitimate characters in our data.
 * This makes parsing alot easier.
 * It is obviously defeatable, but ur config file writers are not
 * out to break their own configs/software.  We just have to assume
 * the strings we temporarily translate them to wil not be found
 * in the config file
 *
 * Inputs:
 *      input string
 *      output string
 *      size of output buffer
 * Returns:
 *     0:   ok
 *     1:   output buffer exceeded
 */

static int
translate_escape_chars( char *input, char *output, int len )
{
    char    *p, *p2, *i_am ;
    short   e_len, s_len, i_len ;

    e_len = strlen( ESCAPE_STR  );
    s_len = strlen( SEPARATOR_STR ) ;
    i_len  = strlen( input ) ;

    p  = input ;
    p2 = output ;
    while ( *p ) {
        // disquise a escaped escape char
        if (( *p == ESCAPE ) && ( *(p+1) == ESCAPE )) {
            if ((i_len + e_len) > (len-1)) return(1) ;

            strcpy( p2, ESCAPE_STR ) ;
            p2 += e_len ;
            p  += 2 ;
            continue ;
        }

        // disquise a separator char
        if (( *p == ESCAPE ) && ( *(p+1) == SEPARATOR )) {
            if ((i_len + s_len) > (len-1)) return(1) ;

            strcpy( p2, SEPARATOR_STR ) ;
            p2 += s_len ;
            p  += 2 ;
            continue ;
        }

        *p2++ = *p++ ;
    }
    *p2 = '\0' ;    // terminate output

    return(0) ;
}

/*
 * translate back our escape and separator characters
 *
 * Inputs:
 *      input string
 *      output string
 * Returns:
 *     0:   ok
 */

static int
translate_back_escape_chars( char *input, char *output )
{
    char    *p, *p2 ;
    short   e_len, s_len ;

    e_len = strlen( ESCAPE_STR  );
    s_len = strlen( SEPARATOR_STR ) ;

    p  = input ;
    p2 = output ;
    while ( *p ) {
        if ( strncmp( p, SEPARATOR_STR, s_len ) == 0 ) {
            *p2++ = SEPARATOR ;
            p += s_len ;
            continue ;
        }

        if ( strncmp( p, ESCAPE_STR, e_len ) == 0 ) {
            *p2++ = ESCAPE ;
            p += e_len ;
            continue ;
        }

        *p2++ = *p++ ;
    }
    *p2 = '\0' ;    // terminate output

    return(0) ;
}
