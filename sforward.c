/*
 * sforward - simple port forwarder for udp and tcp
 * it was built as simple c program in one source file
 * it can forward tcp and udp connections
 * serves only one client. no multiclient mode
 * uses select method
 * all config lay in defines which is very useful for hiding
 */


#include <stdio.h>      /* for printf() and fprintf() */
#include <sys/socket.h> /* for socket(), connect(), send(), and recv() */
#include <arpa/inet.h>  /* for sockaddr_in and inet_addr() */
#include <stdlib.h>     /* for atoi() and exit() */
#include <string.h>     /* for memset() */
#include <unistd.h>     /* for close() */
#include <fcntl.h>      /* for fcntl() */

#define IPROTO_TCP 1
#define IPROTO_UDP 2

/* config */
#define INCOME_IP_ANY                 /* if defined then bind on all availaible interfaces */
#ifndef INCOME_IP_ANY               
    #define INCOME_IP "192.168.1.177" /* address to bind for incoming connection listen socket if no INCOME_IP_ANY */ 
#endif
#define INCOME_PORT 5222              /* port for incoming connection */
#define OUTCOME_IP "74.125.232.244"   /* ip for outcome connection to connect to */
#define OUTCOME_PORT 80               /* port for outcome connection */
#define PROTO IPROTO_TCP              /* protocol to use. IPPROTO_TCP or IPPROTO_UDP */
#define DAEMON                         /* if defined then launch process as daemon */
#define VERBOSE 0                     /* if defined produce more verbose output */
#define BUFFER_SIZE 1024*1024*128     /* temporary buffer size */
#define ONCE                          /* if defined then there only one connection can be made. then process die
										 else we just reopen listen socket and wait for another one */

/* logging */
#if VERBOSE > 0
#define vlog printf
#else
#define vlog printf
#endif

#define ilog printf
#define elog perror

/* create common buffer in statis memory for security reasons */
static char buffer[BUFFER_SIZE]; 


int get_tcp_listen_sock() {
	int listen_sock;
	struct sockaddr_in sa;
	/* create socket */
	listen_sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listen_sock < 0) {
		elog("can't create listen socket\n");
		return -1;
	}

	/* bind */
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
#ifdef INCOME_IP_ANY
	sa.sin_addr.s_addr = INADDR_ANY;
#else
	sa.sin_addr.s_addr = inet_addr(INCOME_IP);
#endif
	sa.sin_port = htons(INCOME_PORT);
	if (bind(listen_sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		elog("can't bind income socket\n");
		return -1;
	} 

	/* set to listen state */
	if (listen(listen_sock, 1) < 0) {
		elog("can't set listen_sock to listen mode\n");
		return -1;
	}

	return listen_sock;
}

int get_tcp_client_sock(int listen_sock) {
	int client_sock;
	struct sockaddr_in sa;
	int sa_len = sizeof(sa);

	/* accept */
	if ((client_sock = accept(listen_sock, (struct sockaddr *)&sa, &sa_len)) < 0) {
		elog("can't accept new connection\n");
		return -1;
	}

	vlog("got income client\n");
	return client_sock;
}

int get_outcome_tcp_sock()
{
	int outcome_sock;
	struct sockaddr_in sa;
	char *outcome_addr = OUTCOME_IP;

	/* create sock */
	if ((outcome_sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
		elog("can't create outgoing socket\n");
		return -1;
	}
	
	/* set address */
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	if (inet_aton(outcome_addr, &sa.sin_addr) <= 0) {
		elog("can't convert outcome addr in get_outcome_tcp_sock");
		return -1;
	}
	sa.sin_port = htons(OUTCOME_PORT);

	/* connect */
	ilog("connection out");
	if (connect(outcome_sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		elog("can't connect to outcome");
		return -1;
	}

	ilog("successfuly connected to outboud");
	return outcome_sock;
		
}

int traverse(int from_sock, int to_sock) {
	size_t recv_size;
	/* recv */
	if ((recv_size = recv(from_sock, (void *)buffer, BUFFER_SIZE, 0)) <= 0) {
		elog("error on recv in traverse\n");
		return -1;
	}
	/* send */
	if (send(to_sock, (void *)buffer, recv_size, 0) <= 0) {
		elog("error on send in traverse\n");
		return -1;
	}
	return 0;
}

int sock_loop(int income_sock, int outcome_sock) {
	fd_set fdread;
	fd_set fdwrite;
	fd_set fdexcept;
	fd_set *fds[3]; 
	int fdmax;
	int i;

	/* fill fds */
	fds[0] = &fdread;
	fds[1] = &fdwrite;
	fds[2] = &fdexcept;
	/* set sockets to non block mode */
	/*fcntl(income_sock, F_SETFL, O_NONBLOCK);
	  fcntl(outcome_sock, F_SETFL, O_NONBLOCK);*/
	

	for(;;) {
		/* select read sockets */
		for (i = 0; i < 3; ++i) {
			FD_ZERO(fds[i]);
			FD_SET(income_sock, fds[i]);
			FD_SET(outcome_sock, fds[i]);
		}

		fdmax = outcome_sock;

		if (select(fdmax+1, &fdread, &fdwrite, &fdexcept, NULL) < 0) {
			elog("select error in tcp_loop\n");
			return -1;
		}

		/* if somebody closed the connection */
		if (FD_ISSET(income_sock, &fdexcept)) { /*|| */
			/*			(FD_ISSET(income_sock, &fdread) && FD_ISSET(income_sock, &fdwrite))) {*/
						elog("income sock closed\n");
			break;
		}

			if (FD_ISSET(outcome_sock, &fdexcept)) { /* || */
				/*			(FD_ISSET(outcome_sock, &fdread) && FD_ISSET(outcome_sock, &fdwrite))) { */
						elog("outcome sock closed\n");
						break;
			}
		
		/* if we got data from income */
		if (FD_ISSET(income_sock, &fdread))	{
			vlog("got some data from income\n");
			if (traverse(income_sock, outcome_sock) < 0) {
				return -1;
			}
		}

		/* if we got data from outcome */
		if (FD_ISSET(outcome_sock, &fdread)) {
			vlog("got some data from outcome");
			if (traverse(outcome_sock, income_sock) < 0) {
				return -1;
			}
		}
	}
}

int start_tcp() {
	int listen_sock;
	int income_sock;
	int outcome_sock;
	/* open listen connection  */
	ilog("opening listen connection\n");
	if ((listen_sock = get_tcp_listen_sock()) < 0)
		goto cleanup;

	/* accept client */
	vlog("created listen sock : %d\n", listen_sock);
	ilog("waiting for client to connect to income\n");
	if ((income_sock = get_tcp_client_sock(listen_sock)) < 0)
		goto cleanup;
	
	/* open outcome connection */
	ilog("opening outcome connection\n");
	if ((outcome_sock = get_outcome_tcp_sock()) < 0) {
		goto cleanup;
	}

	/* tcp send recv loop */
	sock_loop(income_sock, outcome_sock);

	/* cleanup */
 cleanup:
	close(income_sock);
	close(outcome_sock);
}

int start_udp() {
	int listen_sock;
	int income_sock;
	/* open udp listen socket */
} 

void daemonize() {
	
}

int main() {
#ifdef DAEMON
	ilog("running in background\n");
	daemonize();
#else
	ilog("running in foreground\n");
#endif

#if PROTO == IPROTO_TCP
	#ifndef ONCE
	for (;;) 
    #endif
		start_tcp();
#elif PROTO == IPPROTO_UDP
	#ifndef ONCE
	for (;;)
	#endif
		start_udp();
#else 
	#error unsupported protocol selected. use IPPROTO_TCP or IPPROTO_UDP
#endif
}

