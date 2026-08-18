/*
 * ini.h  Opus 5
 *
 * Minimal sectioned INI reader with a fixed capacity and no dependencies.  
 *
 * Grammar:
 *   [section]        opens a section; everything before the first one is ignored
 *   key = value      trimmed on both sides; value may be empty
 *   # or ;           starts a comment, to end of line
 *
 * Every failure names the file line so a bad config is a startup error rather
 * than a setting that silently does nothing.
 */
#ifndef INI_H
#define INI_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define INI_MAX_SECTIONS 24
#define INI_MAX_KEYS     16
#define INI_MAX_LEN      128

typedef struct ini ini_t;

/*
 * Reads path into memory. On failure returns NULL and writes a message
 * describing what and where into err.
 */
ini_t* ini_load(const char* path, char* err, size_t errlen);
void   ini_free(ini_t* ini);

size_t      ini_section_count(const ini_t* ini);
const char* ini_section_name(const ini_t* ini, size_t i);
bool        ini_has_section(const ini_t* ini, const char* section);

/*
 * Raw lookup. NULL when the section or key is absent.
 */
const char* ini_get(const ini_t* ini, const char* section, const char* key);

/*
 * Typed lookups. An absent key yields the default and succeeds; a present but
 * malformed value fails.
 *
 * Returns 0 on success, -1 on a malformed value (err names section and key).
 *
 * ini_get_str points out at storage owned by ini, valid until ini_free.
 */
int ini_get_str(const ini_t* ini, const char* section, const char* key,
                const char* def, const char** out, char* err, size_t errlen);
int ini_get_u32(const ini_t* ini, const char* section, const char* key,
                uint32_t def, uint32_t* out, char* err, size_t errlen);
int ini_get_f64(const ini_t* ini, const char* section, const char* key,
                double def, double* out, char* err, size_t errlen);
int ini_get_bool(const ini_t* ini, const char* section, const char* key,
                 bool def, bool* out, char* err, size_t errlen);

/*
 * Schema check: fails if the section holds any key outside allowed, so a
 * misspelled key is reported rather than ignored.
 */
int ini_check_keys(const ini_t* ini, const char* section,
                   const char* const* allowed, size_t n,
                   char* err, size_t errlen);

#endif // !INI_H
