/*** Includes ***/
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/socket.h>

#include "common.c"

#define BUFF_SIZE 64

/*** Logging utils ***/

/*** Client ***/
char* GetUserInput(char* input){
	int n = 0;
	while ((input[n++] = getchar()) != '\n');
	return input;
}

int SendToServer(const char* url, const int port, const char* msg) {
	// Create socket
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == -1) Die("SendToServer - socket");
	struct sockaddr_in address;
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = inet_addr(url);
	address.sin_port = htons(port);

	// Open connection
	if(connect(sock, (struct sockaddr*)&address, (socklen_t) sizeof(address)) < 0) Die("SendToServer - connect");

	write(sock, msg, strlen(msg));
	char buff[100];
	memset(&buff, 0, sizeof(buff));
	read(sock, buff, sizeof(buff));
	close(sock);

	printf("%s\n", buff);
	return 0;
}

// Test our server
int main(){
	int port = 9999;
	const char url[] = "127.0.0.1";
	while (1) {
		char msg[BUFF_SIZE];
		memset(msg, 0, sizeof(msg));
		GetUserInput(msg);
		SendToServer(url, port, msg);
	}
	return 0;
}
