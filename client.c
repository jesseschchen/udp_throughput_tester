#include <sys/socket.h> // socket()
#include <unistd.h> 	// read(), write()
#include <fcntl.h> 		// open(), close()
#include <errno.h>		// errno
#include <stdlib.h>		// malloc(), free()
#include <netinet/in.h> // struct sockaddr_in
#include <stdio.h> 		// sprintf()
#include <arpa/inet.h>	// inet_addr()
#include <string.h>		// strlen()
#include <pthread.h>	// pthreads
#include <stdint.h> 	// uint8_t, etc

#define CONSEC_COUNT 4
#define SEGMENT_INTERVAL 1 // segment interval in seconds

float RATE_PRECISION;


typedef struct conn_info {
	char* server_ip;
	int port_num;
	size_t target_rate;
	size_t* packet_gap;
	size_t* segment_total;
} conn_info;


// updates packet gap according to client's send rate
void* update_packet_gap_from_local(void* arg) {
	conn_info ci = *(conn_info*)arg; // makes a local copy of the conn_info struct
	size_t* segment_total = ci.segment_total;

	struct sockaddr_in serv_addr;
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = inet_addr(ci.server_ip);
	serv_addr.sin_port = htons(ci.port_num); // always use port 25999 for control port
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	perror("control socket()");
	connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
	perror("control connect()");
	close(sock); // closes server's connection

	// updates packet_gap based on observed rate
	struct timespec start, end;
	double segment_time, rate_ratio, ratio_diff;
	size_t segment_rate;
	uint8_t consec_hits = 0; // consecutive cycles withing target tput range
	double consec_ratio_total; // sum of consecutive rate ratios
	while(1) {
		*segment_total = 0;
		clock_gettime(CLOCK_MONOTONIC, &start);
		sleep(SEGMENT_INTERVAL);
		clock_gettime(CLOCK_MONOTONIC, &end);
		segment_time = (end.tv_sec - start.tv_sec) + 1e-9*(end.tv_nsec - start.tv_nsec);
		segment_rate = (*segment_total*8) / segment_time;

		// updates packet_gap based on rate differences
		rate_ratio = (double)segment_rate / ci.target_rate;
		ratio_diff = rate_ratio - 1;
		printf("packet_gap: %li\n", *ci.packet_gap);
		printf("rate_ratio: %f\n", rate_ratio);
		if (ratio_diff < RATE_PRECISION && ratio_diff > (-1)*RATE_PRECISION) { // rate is within target range
			consec_hits += 1;
			consec_ratio_total += rate_ratio;
			printf("hit: %i\n", consec_hits);
			if (consec_hits >= CONSEC_COUNT) { // target rate has been set, stop updating packet gap
				//matched = 1;
				printf("CONVERGENCE\n");
			}
		}
		else {
			if (consec_hits > 0) {
				double avg_consec_rate_ratio = consec_ratio_total / consec_hits;
				// update packet gap with average throughput of previous consec_hits
				*ci.packet_gap = *ci.packet_gap * avg_consec_rate_ratio; 
			}
			else {
				// updates packet gap with previous observed rate
				*ci.packet_gap = *ci.packet_gap * rate_ratio;
			}
			consec_hits = 0; // reset number of consecutive hits
			consec_ratio_total = 0; // reset total ratios
		}
	}
	pthread_exit(NULL);
}


// receives updates about current throughput and adjusts packeg_gap accordingly 
// updates packet gap according to server's received rate
void* update_packet_gap(void* arg) {
	conn_info ci = *(conn_info*)arg;  // makes a local copy of the conn_info struct
	struct sockaddr_in serv_addr;
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = inet_addr(ci.server_ip);
	serv_addr.sin_port = htons(ci.port_num); // always use port 25999 for control port



	int sock = socket(AF_INET, SOCK_STREAM, 0);
	perror("control socket()");

	connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
	perror("control connect()");

	uint8_t matched = 0;
	size_t rec_rate; // traffic rate reported by server
	uint8_t consec_hits = 0; // consecutive cycles withing target tput range
	double consec_ratio_total; // sum of consecutive rate ratios
	while(recv(sock, &rec_rate, sizeof(size_t), 0)) {
		double rate_ratio = (double)rec_rate / ci.target_rate;
		double ratio_diff = rate_ratio - 1;
		printf("packet_gap: %li\n", *ci.packet_gap);
		printf("rate_ratio: %f\n", rate_ratio);
		if (ratio_diff < RATE_PRECISION && ratio_diff > (-1)*RATE_PRECISION) { // rate is within target range
			consec_hits += 1;
			consec_ratio_total += rate_ratio;
			if (consec_hits >= CONSEC_COUNT) { // target rate has been set, stop updating packet gap
				//matched = 1;
				printf("CONVERGENCE\n");
			}
		}
		else {
			if (consec_hits > 0) {
				double avg_consec_rate_ratio = consec_ratio_total / consec_hits;
				// update packet gap
				*ci.packet_gap = *ci.packet_gap * avg_consec_rate_ratio; 
			}
			else {
				*ci.packet_gap = *ci.packet_gap * rate_ratio;
			}
			consec_hits = 0; // reset number of consecutive hits
			consec_ratio_total = 0; // reset total ratios
		}
		send(sock, &matched, sizeof(uint8_t), 0); // informs server about current state of tput matching
	}


	printf("closing packet gap updater!\n");
	close(sock);

	pthread_exit(NULL);
}



// infinite loops and sends packets until killed
// sends packet at specified rate
int send_packets(int socket_fd, struct sockaddr_in* address, int buf_size, size_t* packet_gap, size_t* segment_total) {
	void* buf = malloc(buf_size);


	size_t pack_id = 0;
	int bytes_sent;

	for (; ; pack_id++) {
		//printf("packet_id: %li\n", pack_id);
		memcpy(buf, &pack_id, sizeof(size_t));
		if ((bytes_sent = sendto(socket_fd, buf, buf_size, 0, (struct sockaddr*)address, sizeof(*address))) != buf_size) {
			perror("sendto()");
		}
		*segment_total += bytes_sent;

		// printf("sent!\n");
		//usleep(*packet_gap); // usleep is too slow
		for(int i = 0; i < *packet_gap; i++);
	}
}

// ./udp_client <server_ip> <port> <buf_size> <bitrate> <rate_precision>
int main(int argc, char* argv[]) {
	if (argc != 7){
		fprintf(stderr, "usage: %s <server_ip_address> <port> <buf_size> <bitrate> <rate_precision> <send/recv_rate>\n", argv[0]);
		exit(1);
	}

	char* server_ip = argv[1]; // server ip
	int port = atoi(argv[2]);  // port number
	int buf_size = atoi(argv[3]);   // buf_size / packet size
	size_t target_rate = atol(argv[4]); // target bitrate
	sscanf(argv[5], "%f", &RATE_PRECISION); // target precision
	int recv_rate = atoi(argv[6]); // adjust rate based on send/recv rates

	size_t packet_gap = 1000000/(((double)target_rate)/(buf_size*8));

	int socket_fd;
	struct sockaddr_in address;

	// opens a socket 
	if ((socket_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("failed to create socket");
		exit(1);
	}
	printf("created socket!\n");

	// no need to bind because client doesn't care what port it's on

	// initializes a sockaddr_in structure to connect to
	address.sin_family = AF_INET; // IPv4
	address.sin_addr.s_addr = inet_addr(server_ip); // converts ip from char* to binary
	address.sin_port = htons(port); // converts from host to network byte ord

	size_t segment_total;

	// start update_packet_gap threads
	conn_info ci;
	ci.server_ip = server_ip;
	ci.port_num = port;
	ci.target_rate = target_rate;
	ci.packet_gap = &packet_gap;
	ci.segment_total = &segment_total;

	pthread_t update_thread;
	if (recv_rate) 
		pthread_create(&update_thread, NULL, update_packet_gap, (void*)&ci);
	else 
		pthread_create(&update_thread, NULL, update_packet_gap_from_local, (void*)&ci);
	pthread_detach(update_thread);


	printf("packet_gap(us): %li\n", packet_gap);
	// starts sending packets forever
	send_packets(socket_fd, &address, buf_size, &packet_gap, &segment_total);



	close(socket_fd);

}