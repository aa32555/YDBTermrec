#include "termrec.h"

void parse_args(int argc, char **argv, struct arguments *arguments)
{
        arguments->record = 1;
        arguments->session = NULL;
        arguments->key_playback = 0;

        argp_parse (&argp, argc, argv, 0, 0, arguments);
}
