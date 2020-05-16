/*
 * network_util.c
 *
 * Functions that implement network operations.
 *
 *  @since 2019-04-10
 *  @author: Philip Gust
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

/**
 * Get listener socket
 *
 * @param port the port number
 * @return listener socket or 0 if unavailable
 */
int get_listener_socket(int port) {
    // Creating internet socket stream file descriptor
    int listen_sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock_fd == 0) {
        return 0;
    }

    // SO_REUSEADDR prevents the "address already in use" errors
    // that commonly come up when testing servers.
    int optval = 1;
    if (setsockopt(listen_sock_fd, SOL_SOCKET, SO_REUSEADDR, &optval , sizeof(int)) < 0) {
    	close(listen_sock_fd);
    	return 0;
    }

    // host address and port
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);
    memset(&address, 0, addrlen);
    address.sin_family = AF_INET;  // address from internet
    address.sin_port = htons(port);   // port in network byte order
    address.sin_addr.s_addr = INADDR_ANY;  // bind to any address

    // bind host address to port
    if (bind(listen_sock_fd, (struct sockaddr *)&address, addrlen) < 0) {
    	close(listen_sock_fd);
		return 0;
    }

    // set up queue for clients connections up to default
    // maximum pending socket connections (usually 128)
    if (listen(listen_sock_fd, SOMAXCONN) < 0) {
    	close(listen_sock_fd);
    	return 0;
    }

	return listen_sock_fd;
}

/**
 * Accept new peer connection on a listen socket.
 *
 * @param listen_sock_fd the listen socket
 * @return the peer socket fd
 */
int accept_peer_connection(int listen_sock_fd) {
	while (true) {
		struct sockaddr_in peer_addr;
		socklen_t peer_size = sizeof(peer_addr);
		int peer_sock_fd = accept(listen_sock_fd, (struct sockaddr *)&peer_addr, &peer_size);
		if (peer_sock_fd > 0) {
			return peer_sock_fd;
		}
		perror("accept");
	}
}

/**
 * Get the local host and port for a socket.
 *
 * @param sock_fd the socket
 * @param addr_str buffer for IP address string
 * @param port pointer for port value
 * @return 0 if successful
*/
int get_local_host_and_port(int sock_fd, char *addr_str, int *port) {
	struct sockaddr_in addr;
	socklen_t size = sizeof(addr);

	int status = getsockname(sock_fd, (struct sockaddr *)&addr, &size);
    if (status == 0) {
    	*port = ntohs(addr.sin_port);
    	strcpy(addr_str, inet_ntoa(addr.sin_addr));
    }
    return status;
}

/**
 * Get the peer host and port for a socket.
 *
 * @param sock_fd the socket
 * @param addr_str buffer for IP address string
 * @param port pointer for port value
 * @return 0 if successful
 */
int get_peer_host_and_port(int sock_fd, char *addr_str, int *port) {
	struct sockaddr_in addr;
	socklen_t size = sizeof(addr);

	int status = getpeername(sock_fd, (struct sockaddr *)&addr, &size);
    if (status == 0) {
    	*port = ntohs(addr.sin_port);
    	strcpy(addr_str, inet_ntoa(addr.sin_addr));
    }
    return status;
}


