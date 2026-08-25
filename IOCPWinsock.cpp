#ifndef WIN32_LEAN_AND_MEAN
	#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <Ws2tcpip.h>
#include <mswsock.h>
#include <vector>
#include <iostream>
#include "structs.h"
#include "WorkerThread.h"
using namespace std;



int main() {
	WSADATA wsaData;
	sockaddr_in serverAddress{};
	vector<HANDLE> hWorkersVector;
	SERVER_CONTEXT serverContext;
	SYSTEM_INFO systemInfo;
	DWORD workerCount;
	IO_CONTEXT *lpIOContext;
	BOOL result;
	DWORD bytesReceived = 0;

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

	//obtenir punter a acceptex
	GUID guidAcceptEx = WSAID_ACCEPTEX;
	DWORD bytesReturned = 0;
	if(WSAIoctl(serverContext.listeningSocket, SIO_GET_EXTENSION_FUNCTION_POINTER, &guidAcceptEx, sizeof(guidAcceptEx), &serverContext.lpfnAcceptEx, sizeof(serverContext.lpfnAcceptEx), &bytesReturned, nullptr, nullptr) == SOCKET_ERROR) {
		cout << "Error obtenint AcceptEx: " << WSAGetLastError() << endl;
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
	serverContext.running = true;

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

	//instanciar IO_CONTEXT
	lpIOContext = new IO_CONTEXT();
	lpIOContext->operation = IO_OPERATION::ACCEPT;
	lpIOContext->acceptSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
	if(lpIOContext->acceptSocket == INVALID_SOCKET) {
		cout << "Error en crear accept socket" << endl;
		closesocket(serverContext.listeningSocket);
		delete lpIOContext;
		WSACleanup();
		return 1;
	}

	//associar acceptSocket a iocp
	if(CreateIoCompletionPort(reinterpret_cast<HANDLE>(lpIOContext->acceptSocket), serverContext.hCompletionPort, 0, 0) == nullptr) {
		cout << "Error associant acceptSocket a IOCP" << endl;
		closesocket(lpIOContext->acceptSocket);
		closesocket(serverContext.listeningSocket);
		CloseHandle(serverContext.hCompletionPort);
		delete lpIOContext;
		WSACleanup();
		return 1;
	}

	//cridar a AcceptEx
	result = serverContext.lpfnAcceptEx(serverContext.listeningSocket, lpIOContext->acceptSocket, lpIOContext->acceptBuffer, 0, ADDRESS_BUFFER_SIZE, ADDRESS_BUFFER_SIZE, &bytesReceived, &lpIOContext->overlapped);
	if(result) {
		cout << "AcceptEx completat immediatament" << endl;
	}
	else {
		DWORD error = WSAGetLastError();
		if(error == ERROR_IO_PENDING) {
			cout << "AcceptEx pendent..." << endl;
		}
		else {
			cout << "Error en AcceptEx: " << error << endl;
		}
	}
}