#include "WorkerThread.h"

DWORD WINAPI WorkerThread(LPVOID lpParam) {
	SERVER_CONTEXT *lpServerContext = static_cast<SERVER_CONTEXT*>(lpParam);
	IO_CONTEXT *lpIOContext;
	BOOL bResult;
	int iResult;
	int wsaError;

	//mentres el serevidor estigui corrent
	while(lpServerContext->running) {
		DWORD bytesTransferred;
		ULONG_PTR completionKey;
		OVERLAPPED *pOverlapped;

		//espera indefinidament que arribi la notificació d'una compleció
		if(!(bResult = GetQueuedCompletionStatus(lpServerContext->hCompletionPort, &bytesTransferred, &completionKey, &pOverlapped, INFINITE))) {
			DWORD error = GetLastError();
			if(pOverlapped == nullptr) {
				//error intern en la pròpia GetQueuedCompletionStatus
				cout << "Error en GetQueuedCompletionStatus: " << error << endl;
			}
			else {
				//error en l'operació associada, puc recuperar context io
				lpIOContext = reinterpret_cast<IO_CONTEXT*>(pOverlapped);
				cout << "Error en operacio I/O: " << error << endl;

				//gestió segons cada cas d'operació
				switch(lpIOContext->operation) {
				case IO_OPERATION::ACCEPT: {
					closesocket(lpIOContext->acceptSocket);
					delete lpIOContext;
				} break;
				case IO_OPERATION::READ: {
					
				} break;
				case IO_OPERATION::WRITE: {

				} break;
				}
			}
		}
		else if (completionKey == COMPLETION_KEY_SHUTDOWN && pOverlapped == nullptr) {
			//ordre d'aturada des del main
			break;
		}
		else {
			//operació finalitzada correctament
			lpIOContext = reinterpret_cast<IO_CONTEXT*>(pOverlapped);

			//gestió segons cada cas d'operació
			switch(lpIOContext->operation) {
				case IO_OPERATION::ACCEPT: {
					//modificar context del socket per vincular-lo al listening socket
					iResult = setsockopt(lpIOContext->acceptSocket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, reinterpret_cast<char*>(&lpServerContext->listeningSocket), sizeof(lpServerContext->listeningSocket));
					if(iResult == SOCKET_ERROR) {
						//error en modificar context
						cout << "Error en vincular accept socket a listening socket" << endl;
						closesocket(lpIOContext->acceptSocket);
						delete lpIOContext;
					}
					else {
						//crea context client
						CLIENT_CONTEXT *lpClientContext = new CLIENT_CONTEXT();
						lpClientContext->socket = lpIOContext->acceptSocket;

						//llençar procès de lectura
						int result = LaunchReadOperation(lpClientContext);
						if(result != 0 && result != WSA_IO_PENDING) {
							cout << "Error en operacio WSARecv: " << result << endl;
							delete lpClientContext;
							closesocket(lpIOContext->acceptSocket);
						}

						//alliberar IO_CONTEXT completat
						delete lpIOContext;
					}
				} break;
				case IO_OPERATION::READ: {
					//recupero client context
					CLIENT_CONTEXT *lpClientContext = lpIOContext->clientContext;

					//detecto graceful shutdown
					if(bytesTransferred == 0) {
						cout << "El client ha tancat la connexio" << endl;
						closesocket(lpClientContext->socket);
						delete lpClientContext;
						delete lpIOContext;
					}
					else {
						//acumulo la petició
						string data(lpIOContext->readBuffer, bytesTransferred);
						lpClientContext->request.append(data);

						cout << "Complecio READ" << endl;
						cout << "Bytes rebuts: " << bytesTransferred << endl;
						cout << "Dades rebudes <" << data << ">" << endl;

						//busco final de http al request del cient
						if(lpClientContext->request.find("\r\n\r\n") != std::string::npos) {
							//preparo resposta
							lpClientContext->response = "HTTP/1.1 200 OK\r\nContent-Length: 25\r\nConnection: close\r\nr\n<h1>Servidor IOCP OK</h1>";

							//llenço WSASend
							cout << "Peticio HTTP completada" << endl;
						}
						else {
							//llenço nova WSARead
							int result = LaunchReadOperation(lpClientContext);
							if(result != 0 && result != WSA_IO_PENDING) {
								cout << "Error en operacio WSARecv: " << result << endl;
								closesocket(lpClientContext->socket);
								delete lpClientContext;
							}
						}
						//allibero IO_CONTEXT completat
						delete lpIOContext;
					}
				} break;
				case IO_OPERATION::WRITE: {

				} break;
			}
		}
	}

	return 0;
}

int LaunchReadOperation(CLIENT_CONTEXT *lpClientContext) {
	IO_CONTEXT *lpReadContext = nullptr;
	DWORD flags = 0;
	DWORD bytesReceived = 0;
	int result = 0;
	int wsaError = 0;

	//crear nou IO_CONTEXT per operació de lectura
	lpReadContext = new IO_CONTEXT();
	lpReadContext->operation = IO_OPERATION::READ;
	lpReadContext->clientContext = lpClientContext;
	lpReadContext->buffer.buf = lpReadContext->readBuffer;
	lpReadContext->buffer.len = READ_BUFFER_SIZE;

	//llençar el procés de lectura
	if((result = WSARecv(lpClientContext->socket, &lpReadContext->buffer, 1, &bytesReceived, &flags, &lpReadContext->overlapped, nullptr)) == SOCKET_ERROR) {
		if((wsaError = WSAGetLastError()) != WSA_IO_PENDING) {
			delete lpReadContext;
			lpReadContext = nullptr;
		}
		return wsaError;
	}
	else {
		return result;
	}
}

int LaunchWriteOperation(CLIENT_CONTEXT *lpClientContext) {
	IO_CONTEXT *lpWriteContext = nullptr;

	lpWriteContext = new IO_CONTEXT();
	lpWriteContext->operation = IO_OPERATION::WRITE;
	lpWriteContext->clientContext = lpClientContext;

	lpWriteContext->buffer.buf = lpClientContext->response.data();
	lpWriteContext->buffer.len = static_cast<ULONG>(lpClientContext->response.size());
}