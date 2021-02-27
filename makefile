CC = gcc
OPTIMIZE = -O2
#CFLAGS = $(OPTIMIZE) -pthread -g -lm
CFLAGS = -g 
# -pthread - pthread support
# -g - generate debug info
# -O2 - optimization level
# -lm - necessary for math.h


default: server.c client.c
	@make client
	@make server

client: client.c
	@echo "building client..."
	@$(CC) -o udp_client client.c  -lpthread
	@chmod +x udp_client  
	@echo "finished building client"

server: server.c
	@echo "buildling server.."
	@$(CC) -o udp_server server.c -lpthread
	@chmod +x udp_server
	@echo "finished building server"