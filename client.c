#include <stdio.h> 
#include <stdlib.h> 
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <termios.h>
#include <time.h>
#include <ctype.h>
#include <linenoise/linenoise.h>
#include "define.h"
#include "args.c"
#include "printbuf.c"


/* Linenoise callback */
static void completion(const char *buf, linenoiseCompletions *lc);
static char *hints(const char *buf, int *color, int *bold);

/* Commands */
typedef int (*do_command)(int argc, char **args);

static int do_command_help(int argc, char **args);
static int do_command_state(int argc, char **args);
static int do_command_connect(int argc, char **args);
static int do_command_read(int argc, char **args);
static int do_command_write(int argc, char **args);
static int do_command_close(int argc, char **args);
static int do_command_shutdown(int argc, char **args);

typedef struct {
    const char *name;
    int argc; // Arguments required

    /* For help message */
    const char *help_args;   // 0 or "" if no additional arguments
    const char *help_message;

    do_command handler;
} command_s; 

command_s commands[] = {
    {"help", 0, 0, "Help message", do_command_help},
    {"state", 0, 0, "Show connection state", do_command_state},
    {"connect", 2, "`ip` `port`", "Make connection", do_command_connect},
    {"read", 0, 0, "Read data from connection", do_command_read},
    {"write", 1, "`message`", "Write `message` to connection", do_command_write},
    {"close", 0, 0, "Close connection", do_command_close},
    {"shutdown", 1, "read|write", "Shutdown connection", do_command_shutdown},
};

/* Connection state */
typedef enum {
    s_closed = 0,
    s_connected = 1,
} state_e;

const char *state_str[] = {"s_closed", "s_connected"};

/* client */
typedef struct {
    state_e state; 
    int cfd;
} client_s;

static client_s client;

#define HISTORY_LOG ".chistory.txt"
#define PROMPT "client > "

int main(int argc, char **argv) {
    /* Init client */
    client.state = s_closed;
    client.cfd = 0;

    linenoiseSetCompletionCallback(completion);
    linenoiseSetHintsCallback(hints);
    linenoiseHistoryLoad(HISTORY_LOG);

    char *line;
    int largc = 0;
    char *largs[MAX_ARG_SIZE];
    while( (line=linenoise(PROMPT))!=NULL) {
        largc = parse_args(line, largs, MAX_ARG_SIZE);
        if( largc<0 ) {
            fprintf(stderr, "Parse line error: '%s'\n", line);
            continue;
        } else if( largc==0 ) {
            // empty line
            continue;
        }

        linenoiseHistoryAdd(line);
        linenoiseHistorySave(HISTORY_LOG);

        int i = 0;
        int cmd_n = sizeof(commands)/sizeof(command_s);
        for( ; i<cmd_n; i++ ) {
            command_s *cmd = &commands[i];
            if( strcmp(largs[0], cmd->name)!=0 ) {
                continue;
            }
            if( largc<cmd->argc+1 ) {
                fprintf(stderr, "arguments error\n");
                fprintf(stderr, "usage: %s %s\n", cmd->name, cmd->help_args);
                break;
            } 
            if( cmd->handler ) {
                cmd->handler(largc, largs);
            }
            break;
        }
        if( i==cmd_n ) {
            fprintf(stderr, "Command not found: '%s'\n", largs[0]);
        }

        free_args(largc, largs);
        free(line);
    }
    return 0;
}

void completion(const char *buf, linenoiseCompletions *lc) {
    int n = strlen(buf);
    for(int i=0; i<sizeof(commands)/sizeof(command_s); i++) {
        command_s *cmd = &commands[i];
        if( strncasecmp(buf, cmd->name, n)==0 ) {
            linenoiseAddCompletion(lc, cmd->name);
        }  
    }
}

char *hints(const char *buf, int *color, int *bold) {
    *color = 35;
    *bold = 0;

    int tmp_size = 256;
    char *tmp = malloc(tmp_size);
    for(int i=0; i<sizeof(commands)/sizeof(command_s); i++) {
        command_s *cmd = &commands[i];
        if( strcasecmp(buf, cmd->name)==0 &&
                        cmd->help_args!=0 && 
                        strlen(cmd->help_args)!=0 ) {
            snprintf(tmp, tmp_size, " %s", cmd->help_args); // " " + cmd->help_args
            return tmp;
        }
    }
    return NULL;
}

int do_command_connect(int argc, char **args) {
    const char *ip = args[1];
    short port = atoi(args[2]);

    if( client.state!=s_closed ) {
        printf("Error state: `%s`\n", state_str[client.state]);
        printf("Already connected.\n");
        printf("Close it before making a new connection.\n");
        return 1;
    }

    client.cfd = socket(AF_INET, SOCK_STREAM, 0);
    if( client.cfd<0 ) {
        fprintf(stderr, "create socket error: %s\n", strerror(errno));
        return 1;      
    }
    struct sockaddr_in saddr;
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(port);
    if( inet_pton(AF_INET, ip, &saddr.sin_addr)<0 ) {
        fprintf(stderr, "inet_pton error: %s, ip '%s'\n", strerror(errno), ip);
        return 1;      
    }

    if( connect(client.cfd, (struct sockaddr*)&saddr, sizeof(saddr))<0 ) {
        fprintf(stderr, "connect to %s:%d error: %s\n", ip, port, strerror(errno));
        return 1;
    }

    client.state = s_connected;
    printf("Connected %s:%d \n", ip, port);
    return 0;
}

int do_command_read(int argc, char **args) {
    if( client.state!=s_connected ) {
        printf("Error state: `%s`\n", state_str[client.state]);
        printf("Can't read on closed connection.\n");
        return 1;
    }
    printf("Reading ...\n");

    char buf[1024] = {0};
    size_t bytes = recv(client.cfd, buf, sizeof(buf), 0);
    if( bytes<0 ) {
        printf("recv error: %s\r\n", strerror(errno));
        return 1;
    } else if( bytes==0 ) {
        printf("Read EOF\r\n");
        return 1;
    }
    printf("(%d)\'", bytes);
    printbuf(stdout, buf, strlen(buf));
    printf("\'\n");
}

int do_command_write(int argc, char **args) {
    if( client.state!=s_connected ) {
        printf("Error state: `%s`\n", state_str[client.state]);
        printf("Can't write on closed connection.\n");
        return 1;
    }
    printf("Writing ...\n");

    char *sbuf = args[1];
    size_t bytes = send(client.cfd, sbuf, strlen(sbuf), 0);
    if( bytes<0 ) {
        fprintf(stderr, "send error: %s\r\n", strerror(errno));
        return 1;
    }
    printf("(%d)\'%s\'\n", bytes, sbuf);
}

int do_command_close(int argc, char **args) {
    if( client.state!=s_connected ) {
        printf("Error state: `%s`.\n", state_str[client.state]);
        printf("Not connected.\n");
        return 1;
    }
    close(client.cfd);
    client.state = s_closed;
    printf("Closed\n");
}

int do_command_shutdown(int argc, char **args) {
    if( client.state!=s_connected ) {
        printf("Error state: `%s`.\n", state_str[client.state]);
        printf("Not connected.\n");
        return 1;
    }
    int flag = 0;
    if( strcmp(args[1], "read")==0 ) {
        flag = SHUT_RD;
    } else if ( strcmp(args[1], "write")==0 ) {
        flag = SHUT_WR;
    } else {
        printf("Invalid shutdown mode `%s`\n", args[1]);
        return 1;
    }
    if( shutdown(client.cfd, flag)<0 ) {
        printf("shutdown error: %s\n", strerror(errno));
        return 1;
    }
    printf("OK\n");
}

int do_command_state(int argc, char **args) {
    printf("%s\n", state_str[client.state]);
}

int do_command_help(int argc, char **args) {
    char tmp[256];
    for(int i=0; i<sizeof(commands)/sizeof(command_s); i++) {
        command_s *cmd = &commands[i];
        if( cmd->help_args==0 || strlen(cmd->help_args)==0 ) {
            snprintf(tmp, sizeof(tmp), "%s", cmd->name); 
        } else {
            snprintf(tmp, sizeof(tmp), "%s %s", cmd->name, cmd->help_args);
        }
        printf("%-30s%s\n", tmp, cmd->help_message);
    }
    return 0;
}


