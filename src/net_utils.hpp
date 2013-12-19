#ifndef NET_UTILS_H_
#define NET_UTILS_H_

int make_socket_non_blocking (int sfd);
int create_and_bind(char *port);

#endif