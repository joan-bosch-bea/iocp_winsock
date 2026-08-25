#include "WorkerThread.h"

DWORD WINAPI WorkerThread(LPVOID lpParam) {
	SERVER_CONTEXT *lpServerContext = static_cast<SERVER_CONTEXT*>(lpParam);
	IO_CONTEXT *lpIOContext;
	BOOL bResult;
	int iResult;

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
				//modifica context del socket per vincular-lo al listening socket
				iResult = setsockopt(lpIOContext->acceptSocket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, reinterpret_cast<char*>(&lpServerContext->listeningSocket), sizeof(lpServerContext->listeningSocket));
				if(iResult == SOCKET_ERROR) {
					//error en modificar context
					closesocket(lpIOContext->acceptSocket);
					delete lpIOContext;
				}
				else {
					//nova instancia de CLIENT_CONTEXT
					CLIENT_CONTEXT *lpClientContext = new CLIENT_CONTEXT();
					lpClientContext->socket = lpIOContext->acceptSocket;

					//allibero IO_CONTEXT completat
					delete lpIOContext;

					//creo nou IO_CONTEXT per operació de lectura
					IO_CONTEXT *lpReadContext = new IO_CONTEXT();
					lpReadContext->operation = IO_OPERATION::READ;
					lpReadContext->clientContext = lpClientContext;
				}
			} break;
			case IO_OPERATION::READ: {

			} break;
			case IO_OPERATION::WRITE: {

			} break;
			}
		}
	}

	return 0;
}