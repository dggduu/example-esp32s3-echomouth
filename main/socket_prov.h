// socket_prov.h
#ifndef SOCKET_PROV_H
#define SOCKET_PROV_H

#include <stdint.h>
#include <sys/socket.h> // for socklen_t

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the socket provider component.
 * @return 0 on success, -1 on error.
 */
int socket_prov_init(void);

/**
 * @brief Deinitialize the socket provider component. Closes the socket if open.
 */
void socket_prov_deinit(void);

/**
 * @brief Connect to a remote server.
 * @param host IP address or hostname string.
 * @param port Port number.
 * @return 0 on success, -1 on error.
 */
int socket_prov_connect(const char *host, uint16_t port);

/**
 * @brief Get the global socket descriptor.
 * @return Socket descriptor, or -1 if not connected.
 */
int socket_prov_get_socket(void);

/**
 * @brief Send data over the global socket.
 * @param data Pointer to data.
 * @param len Length of data.
 * @param flags Flags (as in send).
 * @return Number of bytes sent, or -1 on error.
 */
ssize_t socket_prov_send(const void *data, size_t len, int flags);

/**
 * @brief Receive data from the global socket.
 * @param buffer Buffer to receive data.
 * @param len Maximum length to receive.
 * @param flags Flags (as in recv).
 * @return Number of bytes received, or -1 on error.
 */
ssize_t socket_prov_recv(void *buffer, size_t len, int flags);

/**
 * @brief Close the global socket.
 * @return 0 on success, -1 on error.
 */
int socket_prov_close(void);

#ifdef __cplusplus
}
#endif

#endif // SOCKET_PROV_H