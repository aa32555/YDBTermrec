#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <argp.h>
#include <unistd.h>

#include <termios.h>

#include <libyottadb.h>

#include "termrec.h"

int main(int argc, char **argv)
{
	struct arguments arguments;
	struct termios t, old_t;

	parse_args(argc, argv, &arguments);

	if(arguments.key_playback)
	{
		tcgetattr(STDIN_FILENO, &t);
		old_t = t;
		t.c_lflag &= ~ECHO;
		t.c_lflag &= ~ICANON;
		tcsetattr(STDIN_FILENO, TCSANOW, &t);
	}

	if(arguments.record)
	{
		record_session();
	} else {
		assert(arguments.session != NULL);
		playback_session(arguments.session, &arguments);
	}

	if(arguments.key_playback)
		tcsetattr(STDIN_FILENO, TCSANOW, &old_t);

	return 0;
}

void record_session()
{
	char *func = NULL, *value = NULL, in[BUFFER_SIZE], *ret;
	int fd, status, done;
	struct timespec t_message = {0, 0};

	ydb_buffer_t global_name, latest_session_id;
	ydb_buffer_t session[5];

	INIT_YDB_BUFFER(&session[0], BUFFER_SIZE);
	INIT_YDB_BUFFER(&session[1], BUFFER_SIZE);
	INIT_YDB_BUFFER(&session[2], BUFFER_SIZE);
	INIT_YDB_BUFFER(&session[3], BUFFER_SIZE);
	INIT_YDB_BUFFER(&session[4], BUFFER_SIZE);

	YDB_LITERAL_TO_BUFFER("^sessions", &global_name);
	YDB_LITERAL_TO_BUFFER("id", &latest_session_id);

	status = ydb_incr_s(&global_name, 1, &latest_session_id, NULL, &session[0]);
	YDB_ASSERT(status == YDB_OK);

	session[0].buf_addr[session->len_used] = '\0';
	printf("Recording session %s\n", session[0].buf_addr);

	while(1)
	{
		do {
			ret = fgets(in, BUFFER_SIZE, stdin);
		} while(NULL == ret && !feof(stdin) && ferror(stdin) && EINTR == errno);

		if(ret == NULL)
			break;

		status = extract_string(in, &func, &fd, &value);
		if(status)
		{
			if(func)
			{
				free(func);
				func = NULL;
			}
		}
		clock_gettime(CLOCK_MONOTONIC, &t_message);
		session[1].len_used = snprintf(session[1].buf_addr, BUFFER_SIZE, "%ld", t_message.tv_sec);
		session[2].len_used = snprintf(session[2].buf_addr, BUFFER_SIZE, "%ld", t_message.tv_nsec);

		YDB_COPY_STRING_TO_BUFFER("fd", &session[3], done);
		session[4].len_used = snprintf(session[4].buf_addr, BUFFER_SIZE, "%d", fd);
		status = ydb_set_s(&global_name, 4, session, &session[4]);
		YDB_ASSERT(status == YDB_OK);

		YDB_COPY_STRING_TO_BUFFER("value", &session[3], done);
		session[4].len_used = snprintf(session[4].buf_addr, BUFFER_SIZE, "%s", value);
		status = ydb_set_s(&global_name, 4, session, &session[4]);
		YDB_ASSERT(status == YDB_OK);

		YDB_COPY_STRING_TO_BUFFER("func", &session[3], done);
		session[4].len_used = snprintf(session[4].buf_addr, BUFFER_SIZE, "%s", func);
		status = ydb_set_s(&global_name, 4, session, &session[4]);
		YDB_ASSERT(status == YDB_OK);

		free(func);
		func = NULL;
		free(value);
		value = NULL;
	}
}

void playback_session(char *session_id, struct arguments *arguments)
{
	char *func = NULL, *value = NULL, in[BUFFER_SIZE], *ret;
	int fd, status, done, subs_used = 4;
	struct timespec t_last={0, 0}, t_current={0, 0}, t_sleep, t_remain;
	ydb_buffer_t global_name;
	ydb_buffer_t session[5], session_id_buffer;

	INIT_YDB_BUFFER(&session[0], BUFFER_SIZE);
	YDB_COPY_STRING_TO_BUFFER(session_id, &session[0], done);
	INIT_YDB_BUFFER(&session[1], BUFFER_SIZE);
	INIT_YDB_BUFFER(&session[2], BUFFER_SIZE);
	INIT_YDB_BUFFER(&session[3], BUFFER_SIZE);
	INIT_YDB_BUFFER(&session[4], BUFFER_SIZE);

	YDB_LITERAL_TO_BUFFER("^sessions", &global_name);

	INIT_YDB_BUFFER(&session_id_buffer, BUFFER_SIZE);
	YDB_COPY_BUFFER_TO_BUFFER(&session[0], &session_id_buffer, done);

	status = ydb_node_next_s(&global_name, 1, session, &subs_used, session);
	YDB_ASSERT(status == YDB_OK);
	while(status == YDB_OK && YDB_BUFFER_IS_SAME(&session_id_buffer, &session[0]))
	{
		status = ydb_get_s(&global_name, 4, session, &session[4]);
		YDB_ASSERT(status == YDB_OK);
		session[4].buf_addr[session[4].len_used] = '\0';
		fd = atoi(session[4].buf_addr);

		status = ydb_node_next_s(&global_name, 4, session, &subs_used, session);
		YDB_ASSERT(status == YDB_OK);
		status = ydb_get_s(&global_name, 4, session, &session[4]);
		YDB_ASSERT(status == YDB_OK);
		session[4].buf_addr[session[4].len_used] = '\0';
		func = malloc(session[4].len_used + 1);
		snprintf(func, session[4].len_used + 1, "%s", session[4].buf_addr);

		status = ydb_node_next_s(&global_name, 4, session, &subs_used, session);
		YDB_ASSERT(status == YDB_OK);
		status = ydb_get_s(&global_name, 4, session, &session[4]);
		YDB_ASSERT(status == YDB_OK);
		session[4].buf_addr[session[4].len_used] = '\0';
		value = malloc(session[4].len_used + 1);
		snprintf(value, session[4].len_used + 1, "%s", session[4].buf_addr);

		if(strcmp("write", func) == 0 && (fd == 1 || fd == 2))
		{
			session[1].buf_addr[session[1].len_used] = '\0';
			t_current.tv_sec = strtoll(session[1].buf_addr, NULL, 10);
			session[2].buf_addr[session[2].len_used] = '\0';
			t_current.tv_nsec = strtoll(session[2].buf_addr, NULL, 10);

			if(t_last.tv_sec != 0 && t_last.tv_nsec != 0)
			{
				t_sleep.tv_sec = t_current.tv_sec - t_last.tv_sec;
				t_sleep.tv_nsec = t_current.tv_nsec - t_last.tv_nsec;
				while (-1 == nanosleep(&t_sleep, &t_remain)) {
					if (EINTR == errno)
						t_sleep = t_remain;
					else
						break;
				}
			}

			t_last = t_current;
			write(stdout, "%s", value);
			fflush(stdout);
		}
		free(value);
		value = NULL;
		free(func);
		func = NULL;
		status = ydb_node_next_s(&global_name, 4, session, &subs_used, session);

	}
	YDB_ASSERT(status == YDB_OK);
}
