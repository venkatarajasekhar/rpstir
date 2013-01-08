#ifndef _LIB_CONFIGLIB_CONFIGLIB_H
#define _LIB_CONFIGLIB_CONFIGLIB_H

#include <stdlib.h>
#include <stdbool.h>

#include "util/logging.h"
#include "util/macros.h"

#include "configlib/configlib_type.h"


/**
    This is the only configlib file that should be directly included by code
    outside of configlib itself. This file should be included by code that
    defines configuration options or new configuration types.

    If you want to use the existing configuration options and types, see
    lib/config instead of lib/configlib.

    If you want to define configuration options, continue reading this file.
    See the types directory for the already implemented types.

    If you want to define new configuration types, see configlib_type.h.
*/


/**
    Structure to describe an available config option.

    This is used by config_load() below to define all the available options for
    a program.
*/
struct config_option {
    // configuration key, e.g. "SomeOption"
    char *name;

    // Type information, see files in the types directory for examples of what
    // to put in these fields. If a particular type doesn't define a usr_arg
    // or an inverse, use NULL. If a particular type doesn't define a free, use
    // free(3). array_validate (and its usr_arg) can be NULL if no inter-value
    // validation is needed.
    bool is_array;
    config_value_converter value_convert;
    void *value_convert_usr_arg;
    config_value_converter_inverse value_convert_inverse;
    void *value_convert_inverse_usr_arg;
    config_value_free value_free;
    config_array_validator array_validate;
    void *array_validate_usr_arg;

    // Default value, as if it came from the config file. NULL indicates that
    // the option is mandatory. Remember to quote this as if it were parsed
    // in a config file:
    //
    // 1) default_value = "foo bar"
    // 2) default_value = "\"foo bar\""
    //
    // (1) is an array with values "foo" and "bar". (2) is the single value
    // "foo bar".
    char *default_value;
};


/**
    Return the value for a non-array config option.

    This function can be used directly, but the helper functions generated by
    the below CONFIG_GET_HELPER or CONFIG_GET_HELPER_DEREFERENCE macros are
    often easier to use. See the descriptions of those macros for more
    information.
*/
const void *config_get(
    size_t key);

/** Return the length of an array config option. */
size_t config_get_length(
    size_t key);

/**
    Return the values for an array config option.

    Similarly to config_get(), this can be used directly but the functions
    generated by CONFIG_GET_ARRAY_HELPER or CONFIG_GET_ARRAY_HELPER_DEREFERENCE
    are often easier to use.
*/
void const * const * config_get_array(
    size_t key);

/**
    Generate a helper function around config_get() that returns the appropriate
    pointer type.

    For a string type option CONFIG_FOO, this code:

        // in some function body
        const char * foo = (const char *)config_get(CONFIG_FOO);

    Is equivalent to this:

        // in a header file, also note the lack of a semicolon
        CONFIG_GET_HELPER(CONFIG_FOO, char)

        // in a function body
        const char * foo = CONFIG_FOO_get();

    If the CONFIG_FOO option is used multiple times, the second form saves
    typing and prevents accidentally casting to the wrong type or removing
    const-ness, e.g.:

        // this is wrong:
        char * foo = (char *)config_get(CONFIG_FOO);
*/
#define CONFIG_GET_HELPER(key, type) \
    static inline const type * key ## _get() \
    { \
        return (const type *)config_get(key); \
    }

/**
    Same as CONFIG_GET_HELPER above, but dereference the pointer before
    returning.

    For an int type CONFIG_FOO, this code:

        // in function body
        int foo = *(const int *)config_get(CONFIG_FOO);

    Is equivalent to:

        // in header
        CONFIG_GET_HELPER_DEREFERENCE(CONFIG_FOO, int)

        // in function body
        int foo = CONFIG_FOO_get();
*/
#define CONFIG_GET_HELPER_DEREFERENCE(key, type) \
    static inline type key ## _get() \
    { \
        return *(const type *)config_get(key); \
    }

/**
    Generate two helper functions around config_get_array() that return the
    appropriate pointer types. The first function returns the entire array,
    the second returns an item in the array.

    For an array of strings type CONFIG_FOO, this code:

        // in some function body
        char const * const * foo = (char const * const *)config_get_array(CONFIG_FOO);
        const char * foo_0 = foo[0];

    Is equivalent to this:

        // in a header file
        CONFIG_GET_ARRAY_HELPER(CONFIG_FOO, char)

        // in a function body
        char const * const * foo = CONFIG_FOO_get_array();
        const char * foo_0 = foo[0];

        // or an alternate form to just get foo_0
        const char * foo_0 = CONFIG_FOO_get(0);

    Note that for any array CONFIG_FOO, and index i less than
    config_get_length(CONFIG_FOO), the following should be true:

        CONFIG_FOO_get_array()[i] == CONFIG_FOO_get(i)
*/
#define CONFIG_GET_ARRAY_HELPER(key, type) \
    static inline type const * const * key ## _get_array() \
    { \
        return (type const * const *)config_get_array(key); \
    } \
    static inline type const * key ## _get(size_t index) \
    { \
        return key ## _get_array()[index]; \
    }

/**
    Same as CONFIG_GET_ARRAY_HELPER above, but the helper that returns
    individual items dereferences them before returning.

    For an array of ints type CONFIG_FOO, this code:

        // in some function body
        int const * const * foo = (int const * const *)config_get_array(CONFIG_FOO);
        int foo_0 = *foo[0];

    Is equivalent to this:

        // in a header file
        CONFIG_GET_ARRAY_HELPER_DEREFERENCE(CONFIG_FOO, int)

        // in a function body
        int const * const * foo = CONFIG_FOO_get_array();
        int foo_0 = *foo[0];

        // or an alternate form to just get foo_0
        int foo_0 = CONFIG_FOO_get(0);

    Similar to with CONFIG_GET_ARRAY_HELPER, the following condition is true:

        *CONFIG_FOO_get_array()[i] == CONFIG_FOO_get(i)
*/
#define CONFIG_GET_ARRAY_HELPER_DEREFERENCE(key, type) \
    static inline type const * const * key ## _get_array() \
    { \
        return (type const * const *)config_get_array(key); \
    } \
    static inline type key ## _get(size_t index) \
    { \
        return *(key ## _get_array()[index]); \
    }

/**
    Return a string representation of the config option specified by its name.

    @note This function should not be used by most C programs. It is not meant
          to be particularly fast, and it leads to repeatedly parsing the same
          data. This should mainly only be used for interfaces with other
          languages, e.g. shell.

    @return string that should be free()d, or NULL on error
*/
char * config_find(
    const char * key);

/**
    Load configuration data from a config file. Note that most programs should
    use a wrapper around this that fills in the correct parameters. Only call
    this function directly if you need to specify a different set of options,
    e.g. for a program that tests configlib. See lib/config for the wrapper.

    @note This is not thread-safe and MUST be called before any threads that
          could possibly use configuration data are started.

    @param num_options Number of config options.
    @param options Description of options.
    @param filename The file to load data from. This can be NULL, see below.
                    It is an error if filename is not NULL and the specified
                    file can't be accessed. This file is loadded IN ADDITION TO
                    a file from default_filenames. Any value in this file
                    overrides the value in a default file.
    @param default_filenames NULL-terminated array of (NULL-terminated strings
                             of) files to try. Each file is tried in order
                             until one exists. Once an existing file is found,
                             no more files are checked. A value of NULL for
                             default_filenames indicates no defaults. If no
                             files are found, it is not inherently an error:
                             the default values for each configuration item are
                             used. However, if there are any mandatory
                             variables, those will cause errors.
*/
bool config_load(
    size_t num_options,
    const struct config_option *options,
    const char *filename,
    char const * const * default_filenames);

/**
    Call this after configuration data is no longer needed to free resources.

    This is usually only called before a program exits.

    @note This MUST NOT be called when any threads could possibly use
          configuration data.
*/
void config_unload(
    );

#endif
