#define _DEFAULT_SOURCE

#include <stdlib.h>
#include <string.h>

#include "composite_str.h"

#define KW_WORD_TAG		0x75

char* get_cs_string(composite_str* cs)
{
	return cs->short_str.flag == KW_WORD_TAG ? cs->short_str.s : cs->p;
}

int set_cs_string(composite_str* cs, const char* s)
{
	int long_str = strlen(s) >= sizeof(cs->short_str.s);
	cs->p = 0;
	if (long_str)
		cs->p = strdup(s);
	else {
		cs->short_str.flag = KW_WORD_TAG;
		strcpy(cs->short_str.s, s);
	}
	
	if (cs->p == 0)
		return CS_SET_STR_FAIL;

	return long_str ? CS_LONG_STR : CS_SHORT_STR;
}

void free_composite_str(composite_str *cs)
{
	if (cs->p != 0 && cs->short_str.flag != KW_WORD_TAG)
		free(cs->p);
}

