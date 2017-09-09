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

#include <apr_general.h>
#include <apr_hash.h>
#include <apr_strings.h>

#define CMD_SET 0
#define CMD_GET 1
#define CMD_UNKNOWN 2


struct smd_event_loop;

typedef void smd_event_handler(int fd, void *data);

typedef struct smd_epoll_state {
    int epoll_fd;
    struct epoll_event *epoll_events;
} smd_epoll_state;

typedef struct smd_event {
    int mask;
    smd_event_handler *read_event_handler;
    smd_event_handler *write_event_handler;
    void *client_data;
} smd_event;

typedef struct smd_event_loop {
    smd_event *events;
    smd_epoll_state *epoll_state;
//    smd_event_handler *handler;
} smd_event_loop;





struct smd_server {
    int port;
    int tcp_backlog;    /* TCP listen backlog  */
    int fd;

    smd_event_loop *event_loop;

    apr_pool_t *memory_pool;
    apr_hash_t *hash_table;
};

struct smd_server server;

smd_event_loop *smd_create_event_loop(int size) {
    smd_event_loop *event_loop      = NULL;
    smd_epoll_state *epoll_state    = NULL;
    smd_event *event                = NULL;

    event_loop = (smd_event_loop *)apr_palloc(server.memory_pool, sizeof(smd_event_loop));
    if (event_loop == NULL) 
        goto error;

    //event_loop->handler = NULL;

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
    if (event_loop)
        free(event_loop);

    if (epoll_state)
        free(epoll_state);

    if (event)
        free(event);

    return NULL;
}

static void init_server_config() {
    server.port         = 12345;
    server.tcp_backlog  = 512;
    
    server.event_loop   = NULL;

    server.memory_pool  = NULL;
    server.hash_table   = NULL;
}

int lookup_command(char *buf) {
    if (buf == NULL) return CMD_UNKNOWN;

    if (strncasecmp("set", buf, 3) == 0) {
        return CMD_SET;
    } else if (strncasecmp("get", buf, 3) == 0) {
        return CMD_GET;
    } 

    return CMD_UNKNOWN;
}

void smd_set_value(char *key, void *value) {
    apr_hash_set(server.hash_table, key, APR_HASH_KEY_STRING, apr_pstrdup(server.memory_pool, value));
    printf("%s():%d  \n", __FUNCTION__, __LINE__);
}

void *smd_get_value(char *key) {
    return apr_hash_get(server.hash_table, key, APR_HASH_KEY_STRING);
}


int process_command(int fd, char *buf) {
    int cmd;
    void *data;
    /* To do : need to implement strtok */

    cmd = lookup_command(buf);
    if (cmd == -1) {
        printf("Invalid Command\n");
        // To do : send client error message
        return -1;
    }

    switch (cmd) {
        case CMD_SET:
            // XXXX : need to fix here, for now use + 4 first
            smd_set_value("foo", "bar");
            break;
        case CMD_GET:
            // XXXX : need to fix here, for now use + 4 first
            data = smd_get_value("foo");
            /* send data to client*/
            printf("%s():%d data: %s \n", __FUNCTION__, __LINE__, (char*)data);
            break;
        case CMD_UNKNOWN:
        default:
            return -1;
    }

    return 0;
}

void read_query_from_client(int fd, void *data) {
    int nread;
    char buf[1024] ={0,};

    printf("%s():%d  \n", __FUNCTION__, __LINE__);
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

    nsend = write(fd, data, strlen(data));
    if (nsend == -1) {
        printf("send error: %s\n", strerror(errno));
        return;
    }

}

void accept_handler(int fd, void *data) {
    int addr_len;
    int client_fd;
    struct sockaddr_in client_addr;

    printf("%s():%d  \n", __FUNCTION__, __LINE__);
    addr_len = sizeof(client_addr);

    client_fd = accept(server.fd, (struct sockaddr*)&client_addr, &addr_len);
    printf("%s():%d, client_fd: %d  \n", __FUNCTION__, __LINE__, client_fd);

    if (client_fd == -1) {
        printf("Error: %s\n", strerror(errno));
        exit(1);
    }

    /* add event  */
    smd_epoll_state *state = server.event_loop->epoll_state;
    struct epoll_event ee = {0};

    ee.events = 0;
    ee.events |= EPOLLIN | EPOLLET;

    ee.data.fd = client_fd;
    if (epoll_ctl(state->epoll_fd, EPOLL_CTL_ADD, client_fd, &ee) == -1) {
        printf("Error: %s\n", strerror(errno));
        exit(1);
    }
    
    smd_event *e = &server.event_loop->events[client_fd];

    e->read_event_handler = read_query_from_client;
    e->write_event_handler = send_result_to_client;
    e->client_data = NULL;

    printf("Accept !!!\n");
}

static void init_server() {
    struct sockaddr_in server_addr;

    apr_initialize();
    apr_pool_create(&server.memory_pool, NULL);

    server.hash_table = apr_hash_make(server.memory_pool);

    server.event_loop = smd_create_event_loop(1024);
    if (server.event_loop == NULL) {
        printf("Error: %s\n", strerror(errno));
        exit(1);
    }

    /* TCP listen  */
    if ((server.fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("Error: %s\n", strerror(errno));
        exit(1);
    }

    memset((void*)&server_addr, 0x00, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(server.port);


    if (bind(server.fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        printf("Error: %s\n", strerror(errno));
        exit(1);
    }

    if (listen(server.fd, server.tcp_backlog) == -1) {
        printf("Error: %s\n", strerror(errno));
        exit(1);
    }

    


    /* add event  */
    smd_epoll_state *state = server.event_loop->epoll_state;
    struct epoll_event ee = {0};

    ee.events = 0;
    ee.events |= EPOLLIN | EPOLLET;

    ee.data.fd = server.fd;
    if (epoll_ctl(state->epoll_fd, EPOLL_CTL_ADD, server.fd, &ee) == -1) {
        printf("Error: %s\n", strerror(errno));
        exit(1);
    }
    
    smd_event *e = &server.event_loop->events[server.fd];

    e->read_event_handler = accept_handler;
    e->client_data = NULL;

}

void write_handler(smd_event_loop *el) {
    printf("write handler called !!!\n");
}

void run(smd_event_loop *ev) {
    int num_events, i;
    smd_epoll_state *state = NULL;
    
    while (1) {
        //ev->handler(ev);

        state = ev->epoll_state;
    
        printf("%s():%d  state->epoll_fd: %d\n", __FUNCTION__, __LINE__, state->epoll_fd);
        num_events = epoll_wait(state->epoll_fd, (struct epoll_event*)state->epoll_events, 10, -1);

        if (num_events == -1) {
            printf("%s():%d  \n", __FUNCTION__, __LINE__);
            printf("Error: %s\n", strerror(errno));
            exit(1);
        }

        printf("%s():%d, ret:%d  \n", __FUNCTION__, __LINE__, num_events);
        for (i=0; i<num_events; i++) {
            struct epoll_event *ee = &state->epoll_events[i];
            smd_event *e = &ev->events[ee->data.fd];
            printf("%s():%d ee->data.fd:%d, server.fd: %d \n", __FUNCTION__, __LINE__, ee->data.fd, server.fd);

            printf("read_file_proc called\n");
            e->read_event_handler(ee->data.fd, e->client_data);

            //e->write_event_handler(ee->data.fd, e->client_data);
        }
    }
}

int main(int argc, char **argv) {
    init_server_config();

    init_server();

    /* add handler  */
    //server.event_loop->handler = write_handler;
    /* running loop  */
    run(server.event_loop);

    /* free memory */
    apr_pool_destroy(server.memory_pool);

    apr_terminate();

    return 0;
}

