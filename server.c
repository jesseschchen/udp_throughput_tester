#include <sys/socket.h> // socket()
#include <unistd.h> 	// read(), write()
#include <fcntl.h> 		// open(), close()
#include <errno.h>		// errno
#include <stdlib.h>		// malloc(), free()
#include <netinet/in.h> // struct sockaddr_in
#include <stdio.h> 		// sprintf()
#include <arpa/inet.h>	// inet_addr()
#include <string.h> 	// memcpy(), strlen()
#include <time.h> 		// clock()
#include <sys/select.h>	// select()
#include <stdint.h>		// uint8_t, etc
#include <pthread.h>	// pthreads

#define MAX_CLIENTS 20
#define SEGMENT_INTERVAL 1 // segment interval in seconds


typedef struct conn_info {
	int port_num;
	size_t* segment_total;
} conn_info;



void* update_client_rate(void* arg) {
	conn_info ci = *(conn_info*)arg;
	size_t* segment_total = ci.segment_total; // makes a local copy of the conn_info struct
	struct sockaddr_in serv_addr;
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(ci.port_num);
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	int conn_fd, sock = socket(AF_INET, SOCK_STREAM, 0);
	perror("control socket()");

	int enable = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
	bind(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
	perror("control bind()");

	listen(sock, MAX_CLIENTS);
	perror("control listen()");

	conn_fd = accept(sock, NULL, NULL);
	perror("control accept()");


	struct timespec start, end;
	size_t segment_rate;
	uint8_t matched = 0;

	while(!matched) {
		*segment_total = 0;
		clock_gettime(CLOCK_MONOTONIC, &start);
		// update client with recorded rate
		sleep(SEGMENT_INTERVAL);
		clock_gettime(CLOCK_MONOTONIC, &end);
		double run_time = (end.tv_sec - start.tv_sec) + 1e-9*(end.tv_nsec - start.tv_nsec);

		segment_rate = (*segment_total*8) / run_time; // will give bytes/s

		printf("observed rate: %li\n", segment_rate);
		send(conn_fd, &segment_rate, sizeof(segment_rate), 0);

		recv(conn_fd, &matched, sizeof(matched), 0);
	}
	close(conn_fd);
}


// receivs data packets and updates the amount of data received in this segment
void recv_forever(int server_fd, int buf_size, size_t* segment_total) {
	void* buf = malloc(buf_size);
	int recvd;


	while((recvd = recvfrom(server_fd, buf, buf_size, 0,  NULL, NULL)) > 0) {
		// printf("revd: %i\n", recvd);
		*segment_total += recvd;
	}
}



// preps the server to recvfrom() on one port
int prep_port(int port, int buf_size) {
	int server_fd, connection_fd;
	int recvd_file_size;
	struct sockaddr_in address;
	int addrlen = sizeof(address);


	// opens a socket
	if ((server_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("failed to create socket");
		exit(1);
	}
	printf("created socket!\n");

	// initializes a sockaddr_in structure to listen on 
	address.sin_family = AF_INET; // IPv4
	address.sin_addr.s_addr = htonl(INADDR_ANY); // binds to all available interfaces
	address.sin_port = htons(port); // converts from host to network byte order

	// binds server_fd to a specific sockaddr_in
	int enable = 1;
	setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
	if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
		fprintf(stderr, "unable to bind to port %i\n", port);
		exit(1);
	}
	printf("bound socket to port %i\n", port);

	return server_fd;
}



// ./tcp_server <port> 
int main(int argc, char* argv[]) {
	if (argc != 3) {
	 	fprintf(stderr, "usage: ./tcp_server <port> <buf_size>\n");
	 	exit(1);
	}


	int port = atoi(argv[1]);
	int buf_size = atoi(argv[2]);

	size_t segment_total;
	
	conn_info ci;
	ci.port_num = port;
	ci.segment_total = &segment_total;

	pthread_t update_thread;
	pthread_create(&update_thread, NULL, update_client_rate, (void*)&ci);
	pthread_detach(update_thread);

	int server_fd = prep_port(port, buf_size); // preps the server
	recv_forever(server_fd, buf_size, &segment_total);  // recvs packets forever until killed
}