/*
 * network_util.h
 *
 * Functions that implement network operations.
 *
 *  @since 2019-04-10
 *  @author: Philip Gust
 */

#ifndef NETWORK_UTIL_H_
#define NETWORK_UTIL_H_

/**
 * Get listener socket
 *
 * @param port the port number
 * @return listener socket or 0 if unavailable
 */
int get_listener_socket(int port) ;

/**
 * Accept new peer connection on a listen socket.
 *
 * @param listen_sock_fd the listen socket
 * @return the peer socket fd
 */
int accept_peer_connection(int listen_sock_fd);

/**
 * Get the local host and port for a socket.
 *
 * @param sock_fd the socket
 * @param addr_str buffer for IP address string
 * @param port pointer for port value
 * @return 0 if successful
*/
int get_local_host_and_port(int sock_fd, char *addr_str, int *port);

/**
 * Get the peer host and port for a socket.
 *
 * @param sock_fd the socket
 * @param addr_str buffer for IP address string
 * @param port pointer for port value
 * @return 0 if successful
 */
int get_peer_host_and_port(int sock_fd, char *addr_str, int *port);

#endif /* NETWORK_UTIL_H_ */
