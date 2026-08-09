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

DWORD WINAPI WorkerThread(LPVOID lpParam) {
	SERVER_CONTEXT *lpServerContext = static_cast<SERVER_CONTEXT*>(lpParam);

	DWORD bytesTransferred = 0;//bytes transferits durant l'operació, si el client tanca la connexió els bytes son 0
	ULONG_PTR completionKey = 0;
	OVERLAPPED *pOverlapped = nullptr;//per diferenciar operacions pendents de lectura o escriptura

	return 0;
}

int main() {
	WSADATA wsaData;
	sockaddr_in serverAddress{};
	vector<HANDLE> hWorkersVector;
	SERVER_CONTEXT serverContext;
	SYSTEM_INFO systemInfo;
	DWORD workerCount;

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

	//crear els fils de procés
	GetSystemInfo(&systemInfo);
	workerCount = systemInfo.dwNumberOfProcessors;
	for(DWORD i = 0; i < workerCount; i++) {
		HANDLE hThread = CreateThread(nullptr, 0, WorkerThread, &serverContext, 0, nullptr);
		if(hThread != nullptr) {
			hWorkersVector.push_back(hThread);
		}
		else {
			cout << "Error en crear el procès #" << i << endl;
		}
	}
}