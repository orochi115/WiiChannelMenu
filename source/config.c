#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#include "config.h"
#include "log.h"

#define LINE_MAX_LEN 1024

typedef enum { SEC_NONE, SEC_MENU, SEC_CHANNEL } Section;

static char *trim(char *s)
{
	while (*s && isspace((unsigned char)*s))
		s++;
	char *end = s + strlen(s);
	while (end > s && isspace((unsigned char)end[-1]))
		*--end = '\0';
	return s;
}

static char *unquote(char *s)
{
	size_t len = strlen(s);
	if (len >= 2 && ((s[0] == '"' && s[len - 1] == '"') || (s[0] == '\'' && s[len - 1] == '\''))) {
		s[len - 1] = '\0';
		return s + 1;
	}
	return s;
}

static bool set_str(char *dst, const char *val, int lineno, const char *key)
{
	if (strlen(val) >= CFG_STR_MAX) {
		log_msg("config: line %d: value of '%s' is too long", lineno, key);
		return false;
	}
	strcpy(dst, val);
	return true;
}

bool config_load(const char *path, MenuConfig *cfg)
{
	memset(cfg, 0, sizeof(*cfg));

	FILE *fp = fopen(path, "r");
	if (!fp) {
		log_msg("config: cannot open %s", path);
		return false;
	}

	char buf[LINE_MAX_LEN];
	Section sec = SEC_NONE;
	ChannelEntry *cur = NULL;
	int lineno = 0;
	bool ok = true;

	while (ok && fgets(buf, sizeof(buf), fp)) {
		lineno++;

		if (!strchr(buf, '\n') && !feof(fp)) {
			log_msg("config: line %d: line too long", lineno);
			ok = false;
			break;
		}

		char *line = buf;
		// UTF-8 BOM
		if (lineno == 1 && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF)
			line += 3;
		line = trim(line);

		if (*line == '\0' || *line == ';' || *line == '#')
			continue;

		if (*line == '[') {
			char *close = strchr(line, ']');
			if (!close || close[1] != '\0') {
				log_msg("config: line %d: malformed section header", lineno);
				ok = false;
				break;
			}
			*close = '\0';
			char *name = trim(line + 1);

			if (strcasecmp(name, "menu") == 0) {
				sec = SEC_MENU;
				cur = NULL;
			} else if (strcasecmp(name, "channel") == 0) {
				if (cfg->count >= MAX_CHANNELS) {
					log_msg("config: line %d: more than %d channels", lineno, MAX_CHANNELS);
					ok = false;
					break;
				}
				sec = SEC_CHANNEL;
				cur = &cfg->ch[cfg->count++];
				cur->line = lineno;
			} else {
				log_msg("config: line %d: unknown section [%s]", lineno, name);
				ok = false;
				break;
			}
			continue;
		}

		char *eq = strchr(line, '=');
		if (!eq) {
			log_msg("config: line %d: expected key = value", lineno);
			ok = false;
			break;
		}
		*eq = '\0';
		char *key = trim(line);
		char *val = unquote(trim(eq + 1));

		if (sec == SEC_MENU) {
			if (strcasecmp(key, "font") == 0)
				ok = set_str(cfg->font, val, lineno, key);
			else {
				log_msg("config: line %d: unknown key '%s' in [menu]", lineno, key);
				ok = false;
			}
		} else if (sec == SEC_CHANNEL) {
			if (strcasecmp(key, "id") == 0)
				ok = set_str(cur->id, val, lineno, key);
			else if (strcasecmp(key, "name") == 0)
				ok = set_str(cur->name, val, lineno, key);
			else if (strcasecmp(key, "image") == 0)
				ok = set_str(cur->image, val, lineno, key);
			else {
				log_msg("config: line %d: unknown key '%s' in [channel]", lineno, key);
				ok = false;
			}
		} else {
			log_msg("config: line %d: key '%s' outside of any section", lineno, key);
			ok = false;
		}
	}

	if (ferror(fp)) {
		log_msg("config: read error");
		ok = false;
	}
	fclose(fp);

	if (!ok)
		return false;

	for (int i = 0; i < cfg->count; i++) {
		if (cfg->ch[i].id[0] == '\0') {
			log_msg("config: [channel] at line %d has no id", cfg->ch[i].line);
			return false;
		}
	}

	if (cfg->count == 0) {
		log_msg("config: no [channel] entries");
		return false;
	}

	return true;
}

void config_resolve_path(const char *in, char *out, int outsize)
{
	if (strchr(in, ':'))
		snprintf(out, outsize, "%s", in);
	else
		snprintf(out, outsize, "%s%s", CONFIG_DIR, in[0] == '/' ? in + 1 : in);
}
