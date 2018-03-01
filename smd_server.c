/* smd server  */

#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <errno.h>
#include <stdlib.h>
#include <execinfo.h>

#include <apr_general.h>
#include <apr_hash.h>
#include <apr_strings.h>

/* MACRO */
#define CMD_SET     0
#define CMD_GET     1
#define CMD_SAVE	2
#define CMD_QUIT    3
#define CMD_UNKNOWN 4

#define SMD_ADD_EVENT   0
#define SMD_MOD_EVENT   1
#define SMD_DEL_EVENT   2

/* structures */
struct smd_event_loop;

typedef void smd_event_handler(int fd, void *data);

typedef struct smd_epoll_state {
    int epoll_fd;
    struct epoll_event *epoll_events;
} smd_epoll_state;

// To do : need to implement client instead of using client_data in smd_event just as a result buffer.
typedef struct client {
    int fd;
    apr_pool_t *client_mp;
    void *buf;
} client;

typedef struct smd_event {
    int mask;
    smd_event_handler *read_event_handler;
    smd_event_handler *write_event_handler;
    void *client_data;
} smd_event;

typedef struct smd_event_loop {
    smd_event *events;
    smd_epoll_state *epoll_state;
} smd_event_loop;


struct smd_server {
    int port;
    int tcp_backlog;    /* TCP listen backlog  */
    int fd;

    smd_event_loop *event_loop;

    apr_pool_t *memory_pool;
    apr_hash_t *hash_table;
};


/* function definition */
void set_event(int fd, int flag, smd_event_handler *read_handler, smd_event_handler *write_handler); 
int process_command(int fd, char *buf);

/* gobal varialbes */
struct smd_server server;


smd_event_loop *smd_create_event_loop(int size) {
    smd_event_loop *event_loop      = NULL;
    smd_epoll_state *epoll_state    = NULL;
    smd_event *event                = NULL;

    event_loop = (smd_event_loop *)apr_palloc(server.memory_pool, sizeof(smd_event_loop));
    if (event_loop == NULL) { 
        goto error;
    }

    /* epoll create */
    epoll_state = (smd_epoll_state *)apr_palloc(server.memory_pool, sizeof(smd_epoll_state));
    if (epoll_state == NULL) {
        goto error;
    }

    epoll_state->epoll_fd = epoll_create(size);
    if (epoll_state->epoll_fd == -1) {
        goto error;
    }

    epoll_state->epoll_events = (struct epoll_event*)apr_palloc(server.memory_pool, sizeof(struct epoll_event) * 10);

    event_loop->epoll_state = epoll_state;

    event = (smd_event *)apr_palloc(server.memory_pool, sizeof(smd_event)*1024);
    if (event == NULL) {
        goto error;
    }

    event_loop->events = event;

    return event_loop;

error:
    return NULL;
}

void init_server_config() {
    server.port         = 12345;
    server.tcp_backlog  = 512;
    
    server.event_loop   = NULL;

    server.memory_pool  = NULL;
    server.hash_table   = NULL;
}

int lookup_command(char *buf) {
    if (buf == NULL) return -1;

    if (strncasecmp("set", buf, 3) == 0) {
        return CMD_SET;
    } else if (strncasecmp("get", buf, 3) == 0) {
        return CMD_GET;
    } else if (strncasecmp("save", buf, 4) == 0) {
        return CMD_SAVE;
    } else if (strncasecmp("quit", buf, 4) == 0) {
        return CMD_QUIT;
    }

    return CMD_UNKNOWN;
}

void smd_set_value(char *key, void *value) {
    apr_hash_set(server.hash_table, apr_pstrdup(server.memory_pool, key), APR_HASH_KEY_STRING, apr_pstrdup(server.memory_pool, value));
}

void *smd_get_value(char *key) {
    return apr_hash_get(server.hash_table, key, APR_HASH_KEY_STRING);
}

void read_query_from_client(int fd, void *data) {
    int nread;
    char buf[1024] ={0,};

    nread = read(fd, buf, 1023);
    if (nread == -1) {
        printf("read error: %s\n", strerror(errno));
        return;
    } else if (nread == 0) { // connection closed
        return;
    }

    process_command(fd, buf);  
}

void send_result_to_client(int fd, void *data) {
    int nsend;

    if (data == NULL) return;

    nsend = write(fd, data, strlen(data));
    if (nsend == -1) {
        printf("send error: %s\n", strerror(errno));
        return;
    }
}

int save_data_to_file() {
	apr_hash_index_t *hi;
	void *key, *val;
	FILE *fp;

	fp = fopen("smd_data", "w+");
	if (!fp) return -1;

	hi = apr_hash_first(server.memory_pool, server.hash_table);
	while (hi) {
		val = NULL;
		apr_hash_this(hi, (const void**)&key, NULL, &val);

		if (key && val)
			fprintf(fp, "set %s %s\n", (char*)key, (char*)val);

		hi = apr_hash_next(hi);
	}
	fclose(fp);

	return 0;
}

void load_data_from_file() {
	FILE *fp;
	char *line = NULL;
	size_t len = 0;

	fp = fopen("smd_data", "r");
	if (!fp) return;

	while (getline(&line, &len, fp) != -1) {
		int cmd;
		char *command, *key, *value;
		char *ptr;

		command = strtok_r(line, " ", &ptr);
		key = strtok_r(NULL, " ", &ptr);

		cmd = lookup_command(command);
		if (cmd == -1) {
			printf("Invalid Command\n");
			continue;
		}

		switch (cmd) {
			case CMD_SET:
				value = strtok_r(NULL, " ", &ptr);
				smd_set_value(key, value);
				break;
		}
	}
	if (line) free(line);

	printf("smd_data file successfully loaded\n");
	return;
}

int smd_save() {
	int pid;

	pid = fork();
	if (pid == -1) {
		return -1;
	} else if (pid == 0) {
		// child
		save_data_to_file();	
		exit(0);
	}

	return 0;
}

int process_command(int fd, char *buf) {
    int cmd;
    void *data;
    char *command, *key, *value;
    char *ptr;

    if (buf == NULL) return -1;

    command = strtok_r(buf, " \n", &ptr);
    key = strtok_r(NULL, " \n", &ptr);

    cmd = lookup_command(command);
    if (cmd == -1) {
        printf("Invalid Command\n");
        return -1;
    }

    smd_event *e = &server.event_loop->events[fd];

	if (cmd == CMD_SET || cmd == CMD_GET) {
		if (key == NULL) {
			e->client_data = apr_psprintf(server.memory_pool, "%s", "Key is empty");
			return -1;
		}
	}

    switch (cmd) {
        case CMD_SET:
            value = strtok_r(NULL, " \n", &ptr);
			if (value == NULL) {
				e->client_data = apr_psprintf(server.memory_pool, "%s", "Value is empty");
				return -1;
			}
            smd_set_value(key, value);
            e->client_data = apr_psprintf(server.memory_pool, "%s", "set command OK");
            break;
        case CMD_GET:
            data = smd_get_value(key);
            e->client_data = apr_psprintf(server.memory_pool, "%s", data?(char*)data : "");
            break;
        case CMD_SAVE:
			smd_save();
            e->client_data = apr_psprintf(server.memory_pool, "%s", "save success");
            break;
        case CMD_QUIT:
            send_result_to_client(fd, "closing connection...");
            set_event(fd, SMD_DEL_EVENT, NULL, NULL);
            break;
        case CMD_UNKNOWN:
            e->client_data = apr_psprintf(server.memory_pool, "%s", "unknown command");
            break;
        default:
            return -1;
    }

    return 0;
}


void accept_handler(int fd, void *data) {
    socklen_t addr_len;
    int client_fd;
    struct sockaddr_in client_addr;

    addr_len = sizeof(client_addr);

    client_fd = accept(server.fd, (struct sockaddr*)&client_addr, &addr_len);
    if (client_fd == -1) {
        goto error;
    }

    /* add event  */
    set_event(client_fd, SMD_ADD_EVENT, read_query_from_client, send_result_to_client);

    return;
error:
    printf("Error: %s\n", strerror(errno));
    apr_pool_destroy(server.memory_pool);
    apr_terminate();
    exit(1);
}

void init_server_socket() {
    struct sockaddr_in server_addr;

    /* TCP listen  */
    if ((server.fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        goto error;
    }

    memset((void*)&server_addr, 0x00, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(server.port);


    if (bind(server.fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        goto error;
    }

    if (listen(server.fd, server.tcp_backlog) == -1) {
        goto error;
    }
    
    return;

error:
    printf("Error: %s\n", strerror(errno));
    apr_pool_destroy(server.memory_pool);
    apr_terminate();
    exit(1);
}

void set_event(int fd, int flag, smd_event_handler *read_handler, smd_event_handler *write_handler) {
    int op;
    struct epoll_event ee = {0};
    smd_epoll_state *state = server.event_loop->epoll_state;

    ee.events = 0;
    ee.events |= EPOLLIN;

    if (flag == SMD_ADD_EVENT) op = EPOLL_CTL_ADD;
    else if (flag == SMD_MOD_EVENT) op = EPOLL_CTL_MOD;
    else if (flag == SMD_DEL_EVENT) op = EPOLL_CTL_DEL;

    ee.data.fd = fd;
    if (epoll_ctl(state->epoll_fd, op, fd, &ee) == -1) {
        goto error;
    }
    
    smd_event *e = &server.event_loop->events[fd];

    e->read_event_handler = read_handler;
    e->write_event_handler = write_handler;
    e->client_data = NULL;

    return;
error:
    printf("Error: %s\n", strerror(errno));
    apr_pool_destroy(server.memory_pool);
    apr_terminate();
    exit(1);
}

void handler(int sig) {
	void *array[10];
	size_t size;

	size = backtrace(array, 10);

	fprintf(stderr, "Error: signal %d\n", sig);
	backtrace_symbols_fd(array, size, STDERR_FILENO);
	exit(1);
}

void set_signal() {
	signal(SIGSEGV, handler);
}

void init_server() {

	set_signal();

    init_server_config();

    apr_initialize();

    apr_pool_create(&server.memory_pool, NULL);

    server.hash_table = apr_hash_make(server.memory_pool);

    server.event_loop = smd_create_event_loop(1024);
    if (server.event_loop == NULL) {
        goto error;
    }

    init_server_socket();

    /* add event  */
    set_event(server.fd, SMD_ADD_EVENT, accept_handler, NULL);

	load_data_from_file();
    return;
error:
    printf("Error: %s\n", strerror(errno));
    apr_pool_destroy(server.memory_pool);
    apr_terminate();
    exit(1);
}

void run() {
    int num_events, i;
    smd_epoll_state *state = NULL;
    
    while (1) {
        state = server.event_loop->epoll_state;
    
        num_events = epoll_wait(state->epoll_fd, (struct epoll_event*)state->epoll_events, 10, -1);

        if (num_events == -1) {
            printf("Error: %s\n", strerror(errno));
            exit(1);
        }

        for (i=0; i<num_events; i++) {
            struct epoll_event *ee = &state->epoll_events[i];
            smd_event *e = &server.event_loop->events[ee->data.fd];

            if (e->read_event_handler) {
                e->read_event_handler(ee->data.fd, e->client_data);
            }

            if (e->write_event_handler) {
                e->write_event_handler(ee->data.fd, e->client_data);
            }
        }
    }
}

void destroy_server() {
    apr_pool_destroy(server.memory_pool);
    apr_terminate();
}

int main(int argc, char **argv) {
    init_server();

    /* running loop  */
    run(server.event_loop);

    /* free memory */
    destroy_server();

    return 0;
}

