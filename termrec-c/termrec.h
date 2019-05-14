#ifndef TERMREC_H
#define TERMREC_H

#include <argp.h>

#define INIT_YDB_BUFFER(buffer, len) (buffer)->buf_addr = malloc(len); (buffer)->len_used = 0; (buffer)->len_alloc = len;
#define BUFFER_SIZE 32768

static char doc[] =
        "Records a terminal session and later plays it back";

static char args_doc[] =
        "ARG1 ARG2";

static struct argp_option options[] = {
        {"record", 'r', 0, 0, "Records the given terminal session"},
        {"playback", 'p', "session", 0, "Playback the specified session"},
        {"key", 'k', 0, 0, "Advance playback by pressing keys"},
        { 0 }
};

struct arguments {
        char *session;
	int key_playback;
        int record;
};

static error_t parse_opt(int key, char *arg, struct argp_state *state);

static struct argp argp = { options, parse_opt, args_doc, doc };

int extract_string(char *in, char **function_name, int *fd, char **string);
void parse_args(int argc, char **argv, struct arguments *arguments);
void record_session();
void playback_session(char *session, struct arguments *arguments);

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
        /* Get the input argument from argp_parse, which we
        know is a pointer to our arguments structure. */
        struct arguments *arguments = state->input;

        switch (key)
        {
        case 'r':
                arguments->record = 1;
                break;
        case 'p':
                arguments->session = arg;
                arguments->record = 0;
                break;
	case 'k':
		arguments->key_playback = 1;
		break;
        default:
                return ARGP_ERR_UNKNOWN;
        }
        return 0;
}

#endif
