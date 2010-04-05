/*-----------------------------------------------------------------------------
 * forward-server.c
 * Copyright (C) 2010 Steffen L. Norgren <ironix@trollop.org>
 * 
 * forward-server.c is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * forward-server.c is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *----------------------------------------------------------------------------*/

#include "forward-server.h"

int setnonblock(int fd)
{
	int flags;
	
	flags = fcntl(fd, F_GETFL);
	if (flags <= ERROR)
		return flags;
	
	flags |= O_NONBLOCK;
	if (fcntl(fd, F_SETFL, flags) <= ERROR) 
		return -1;
	
	return ERROR_NONE;
}

static void read_remote_cb(struct ev_loop *loop, struct ev_io *w, int revents)
{ 
	int rlen = 0;
	char read_buff[IO_BUFFER];
	
	/* retrieve the desired client object */
	struct client *client = ((struct client*) (((char*)w) -
							offsetof(struct client, ev_read_out)));
	
	if (revents & EV_READ) {
		rlen = read(client->fd_out, &read_buff, IO_BUFFER);
		
		/* only send if we have data */
		if (rlen > 0)
			write(client->fd_in, read_buff, rlen);
	}
}

static void read_client_cb(struct ev_loop *loop, struct ev_io *w, int revents)
{ 
	int rlen = 0;
	char read_buff[IO_BUFFER];
	char client_ip[INET_ADDRSTRLEN];
	char remote_ip[INET_ADDRSTRLEN];
	struct sockaddr_in client_addr, remote_addr;
	
	/* retrieve the desired client object */
	struct client *client = ((struct client*) (((char*)w) -
							offsetof(struct client, ev_read)));
	
	if (revents & EV_READ) {
		rlen = read(client->fd_in, &read_buff, IO_BUFFER);
		
		/* only send if we have data */
		if (rlen > 0)
			write(client->fd_out, read_buff, rlen);
		else { /* otherwise close the connection and event loop */
			ev_io_stop(EV_A_ w);
			
			/* get the structs */
			client_addr = peer_info(client->fd_in);			
			remote_addr = peer_info(client->fd_out);
			
			/* get the IP addresses */
			inet_ntop(AF_INET, &client_addr.sin_addr,
					  client_ip, sizeof(client_ip));
			inet_ntop(AF_INET, &remote_addr.sin_addr,
					  remote_ip, sizeof(remote_ip));
			
			/* print disconnection info */
			printf("Terminated client %s:%d connection to %s:%d\n",
				   client_ip, ntohs(local_info(client->fd_in).sin_port),
				   remote_ip, ntohs(remote_addr.sin_port));
			
			/* close the sockets */
			close(client->fd_in);
			close(client->fd_out);
		}
	}
}

static void accept_cb(struct ev_loop *loop, struct ev_io *w, int revents)
{
	int fd_in, fd_out, remote_port, local_port, i;
	struct sockaddr_in in_addr, out_addr;
	struct hostent *hent;
	struct client *client;
	socklen_t in_len = sizeof(in_addr);
	socklen_t out_len = sizeof(out_addr);
	char *remote_ip;
	
	/* allocate a new client object */
	client = calloc(1, sizeof(*client));
	
	/* create two linked objects: inbound & outboind client requests */
	fd_in = accept(w->fd, (struct sockaddr *)&in_addr, &in_len);
	local_port = ntohs(local_info(fd_in).sin_port);
	
	if (fd_in == ERROR)
		return;
	
	/* create the outbound socket */
	if ((fd_out = socket(AF_INET, SOCK_STREAM, 0)) <= ERROR)
		return;
	
	/* connect to the appropriate ip:port for this connection */
	for (i = 0; i < num_elements; i++) {
		if (forward_info[i].local_port == local_port) {
			remote_ip = forward_info[i].remote_ip;
			remote_port = forward_info[i].remote_port;
		}
	}

	/* create the sockaddr_in structure for the outbound connection */
	if (!(hent = gethostbyname(remote_ip)))
		return;
	
	memcpy(&out_addr.sin_addr, hent->h_addr, hent->h_length);
	out_addr.sin_family = AF_INET;
	out_addr.sin_port = htons(remote_port);
	
	/* attempt to make the outbound connection */
	if (connect(fd_out, (struct sockaddr *)&out_addr, out_len) <= ERROR)
		if (errno != EINPROGRESS)
			return;
	
	/* associate the socket descriptors for the client */
	client->fd_in = fd_in;
	client->fd_out = fd_out;
	
	/* print connection info */
	printf("Forwarding client %s:%d to %s:%d\n",
		   inet_ntoa(in_addr.sin_addr),
		   local_port, hent->h_name, ntohs(out_addr.sin_port));
	
	/* set inbound & outbound to non-blocking */
	if (setnonblock(client->fd_in) <= ERROR ||
		setnonblock(client->fd_out) <= ERROR)
		err(1, "failed to set socket to non-blocking");
	
	/* add read events from the client to the loop */
	ev_io_init(&client->ev_read, read_client_cb, client->fd_in, EV_READ);
	ev_io_start(loop, &client->ev_read);

	/* add read events from remote to the loop */
	ev_io_init(&client->ev_read_out, read_remote_cb, client->fd_out, EV_READ);
	ev_io_start(loop, &client->ev_read_out);
}

struct sockaddr_in local_info(int socket_fd)
{
	struct sockaddr_in addr;
	socklen_t addr_len = sizeof(addr);
	
	/* get the local port */
	if (getsockname(socket_fd, (struct sockaddr *)&addr, &addr_len) == ERROR)
		err(1, "getsockname");
	
	return addr;
}

struct sockaddr_in peer_info(int socket_fd)
{
	struct sockaddr_in addr;
	socklen_t addr_len = sizeof(addr);
	
	if (getpeername(socket_fd, (struct sockaddr*)&addr, &addr_len) == ERROR)
		err(1, "getpeername");
	
	return addr;
}

void read_config(void)
{
	FILE *file;
	char entry[255];
	char *token;
	
	if ((file = fopen(FILENAME, "r")) == NULL)
		err(1, "fopen");
	
	while (fscanf(file, "%s", entry) != EOF) {
		FORWARD item;

		/* extract local port */
		token = strtok(entry, ":");
		item.local_port = atoi(token);
		
		/* extract remote ip */
		token = strtok(NULL, ":");
		item.remote_ip = malloc((strlen(token) + 1) * sizeof(char));
		strncpy(item.remote_ip, token, strlen(token) + 1);
		
		/* extract remote port */
		token = strtok(NULL, ":");
		item.remote_port = atoi(token);
		
		forward_add(item);
	}
}

void forward_add(FORWARD item)
{
	void *tmp;
	
	if(num_elements == num_allocated) /* is more space needed? */
	{
		if (num_allocated == 0)
			num_allocated = 3; /* start off with 3 references */
		else
			num_allocated *= 2; /* double space each time */
		
		/* transactional relocation via temporary variable */
		tmp = realloc(forward_info, (num_allocated * sizeof(FORWARD)));
		
		/* realloc error */
		if (!tmp)
			err(1, "realloc");
		
		/* allocation succeeded */
		forward_info = (FORWARD *)tmp;
	}
	
	forward_info[num_elements] = item;
	num_elements++;
}

void print_forward_info(void)
{
	int i;
	
	for (i = 0; i < num_elements; i++) {
		printf("Forwarding from port %d to %s:%d\n",
			   forward_info[i].local_port,
			   forward_info[i].remote_ip,
			   forward_info[i].remote_port);
	}
}

void terminate(int sig)
{
	printf("\nProgram termination...\n");
	free(forward_info);
	
	exit(ERROR_NONE);
}

int main()
{
	struct ev_loop *loop = ev_default_loop (0);	/* Use the default event loop */
	struct sockaddr_in listen_addr; 
	int listen_fd, reuseaddr_on = 1, i;
	
	/* properly cleanup when exiting */
	(void)signal(SIGINT, terminate);
	
	/* read config data into the array */
	read_config();
	
	/* print the configuration */
	print_forward_info();

	for (i = 0; i < num_elements; i++) {
		/* create listening socket */
		listen_fd = socket(AF_INET, SOCK_STREAM, 0); 
		if (listen_fd <= ERROR)
			err(1, "listen failed");
		
		/* reuse the socket address */
		if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_on,
					   sizeof(reuseaddr_on)) == -1)
			err(1, "setsockopt failed");
		
		/* set to listen on all addresses */
		memset(&listen_addr, 0, sizeof(listen_addr));
		listen_addr.sin_family = AF_INET;
		listen_addr.sin_addr.s_addr = INADDR_ANY;
		listen_addr.sin_port = htons(forward_info[i].local_port);
		
		/* bind the socket */
		if (bind(listen_fd, (struct sockaddr *)&listen_addr,
				 sizeof(listen_addr)) < 0)
			err(1, "bind failed");
		
		/* listen for new connections */
		if (listen(listen_fd, 5) <= ERROR)
			err(1, "listen failed");
		
		/* set to non-blocking */
		if (setnonblock(listen_fd) <= ERROR)
			err(1, "failed to set server socket to non-blocking");
		
		/* initialise socket watcher, then start it */
		ev_io_init(&forward_info[i].ev_accept, accept_cb, listen_fd, EV_READ);
		ev_io_start(loop, &forward_info[i].ev_accept);
	}
	
	/* wait for events to arrive */
	ev_loop(loop, 0);
	
	/* loop exited, so end the program */
	return ERROR_NONE;
}