#ifndef _HSTRUCTSH
#define _HSTRUCTSH

enum class IO_OPERATION {
	ACCEPT,
	READ,
	WRITE
};

constexpr DWORD ADDRESS_BUFFER_SIZE		= sizeof(sockaddr_in) + 16;
constexpr DWORD ACCEPT_BUFFER_SIZE		= ADDRESS_BUFFER_SIZE * 2;
constexpr DWORD READ_BUFFER_SIZE		= 4096;

constexpr ULONG_PTR COMPLETION_KEY_SHUTDOWN	= 1000;


struct SERVER_CONTEXT {
	HANDLE hCompletionPort		= nullptr;
	SOCKET listeningSocket		= INVALID_SOCKET;
	LPFN_ACCEPTEX lpfnAcceptEx	= nullptr;
	bool running;
};

struct CLIENT_CONTEXT {
	SOCKET socket = INVALID_SOCKET;
};

struct IO_CONTEXT {
	OVERLAPPED overlapped{};
	WSABUF buffer{};
	IO_OPERATION operation;
	CLIENT_CONTEXT *clientContext = nullptr;
	SOCKET acceptSocket = INVALID_SOCKET;
	char acceptBuffer[ACCEPT_BUFFER_SIZE]{};
	char readBuffer[READ_BUFFER_SIZE]{};
};

#endif