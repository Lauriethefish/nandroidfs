#include <iostream>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "UnixException.hpp"
#include "ClientHandler.hpp"
#include "requests.hpp"

using namespace nandroidfs;

int server_sock = -1;
void handle_client(int client_sock) {
	try
	{
		ClientHandler handler(client_sock);
		handler.handle_messages();
		std::cout << "Goodbye, handled connection so shutting down" << std::endl;
	}
	catch(const std::exception& e)
	{
		std::cerr << "Disconnected from client due to an error: " << e.what() << std::endl;
	}
}

void start_server() {
	sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(NANDROID_PORT);
	addr.sin_addr.s_addr = inet_addr("0.0.0.0");
	server_sock = throw_unless(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
	try
	{
		throw_unless(bind(server_sock, (struct sockaddr*) &addr, sizeof(addr)) == -1);
		throw_unless(listen(server_sock, 1) == -1);

		std::cout << "Binded successfully to port, awaiting connection" << std::endl;
		std::cout << NANDROID_READY << std::endl;

		// Keep accepting requests continuously
		int client_sock = throw_unless(accept(server_sock, nullptr, nullptr));
		handle_client(client_sock);
	}
	catch(const std::exception& e)
	{
		// Ensure the socket is closed, even if an exception is thrown.
		close(server_sock);
		throw;
	}
}

int main() {
	try
	{
		start_server();
	}
	catch(const std::exception& e)
	{
		std::cerr << "Failed to execute daemon: " << e.what() << std::endl;
	}
}
