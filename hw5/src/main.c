#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "debug.h"
#include "protocol.h"
#include "server.h"
#include "client_registry.h"
#include "player_registry.h"
#include "jeux_globals.h"

#ifdef DEBUG
int _debug_packets_ = 1;
#endif

static void terminate(int status);
static int parse_option(int argc, char *argv[]);
static void install_sighup_handler();
static void handle_sighup(int sig_num, siginfo_t *siginfo, void *data);
void server_loop();

static int listen_port;

/*
 * "Jeux" game server.
 *
 * Usage: jeux <port>
 */
int main(int argc, char* argv[]){
    // Option processing should be performed here.
    // Option '-p <port>' is required in order to specify the port number
    // on which the server should listen.
    if (parse_option(argc, argv) < 0) {
        return -1;
    }

    // Perform required initializations of the client_registry and
    // player_registry.
    client_registry = creg_init();
    player_registry = preg_init();

    // TODO: Set up the server socket and enter a loop to accept connections
    // on this socket.  For each connection, a thread should be started to
    // run function jeux_client_service().  In addition, you should install
    // a SIGHUP handler, so that receipt of SIGHUP will perform a clean
    // shutdown of the server.
    install_sighup_handler();
    server_loop();

    fprintf(stderr, "You have to finish implementing main() "
	    "before the Jeux server will function.\n");

    terminate(EXIT_FAILURE);
}

/*
 * Function called to cleanly shut down the server.
 */
void terminate(int status) {
    // Shutdown all client connections.
    // This will trigger the eventual termination of service threads.
    creg_shutdown_all(client_registry);
    
    debug("%ld: Waiting for service threads to terminate...", pthread_self());
    creg_wait_for_empty(client_registry);
    debug("%ld: All service threads terminated.", pthread_self());

    // Finalize modules.
    creg_fini(client_registry);
    preg_fini(player_registry);

    debug("%ld: Jeux server terminating", pthread_self());
    exit(status);
}

int parse_option(int argc, char *argv[]) {
    int ch;
    while ((ch = getopt(argc, argv, "p:")) != -1) {
        switch (ch) {
            case 'p':
                listen_port = atoi(optarg);
                break;
            default:
                printf("Usage: %s -p port\n", argv[0]);
                return -1;
        }
    }

    if (listen_port == 0) {
         printf("Usage: %s -p port\n", argv[0]);
         return -1;
    }

    return 0;
}

void install_sighup_handler() {
    struct sigaction act;
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_SIGINFO;
    act.sa_sigaction = handle_sighup;
    sigaction(SIGHUP, &act, NULL);
}

void handle_sighup(int sig_num, siginfo_t *siginfo, void *data) {
     printf("handle_sighup\n");
}

void server_loop() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        exit(-1);
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(listen_port);
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr* )&sa, sizeof(sa)) < 0) {
        exit(-1);
    }

    if (listen(fd, 512) < 0) {
        exit(-1);
    }

    while (1) {
        struct sockaddr_in sa;
        socklen_t length = sizeof(sa);
        int client_fd = accept(fd, (struct sockaddr*)&sa, &length);
        if (client_fd < 0) {
            break;
        }
        pthread_t thread;
        int *arg = malloc(sizeof(int));
        *arg = client_fd;
        int ret = pthread_create(&thread, NULL, jeux_client_service, arg);
        if (ret < 0) {
            error("pthread_create %s", strerror(ret));
            close(client_fd);
        }
        pthread_detach(thread);
    }

    close(fd);
}

