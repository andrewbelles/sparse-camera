/*
 * ini.c  Opus 5
 * 
 * Implementation of the sectioned INI reader described in ini.h
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>

#include "ini.h"

#define INI_LINE_MAX 512

typedef struct {
  char key[INI_MAX_LEN];
  char value[INI_MAX_LEN];
} ini_pair_t;

typedef struct {
  char name[INI_MAX_LEN];
  ini_pair_t pairs[INI_MAX_KEYS];
  size_t n_pairs;
} ini_section_t;

struct ini {
  ini_section_t sections[INI_MAX_SECTIONS];
  size_t n_sections;
};


static void ini_err(char* err, size_t errlen, const char* fmt, ...)
  __attribute__((format(printf, 3, 4)));

static void ini_err(char* err, size_t errlen, const char* fmt, ...)
{
  if ( !err || errlen == 0 ) {
    return;
  }

  va_list ap;
  va_start(ap, fmt);
  vsnprintf(err, errlen, fmt, ap);
  va_end(ap);
}


/*
 * Trims leading and trailing whitespace in place, returning the new start.
 */
static char* trim(char* s)
{
  while ( *s && isspace((unsigned char)*s) ) {
    s++;
  }

  char* end = s + strlen(s);

  while ( end > s && isspace((unsigned char)end[-1]) ) {
    end--;
  }
  *end = '\0';

  return s;
}


/*
 * Truncates a comment. A '#' or ';' anywhere outside of leading whitespace
 * starts one; there is no quoting, which this config shape does not need.
 */
static void strip_comment(char* s)
{
  for ( char* p = s; *p; p++ ) {
    if ( *p == '#' || *p == ';' ) {
      *p = '\0';
      return;
    }
  }
}


static const ini_section_t* find_section(const ini_t* ini, const char* name)
{
  if ( !ini || !name ) {
    return NULL;
  }

  for ( size_t i = 0; i < ini->n_sections; i++ ) {
    if ( strcmp(ini->sections[i].name, name) == 0 ) {
      return &ini->sections[i];
    }
  }

  return NULL;
}


ini_t* ini_load(const char* path, char* err, size_t errlen)
{
  if ( !path ) {
    ini_err(err, errlen, "ini: no path given");
    return NULL;
  }

  FILE* fp = fopen(path, "r");

  if ( !fp ) {
    ini_err(err, errlen, "ini: cannot open %s: %s", path, strerror(errno));
    return NULL;
  }

  ini_t* ini = calloc(1, sizeof(*ini));

  if ( !ini ) {
    fclose(fp);
    ini_err(err, errlen, "ini: allocation failed");
    return NULL;
  }

  char line[INI_LINE_MAX];
  ini_section_t* cur = NULL;
  size_t lineno = 0;

  while ( fgets(line, sizeof(line), fp) ) {
    lineno++;
    strip_comment(line);

    char* s = trim(line);

    if ( *s == '\0' ) {
      continue;
    }

    if ( *s == '[' ) {
      char* close = strchr(s, ']');

      if ( !close ) {
        ini_err(err, errlen, "%s:%zu: section is missing ']'", path, lineno);
        goto fail;
      }

      *close = '\0';
      char* name = trim(s + 1);

      if ( *name == '\0' ) {
        ini_err(err, errlen, "%s:%zu: empty section name", path, lineno);
        goto fail;
      }

      if ( strlen(name) >= INI_MAX_LEN ) {
        ini_err(err, errlen, "%s:%zu: section name longer than %d",
                path, lineno, INI_MAX_LEN - 1);
        goto fail;
      }

      if ( find_section(ini, name) ) {
        ini_err(err, errlen, "%s:%zu: duplicate section [%s]", path, lineno, name);
        goto fail;
      }

      if ( ini->n_sections >= INI_MAX_SECTIONS ) {
        ini_err(err, errlen, "%s:%zu: more than %d sections",
                path, lineno, INI_MAX_SECTIONS);
        goto fail;
      }

      cur = &ini->sections[ini->n_sections++];
      snprintf(cur->name, sizeof(cur->name), "%s", name);
      continue;
    }

    char* eq = strchr(s, '=');

    if ( !eq ) {
      ini_err(err, errlen, "%s:%zu: expected 'key = value'", path, lineno);
      goto fail;
    }

    *eq = '\0';
    char* key   = trim(s);
    char* value = trim(eq + 1);

    if ( *key == '\0' ) {
      ini_err(err, errlen, "%s:%zu: empty key", path, lineno);
      goto fail;
    }

    if ( !cur ) {
      ini_err(err, errlen, "%s:%zu: key '%s' before any [section]",
              path, lineno, key);
      goto fail;
    }

    if ( strlen(key) >= INI_MAX_LEN || strlen(value) >= INI_MAX_LEN ) {
      ini_err(err, errlen, "%s:%zu: key or value longer than %d",
              path, lineno, INI_MAX_LEN - 1);
      goto fail;
    }

    for ( size_t i = 0; i < cur->n_pairs; i++ ) {
      if ( strcmp(cur->pairs[i].key, key) == 0 ) {
        ini_err(err, errlen, "%s:%zu: duplicate key '%s' in [%s]",
                path, lineno, key, cur->name);
        goto fail;
      }
    }

    if ( cur->n_pairs >= INI_MAX_KEYS ) {
      ini_err(err, errlen, "%s:%zu: more than %d keys in [%s]",
              path, lineno, INI_MAX_KEYS, cur->name);
      goto fail;
    }

    ini_pair_t* pair = &cur->pairs[cur->n_pairs++];
    snprintf(pair->key, sizeof(pair->key), "%s", key);
    snprintf(pair->value, sizeof(pair->value), "%s", value);
  }

  fclose(fp);
  return ini;

fail:
  fclose(fp);
  free(ini);
  return NULL;
}


void ini_free(ini_t* ini)
{
  free(ini);
}


size_t ini_section_count(const ini_t* ini)
{
  return ini ? ini->n_sections : 0;
}


const char* ini_section_name(const ini_t* ini, size_t i)
{
  if ( !ini || i >= ini->n_sections ) {
    return NULL;
  }

  return ini->sections[i].name;
}


bool ini_has_section(const ini_t* ini, const char* section)
{
  return find_section(ini, section) != NULL;
}


const char* ini_get(const ini_t* ini, const char* section, const char* key)
{
  const ini_section_t* sec = find_section(ini, section);

  if ( !sec || !key ) {
    return NULL;
  }

  for ( size_t i = 0; i < sec->n_pairs; i++ ) {
    if ( strcmp(sec->pairs[i].key, key) == 0 ) {
      return sec->pairs[i].value;
    }
  }

  return NULL;
}


int ini_get_str(const ini_t* ini, const char* section, const char* key,
                const char* def, const char** out, char* err, size_t errlen)
{
  const char* raw = ini_get(ini, section, key);

  if ( !raw ) {
    *out = def;
    return 0;
  }

  if ( *raw == '\0' ) {
    ini_err(err, errlen, "[%s] %s: value is empty", section, key);
    return -1;
  }

  *out = raw;
  return 0;
}


int ini_get_u32(const ini_t* ini, const char* section, const char* key,
                uint32_t def, uint32_t* out, char* err, size_t errlen)
{
  const char* raw = ini_get(ini, section, key);

  if ( !raw ) {
    *out = def;
    return 0;
  }

  errno = 0;
  char* end = NULL;
  unsigned long v = strtoul(raw, &end, 10);

  if ( end == raw || *trim(end) != '\0' || errno == ERANGE || v > UINT32_MAX ) {
    ini_err(err, errlen, "[%s] %s: '%s' is not an unsigned 32-bit integer",
            section, key, raw);
    return -1;
  }

  *out = (uint32_t)v;
  return 0;
}


int ini_get_f64(const ini_t* ini, const char* section, const char* key,
                double def, double* out, char* err, size_t errlen)
{
  const char* raw = ini_get(ini, section, key);

  if ( !raw ) {
    *out = def;
    return 0;
  }

  errno = 0;
  char* end = NULL;
  double v = strtod(raw, &end);

  if ( end == raw || *trim(end) != '\0' || errno == ERANGE ) {
    ini_err(err, errlen, "[%s] %s: '%s' is not a number", section, key, raw);
    return -1;
  }

  *out = v;
  return 0;
}


int ini_get_bool(const ini_t* ini, const char* section, const char* key,
                 bool def, bool* out, char* err, size_t errlen)
{
  const char* raw = ini_get(ini, section, key);

  if ( !raw ) {
    *out = def;
    return 0;
  }

  static const char* yes[] = { "true", "yes", "on", "1" };
  static const char* no[]  = { "false", "no", "off", "0" };

  for ( size_t i = 0; i < sizeof(yes) / sizeof(*yes); i++ ) {
    if ( strcasecmp(raw, yes[i]) == 0 ) {
      *out = true;
      return 0;
    }
  }

  for ( size_t i = 0; i < sizeof(no) / sizeof(*no); i++ ) {
    if ( strcasecmp(raw, no[i]) == 0 ) {
      *out = false;
      return 0;
    }
  }

  ini_err(err, errlen, "[%s] %s: '%s' is not a boolean "
          "(true/false, yes/no, on/off, 1/0)", section, key, raw);
  return -1;
}


int ini_check_keys(const ini_t* ini, const char* section,
                   const char* const* allowed, size_t n,
                   char* err, size_t errlen)
{
  const ini_section_t* sec = find_section(ini, section);

  if ( !sec ) {
    ini_err(err, errlen, "[%s]: no such section", section);
    return -1;
  }

  for ( size_t i = 0; i < sec->n_pairs; i++ ) {
    bool ok = false;

    for ( size_t j = 0; j < n && !ok; j++ ) {
      ok = strcmp(sec->pairs[i].key, allowed[j]) == 0;
    }

    if ( !ok ) {
      ini_err(err, errlen, "[%s]: unknown key '%s'", section, sec->pairs[i].key);
      return -1;
    }
  }

  return 0;
}
