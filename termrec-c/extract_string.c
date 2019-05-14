#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "termrec.h"

enum ExtractStringState {
        BEGIN,
        PAREN,
        STRING,
	SQUARE_BRACE
};


int extract_string(char *in, char **function_name, int *fd, char **string)
{
	char *c, *d, *ret;
	char *token_start = NULL, *token_end = NULL;
	int str_len, v;
	enum ExtractStringState state = BEGIN;
	*string = NULL;
	for(token_end = token_start = c = in; *c != '\0'; c++)
	{
		switch(state)
		{
		case BEGIN:
			switch(*c) {
			case '[':
				state = SQUARE_BRACE;
				break;
			case '(':
				state = PAREN;
				// Copy token_start - token_end to *function_name
				str_len = token_end - token_start;
				assert(str_len > 0);
				(*function_name) = malloc(str_len + 2);
				memcpy(*function_name, token_start, str_len + 1);
				(*function_name)[str_len + 1] = '\0';
				token_start = c+1;
				break;
			default:
				token_end = c;
				break;
			}
			break;
		case SQUARE_BRACE:
			switch (*c) {
			case ']':
				state = BEGIN;
				// Make sure c points to the first character not a string
				c++;
				token_start = c+1;
			default:
				break;
			}
			break;
		case PAREN:
			switch(*c) {
				case ',':
					// we sould have the file descriptor now
					assert(*(c+1) == ' ');
					*c = '\0';
					*fd = atoi(token_start);
					break;
				case '"':
					state = STRING;
					token_start = c+1;
					token_end = c+1;
					break;
				default:
					token_end = c;
					break;
			}
			break;
		case STRING:
			switch(*c) {
				case '"':
					str_len = token_end - token_start;
					assert(*(token_start-1) == '"');
					assert(str_len >= 0);
					if(str_len == 0)
					{
						(*string) = malloc(1);
						(*string)[0] = '\0';
						return 0;
					}
					(*string) = malloc(str_len + 1);
					for(ret = (*string), d = token_start; d < token_end; d++)
					{
						assert(*d == '\\');
						d++;
						assert(*d == 'x');
						d++;
						if(*d >= 48 && *d <= 57)
							*d = (char)(*d - 48);
						else if(*d >= 97 && *d <= 102)
							*d = (char)(*d - (97 - 10));
						else
							assert(0);
						v = *d * 16;
						*d++;
						if(*d >= 48 && *d <= 57)
							*d = (char)(*d - 48);
						else if(*d >= 97 && *d <= 102)
							*d = (char)(*d - (97 - 10));
						else
							assert(0);
						v += *d;
						assert(v >= 0 && v <= 0xff);
						*ret++ = v;
					}
					*ret = '\0';
					return 0;
					break;
				default:
					token_end = c;
					break;
			}
			break;
		}
	}
	return 1;
}

