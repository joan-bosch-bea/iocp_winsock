#ifndef WIN32_LEAN_AND_MEAN
	#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <Ws2tcpip.h>
#include <vector>
#include <iostream>
using namespace std;

struct SERVER_CONTEXT {
	HANDLE hCompletionPort = nullptr;
	SOCKET listeningSocket = INVALID_SOCKET;
};

int main() {
	WSADATA wsaData;
	sockaddr_in serverAddress{};
	vector<HANDLE> hWorkersVector;
	SERVER_CONTEXT serverContext;

	//inicialitza winsock
	if(WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		cout << "Error incialitzant llibreria Winsock v2.2" << endl;
		return 1;
	}

	//listening socket
	serverContext.listeningSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
	if(serverContext.listeningSocket == INVALID_SOCKET) {
		cout << "Error en crear el listening socket" << endl;
		WSACleanup();
		return 1;
	}

	//associar el listeningSocket a l'adressa
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_port = htons(8080);
	serverAddress.sin_addr.s_addr = INADDR_ANY;
	if(bind(serverContext.listeningSocket, reinterpret_cast<sockaddr*>(&serverAddress), sizeof(serverAddress)) == SOCKET_ERROR) {
		closesocket(serverContext.listeningSocket);
		WSACleanup();
		return 1;
	}

	//iniciar l'escolta
	if(listen(serverContext.listeningSocket, SOMAXCONN) == SOCKET_ERROR) {
		cout << "Error en iniciar escolta" << endl;
		closesocket(serverContext.listeningSocket);
		WSACleanup();
		return 1;
	}

	//crear IOCP
	serverContext.hCompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
	if(serverContext.hCompletionPort == nullptr) {
		cout << "Error en crear IOCP" << endl;
		closesocket(serverContext.listeningSocket);
		WSACleanup();
		return 1;
	}
}