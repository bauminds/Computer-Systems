/*
 * http_server.c
 *
 * The HTTP server main function sets up the listener socket
 * and dispatches client requests to request sockets.
 *
 *  @since 2019-04-10
 *  @author: Philip Gust
 */
#include <stdbool.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "http_methods.h"
#include "time_util.h"
#include "http_util.h"
#include "http_request.h"
#include "network_util.h"
#include "http_server.h"
#include "thpool.h"
#include "map.h"
#include "mime_util.h"

#define DEFAULT_HTTP_PORT 1500
#define MIN_PORT 1000

/** debug flag */
const bool debug = true;

/** subdirectory of application home directory for web content */
const char *CONTENT_BASE = "content";
//when run it, need to be in root path. "content" is relative to the root path.

/**
 * Task for thread
 * @param socket_fd: the socket number.
 */
void task(void* socket_fd){
	process_request(*(int*)socket_fd);
}

/**
 * Main program starts the server and processes requests
 * @param argv[1]: optional port number (default: 1500)
 */
int main(int argc, char* argv[argc]) {
	int port = DEFAULT_HTTP_PORT;

    if (argc == 2) {
		if ((sscanf(argv[1], "%d", &port) != 1) || (port < MIN_PORT)) {
			fprintf(stderr, "Invalid port %s\n", argv[1]);
			return EXIT_FAILURE;
		}
	}
    //return is a file descriptor of the socket.
    // bind the socket and listen.
    int listen_sock_fd = get_listener_socket(port);
	if (listen_sock_fd == 0) {
		perror("listen_sock_fd");
		return EXIT_FAILURE;
	}

	fprintf(stderr, "HttpServer running on port %d\n", port);

	puts("Making threadpool with 4 threads");
	threadpool thpool = thpool_init(4);

	FILE* mime_type = fopen("./mime.types", "r+");
	if (mime_type == NULL){
		fprintf(stderr, "No mime type file.\n");
		exit(1);
	}
	buildMap(mime_type, &mime_map);
	fclose(mime_type);

	while (true) {
        // accept client connection
		// socket_fd here is a peer socket
		int socket_fd = accept_peer_connection(listen_sock_fd);

		if (debug) {
			int port;
			char host[MAXBUF];
			//use the socket_fd to get host and port info
			if (get_peer_host_and_port(socket_fd, host, &port) != 0) {
			    perror("get_peer_host_and_port");
			} else {
				fprintf(stderr, "New connection accepted  %s:%u\n", host, port);
			}
		}

		int *ptr_socket_fd = malloc(sizeof(int));
		*ptr_socket_fd = socket_fd;
		// Use thread pool worker to handle request
		thpool_add_work(thpool, (void *)task, (void *)ptr_socket_fd);
		free(ptr_socket_fd);

		// handle request
//		process_request(socket_fd);
    }

	puts("Killing threadpool");
	thpool_destroy(thpool);
	//code will never get to here.
    //close listener socket
    close(listen_sock_fd);
    return EXIT_SUCCESS;

}
