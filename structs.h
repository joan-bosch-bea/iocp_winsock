#ifndef _HSTRUCTSH
#define _HSTRUCTSH

enum class IO_OPERATION {
	ACCEPT,
	READ,
	WRITE
};

struct SERVER_CONTEXT {
	HANDLE hCompletionPort = nullptr;
	SOCKET listeningSocket = INVALID_SOCKET;
};

struct CLIENT_CONTEXT {
	SOCKET socket = INVALID_SOCKET;
};

struct IO_CONTEXT {
	OVERLAPPED overlapped{};
	WSABUF buffer{};
	IO_OPERATION operation;
	CLIENT_CONTEXT *client = nullptr;
};

#endif