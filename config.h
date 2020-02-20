/* Copyright (c) 2015-2019 rlandjon <rlandjon@gmail.com>
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
 */
 //type of data

#define TYPE_UNKNOWN        0
#define TYPE_SCALAR         1
#define TYPE_ARRAY          2
#define TYPE_HASH           3

#define TYPE_UNKNOWN_STR    "unknown"
#define TYPE_SCALAR_STR     "scalar"
#define TYPE_ARRAY_STR      "array"
#define TYPE_HASH_STR       "hash"

// buffer sizes
#define TYPE_LEN            10
#define KEYWORD_LEN         64
#define LINE_LEN            256     // single line input
#define BUFFER_SIZE         2048    // includes all continuation lines

#define SEPARATOR           ','
#define SPACE               ' '
#define TAB                 '\t'
#define DOUBLE_QUOTE        '\"'
#define SINGLE_QUOTE        '\''
#define ESCAPE              '\\'

#define ESCAPE_STR          "EsCaPe"
#define SEPARATOR_STR       "CoMmA"


struct hash {
    char    *name ;
    char    *value ;
    struct  hash *next ;
};

struct keyword {
    char    *name ;
    char    *value ;
    char    **values ;
    struct  hash    *hash_value ;
    short   type ;
    struct  keyword *next ;
};

struct section {
    char    *name ;
    char    **keyword_names ;       // short-cut list of names.
    struct  keyword *keyword_ptr ;
    struct  section *next ;
} ;
    
struct config {
    int     index ;
    char    *error ;
    char    *file ;
    char    *current_section ;
    char    **section_names ;       // short-cut list of names.
    struct  section *section_ptr ;
    struct  config *next ;
} ;
