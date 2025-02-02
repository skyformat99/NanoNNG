#include "nng/supplemental/nanolib/hocon.h"
#include "nng/supplemental/nanolib/cvector.h"
#include "stdbool.h"
#include "stdio.h"

static char *
skip_whitespace(char *str)
{
	if (NULL == str) {
		return NULL;
	}

	// Skip white_space and tab with bounds check
	while ('\0' != *str && (' ' == *str || '\t' == *str)) {
		str++;
	}

	return str;
}

static char *
skip_whitespace_reverse(char *str)
{
	if (NULL == str) {
		return NULL;
	}

	// Skip white_space and tab with bounds check
	while ('\0' != *str && (' ' == *str || '\t' == *str)) {
		str--;
	}

	return str;
}

// TODO incomplete, if the comment appears after the string
static bool
is_comment_line(char *line)
{
	while ('\0' != *line && '\n' != *line) {
		line = skip_whitespace(line);
		if ('#' == *line || ('/' == *line && '/' == *(line + 1))) {
			return true;
		}

		return false;
	}
	// skip blank line
	return true;
}

static bool
is_not_brackets(char *s)
{
	return ('{' != *s && '}' != *s && ']' != *s && '[' != *s);
}

static char *
data_preprocessing(char *str)
{
	char *ret = NULL;
	char *p   = str;
	char *p_b = str;

	while (NULL != (p = strchr(p, '\n'))) {
		// skip comment
		if (true == is_comment_line(p_b)) {
			p++;
			p_b = p;
		} else {
			// push one line
			for (; p != p_b; p_b++) {
				cvector_push_back(ret, *p_b);
			}

			// find last non blank character
			// judge if we should append ','
			char *t = skip_whitespace_reverse(p_b - 1);
			if (is_not_brackets(t) && ',' != *t) {
				char *q = skip_whitespace(p + 1);
				if (is_not_brackets(q)) {
					cvector_push_back(ret, ',');
				}
			} else if (']' == *t) {
				if ('\0' != *(p + 1) && '}' != *(p + 1)) {
					cvector_push_back(ret, ',');
				}
			}
			p++;
			p_b = p;
		}
	}

	cvector_push_back(ret, '\0');
	return ret;
}

static cJSON *
path_expression_parse_core(cJSON *parent, cJSON *jso)
{
	jso       = cJSON_DetachItemFromObject(parent, jso->string);
	char *str = strdup(jso->string);
	char *p   = str;
	char *p_a = str + strlen(str);

	char t[128] = { 0 };
	while (NULL != (p = strrchr(str, '.'))) {
		cJSON *jso_new = cJSON_CreateObject();
		// cJSON *jso_new = NULL;

		// a.b.c: {object}
		// c ==> create json object jso(c, jso)
		*p = '_';
		strncpy(t, p + 1, p_a - p);
		// cJSON_AddItemToObject(jso_new, t, jso);
		cJSON_AddItemToObject(
		    jso_new, t, jso); // cJSON_Duplicate(jso, cJSON_True));
		memset(t, 0, 128);
		// jso_new = json(c, jso)
		// cJSON_Delete(jso);
		jso     = jso_new;
		jso_new = NULL;
		p_a     = --p;
	}

	strncpy(t, str, p_a - str + 1);
	cJSON_AddItemToObject(parent, t, jso);
	// memset(t, 0, 128);
	// cJSON_DeleteItemFromObject(parent, str);

	free(str);
	return parent;
}

// {"bridge.sqlite":{"enable":false,"disk_cache_size":102400,"mounted_file_path":"/tmp/","flush_mem_threshold":100,"resend_interval":5000}}
// {"bridge":{"sqlite":{"enable":false,"disk_cache_size":102400,"mounted_file_path":"/tmp/","flush_mem_threshold":100,"resend_interval":5000}}}

// level-order traversal
// find key bridge.sqlite
// create object sqlite with object value
// insert object bridge with sqlite
// delete bridge.sqlite

static cJSON *
path_expression_parse(cJSON *jso)
{
	cJSON *parent = jso;
	cJSON *child  = jso->child;

	while (child) {
		if (child->child) {
			path_expression_parse(child);
		}
		if (NULL != child->string &&
		    NULL != strchr(child->string, '.')) {
			path_expression_parse_core(parent, child);
			child = parent->child;
		} else {
			child = child->next;
		}
	}

	return jso;
}

// if same level has same name, if they are not object
// the back covers the front
// TODO FIXME memory leak
cJSON *
deduplication_and_merging(cJSON *jso)
{
	cJSON  *parent = jso;
	cJSON  *child  = jso->child;
	cJSON **table  = NULL;

	while (child) {
		for (size_t i = 0; i < cvector_size(table); i++) {
			if (table[i] && child && table[i]->string &&
			    child->string &&
			    0 == strcmp(table[i]->string, child->string)) {
				if (table[i]->type == child->type &&
				    cJSON_Object == table[i]->type) {
					// merging object
					cJSON *next = table[i]->child;
					while (next) {
						cJSON *dup = cJSON_Duplicate(
						    next, true);
						cJSON_AddItemToObject(child,
						    dup->string,
						    dup); // cJSON_Duplicate(next,
						          // cJSON_True));
						// cJSON_AddItemToObject(child,
						// next->string, next);
						// //cJSON_Duplicate(next,
						// cJSON_True)); cJSON *free =
						// next;
						next = next->next;
						// cJSON_DetachItemFromObject(table[i],
						// free->string);
					}

					cJSON_DeleteItemFromObject(
					    parent, table[i]->string);
					cvector_erase(table, i);

				} else {
					if (0 == i) {
						parent->child = child;
						cJSON_free(table[i]);
						cvector_erase(table, i);

					} else {
						cJSON *free =
						    table[i - 1]->next;
						table[i - 1]->next =
						    table[i - 1]->next->next;
						cvector_erase(table, i);
						cJSON_free(free);
					}
				}
			}
		}

		cvector_push_back(table, child);

		if (child->child) {
			deduplication_and_merging(child);
		}
		child = child->next;
	}
	cvector_free(table);
	return jso;
}
static char *
remove_error_add(char *data)
{
	// remove ,"}
	if ('}' == data[cvector_size(data) - 1] &&
	    '"' == data[cvector_size(data) - 2] &&
	    ',' == data[cvector_size(data) - 3]) {
		data[cvector_size(data) - 3] = data[cvector_size(data) - 1];
		cvector_pop_back(data);
		cvector_pop_back(data);
	}

	if (']' == data[cvector_size(data) - 1] &&
	    '"' == data[cvector_size(data) - 2] &&
	    ',' == data[cvector_size(data) - 3]) {
		data[cvector_size(data) - 3] = data[cvector_size(data) - 1];
		cvector_pop_back(data);
		cvector_pop_back(data);
	}
	return data;
}

// This function assumes that the value
// is enclosed in double quotes
static char *
read_value(char **data, char *p)
{
	if ('"' == *p && '=' == *(p - 1)) {
		cvector_push_back((*data), *p++);
		while ('\0' != *p) {
			if ('"' == *p && '\\' != *(p - 1)) {
				break;
			}
			cvector_push_back((*data), *p++);
		}
		cvector_push_back((*data), *p++);
	}
	return p;
}

// Replace all '=' to ':'
// If there are no '=' before object, add ':'
// If first non-blank character is not '{', push-front '{' and push
// push-back '}' Replace key to \"key\"
cJSON *
hocon_str_to_json(char *str)
{
	if (NULL == str) {
		return NULL;
	}

	str = data_preprocessing(str);

	// If it's not an illegal json object return
	cJSON *jso = cJSON_Parse(str);
	if (cJSON_False == cJSON_IsInvalid(jso)) {
		cvector_free(str);
		return jso;
	}

	char *p    = str;
	char *data = NULL;
	cvector_push_back(data, '{');
	cvector_push_back(data, '"');

	while ('\0' != *p && NULL != (p = skip_whitespace(p))) {
		while (' ' != *p && '\0' != *p) {
			// read value enclosed in double quotes
			p = read_value(&data, p);
			p = skip_whitespace(p);

			// begin key
			// push ',' after '}', if last is object finish.
			// push '"' before key, if next is not an object.
			// example: '},"key'
			if ('}' == data[cvector_size(data) - 1] && ',' != *p) {
				cvector_push_back(data, ',');
				if ('{' != *p) {
					cvector_push_back(data, '"');
				}
			}

			// end key
			// push '":' if last is not object/array begin
			//
			if (('{' == *p &&
			        ':' != data[cvector_size(data) - 1] &&
			        '[' != data[cvector_size(data) - 1] &&
			        '}' != data[cvector_size(data) - 2]) ||
			    ('[' == *p &&
			        ':' != data[cvector_size(data) - 1])) {
				cvector_push_back(data, '"');
				cvector_push_back(data, ':');
			}

			// replace '=' to ':' and push value
			if ('=' == *p) {
				cvector_push_back(data, '"');
				cvector_push_back(data, ':');
			} else if (',' == *p || '{' == *p) {
				cvector_push_back(data, *p);
				// TODO FIXME unsafe
				if ('}' != *(p + 1) && '"' != *(p + 1) &&
				    '{' != *(p + 1)) {
					cvector_push_back(data, '"');
				}
			} else {
				cvector_push_back(data, *p);
			}

			data = remove_error_add(data);
			p++;
		}
	}

	cvector_free(str);
	cvector_push_back(data, '}');
	cvector_push_back(data, '\0');

	// puts("\n");
	// puts(data);

	if ((jso = cJSON_Parse(data))) {
		if (cJSON_False != cJSON_IsInvalid(jso)) {
			jso = path_expression_parse(jso);
			// puts("\n");
			// char *tmp = cJSON_PrintUnformatted(jso);
			// puts(tmp);
			// cJSON_free(tmp);
			cvector_free(data);
			return deduplication_and_merging(jso);
		}
	}

	cvector_free(data);
	return NULL;
}