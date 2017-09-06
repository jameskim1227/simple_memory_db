/* SMD CLI (Command Line Interface) */

#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>



typedef struct smd_context {
    int fd;

    struct {
        char *host_ip;
        int port;
    } tcp;
} smd_context;


static smd_context *smd_context_init(const char *host_ip, const char *host_port) {
    smd_context *ctx;

    ctx = (smd_context *)malloc(sizeof(smd_context));

    memset(ctx, 0x00, sizeof(smd_context));

    if (ctx == NULL)
        return NULL;
    
    ctx->fd = -1;
    ctx->tcp.host_ip = strdup(host_ip);
    ctx->tcp.port = atoi(host_port);

    return ctx;
}

static int smd_context_connect_tcp(smd_context *ctx) {
    struct sockaddr_in server_addr;

    if ((ctx->fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        return -1;
    }

    memset((void*)&server_addr, 0x00, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ctx->tcp.host_ip);
    server_addr.sin_port = htons(ctx->tcp.port);

    if (connect(ctx->fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        close(ctx->fd);
        return -1;
    }

    return 0;    
}

static int smd_connect(smd_context *ctx) {
    return smd_context_connect_tcp(ctx);
}

static int connect_to_server(smd_context *ctx) {
    if (ctx == NULL) return -1;
   
    return smd_connect(ctx);
}

static void show_prompt(smd_context *ctx) {
    char prompt[128];
    char *line;
    char buf[1024];

    snprintf(prompt, 128, "%s:%d", ctx->tcp.host_ip, ctx->tcp.port);
    printf("\n%s> ", prompt);

    while (1) {
        fgets(buf, 1024, stdin);

        if (buf == NULL) {
            printf("\n%s> ", prompt);
            continue;
        }

        if (strncasecmp(buf, "quit", 4) == 0 || strncasecmp(buf, "exit", 4) == 0) {
            goto quit;
        }
        
        printf("input : %s", buf);
        
        printf("%s> ", prompt);
    }

quit:
    close(ctx->fd);
}

int main(int argc, char **argv) {
    smd_context *ctx = NULL;
    int ret;

    if (argc != 3) {
        printf("./smd_server <host ip> <host port>\n");
        return 1;
    }

    ctx = smd_context_init(argv[1], argv[2]);

    ret = connect_to_server(ctx);
    if (ret != 0) {
        printf("connect to server error\n");
        return -1;
    }

    show_prompt(ctx);

    return 0;
}

