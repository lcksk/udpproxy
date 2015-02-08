// type of data

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
