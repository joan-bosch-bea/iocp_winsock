#ifndef _HSTRUCTSH
#define _HSTRUCTSH

enum class IO_OPERATION {
	ACCEPT,
	READ,
	WRITE
};

constexpr DWORD ADDRESS_BUFFER_SIZE = sizeof(sockaddr_in) + 16;
constexpr DWORD ACCEPT_BUFFER_SIZE = ADDRESS_BUFFER_SIZE * 2;


struct SERVER_CONTEXT {
	HANDLE hCompletionPort = nullptr;
	SOCKET listeningSocket = INVALID_SOCKET;
	LPFN_ACCEPTEX lpfnAcceptEx = nullptr;
};

struct CLIENT_CONTEXT {
	SOCKET socket = INVALID_SOCKET;
};

struct IO_CONTEXT {
	OVERLAPPED overlapped{};
	WSABUF buffer{};
	IO_OPERATION operation;
	CLIENT_CONTEXT *client = nullptr;
	SOCKET acceptSocket = INVALID_SOCKET;
	char acceptBuffer[ACCEPT_BUFFER_SIZE]{};
};

#endif