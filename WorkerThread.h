#ifndef _WORKERTHREADH
#define _WORKERTHREADH

#ifndef WIN32_LEAN_AND_MEAN
	#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <Ws2tcpip.h>
#include <mswsock.h>
#include <iostream>
#include "structs.h"
using namespace std;

DWORD WINAPI WorkerThread(LPVOID lpParam);

#endif