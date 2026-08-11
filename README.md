# IOCP Winsock
Creació des de zero d'un servidor d'alt rendiment per a sistemes Windows amb I/O Completion Ports (IOCP), en llenguatge C++.

# Objectiu
L'objectiu d'aquest projecte és construir el servidor documentant cada pas i explicant les diferents parts de la implementació per entendre com funciona l'arquitectura d'IOCP i com es pot utilitzar per gestionar comunicacions de xarxa de manera asíncrona i concurrent.

# Desenvolupament
## 1. Inicialitzar winsock
Habilitar la funcionalitat de winsock per accedir als recursos de xarxa. És el procés estàndard, no hi ha cap diferència amb el que hauria fet per treballar sense IOCP.

## 2. Creació del socket d'escolta (listening socket)
Per poder treballar amb IOCP és requereix un model de socket especial, concretament WSASocket ja que aquest està preparat per treballar amb entrada i sortida sobreposades (overlapped io); aquest socket d'escolta estarà dedicat única i exclusivament a escoltar connexions d'entrada. La signatura de la funció WSASocket és:

```
SOCKET WSAAPI WSASocketA(
  [in] int                 af,
  [in] int                 type,
  [in] int                 protocol,
  [in] LPWSAPROTOCOL_INFOA lpProtocolInfo,
  [in] GROUP               g,
  [in] DWORD               dwFlags
);
```

Els tres primers arguments de la funció son els mateixos que els utilitzats per crear un socket estàndard. Els tres últims arguments son els que donen capacitat al socket a funcionar amb les funcions avançades de winsocks. El **lpProtocolInfo** és un punter a una estructura WSAPROTOCOL_INFO on es defineixen les característiques d'un proveïdor de serveis; en aquest projecte jo no n'utilitzo cap. El **g** és un GROUP que identifica (opcionalment) un grup de sockets existents o l'acció que s'ha de fer en crear un nou grup de sockets o un nou socket; en aquest projecte indico el GROUP 0 que vol dir que no s'ha de fer cap acció de grup, solament crear el socket. Finalment hi ha els **dwFlags** que son atributs addicionals; en aquest projecte utilitzo solament el flag *WSA_FLAG_OVERLAPPED* per indicar que el socket que es crei pugui funcionar amb operacions IO sobreposades (aquest atribut és requisit per poder treballar correctament amb les funcions adaptades a IOCP WSASend, WSARecv, WSASendTo, WSARecvFrom i WSAIoctl).

## 3. Associar el socket d'escolta a l'adressa d'escolta
El socket d'escolta (listening socket) ha d'escoltar una adressa per poder acceptar connexions entrants. Aquesta funció és la mateixa que per sockets no IOCP i el seu funcionament també és el mateix.

## 4. Crear el IOCP
EL IOCP és l'anomenat port de finalització, és crea amb la funció **CreateIoCompletionPort** i permet que una llista de *workers* assumeixin la gestió de les operacions en els sockets. Aquesta gestió és la que diferencia el model IOCP del model de sockets habitual: disposa una cua de tasques que aniran assumint els *workers* de forma asincrona. En una primera instància és crea el IOCP completament buit per a que de forma interna es crei una estructura de cua i el seu gestor de concurrència, sense vincular cap font a la cua.

Hi ha un paràmetre de la funció **CreateIoCompletionPort** que és molt important, concretament l'últim que és *DWORD NumberOfConcurrentThreads*: aquest paràmetre defineix el nombre de processos que el sistema utilitzarà de manera concurrent, sent el valor 0 l'indicador d'utilitzar el màxim de processos possibles per l'estructura del maquinari on s'executa (això vol dir que utilitzarà el nombre de processadors logics del maquinari). És important ja que després crearé els *workers* i el nombre d'aquests haurà de coincidir amb l'utilitzat per l'IOCP sense malbaratar recursos del sistema. En aquest projecte utilitzaré precisament el valor 0 per treure el màxim rendiment del sistema.

## 5. Calcular el nombre de *workers* a utilitzar
Tal com he comentat al punt anterior, hauré de crear tants *workers* o fils de procés com els que utilitza internament el gestor de fils d'IOCP. Com que li he indicat que utilitzi els màxims en aquest pas faré el càlcul manual per determinar aquest valor. Les funcions de la Win32 API em permeten accedir al la informació del sistema i consultar aquest valor de forma directa:
```
GetSystemInfo(&systemInfo);
workerCount = systemInfo.dwNumberOfProcessors;
```

## 6. Crear els fils de procés (*workers*)
Un cop determinat el nombre de fils a utilitzar ja els puc crear, assignant-los la funció que gestionarà la resolució de totes les situacions possibles (operacions de lectura o escriptura). Aquests fils son de tipus *HANDLE*, i en aquest projecte els guardo dins d'un vector. Els fils tenen aquesta signatura:

```
DWORD WINAPI WorkerThread(LPVOID lpParam);
```

L'argument *lpParam* és un valor que cal personalitzar per adaptar-lo a les necessitats del projecte. D'entrada en el meu projecte envio un punter a una estructura a mode de context de servidor, on guardo el port de finalització (IOCP) i el socket d'escolta:

```
struct SERVER_CONTEXT {
	HANDLE hCompletionPort = nullptr;
	SOCKET listeningSocket = INVALID_SOCKET;
};
```

Igual que qualsevol procés de Windows retornarà un codi d'error (sent 0 el cas de procés sense errors).

## 7. Declarar el processador de fils (*worker*)
El processador de fils és el *worker* que resoldrà les situacions de cada connexió acceptada. Tal com he comentat al punt anterior aquest *worker* rep un punter a una estructura de context del servidor (actualment i com a punt d'entrada conté el port de finalització IOCP i el socket d'escolta). Aquest procés anirà creixent a mesura que avança el projecte ja qui hi haurà tota la funcionalitat per gestionar les connexions acceptades pel serevidor. Inicialment té aquesta forma bàsica:

```
DWORD WINAPI WorkerThread(LPVOID lpParam) {
	SERVER_CONTEXT *lpServerContext = static_cast<SERVER_CONTEXT*>(lpParam);

	DWORD bytesTransferred = 0;
	ULONG_PTR completionKey = 0;
	OVERLAPPED *pOverlapped = nullptr;

	return 0;
}
```

Per ara no fa res, simplement retorna 0 sense interaccionar ni amb el IOCP ni amb el client. El punt crític és el cast de *lpParam* a punter a *SERVER_CONTEXT*, cal tenir clar quin valor s'envia com a paràmetre per poder recuperar-lo com a argument. A dins de la funció ja hi deixo preparades algunes variables que s'utilitzen de forma genèrica per processar els estats del client.

**Important** Hi ha un fet remarcable que cal tenir present en el model mental a l'hora de dissenyar el *worker*: el fil no gestionarà un socket concret, sinó que es crearà un procés i se li assignarà un socket de treball, una operació i un estat de l'operació; això vol dir que no serà un fil que gestionarà la vida d'una connexió, sinó que gestionarà parts del cicle d'un o varis sockets que entraran de forma concurrent.

## 8. Identificar el flux de dades del procés
En aquest punt del projecte el servidor encara no accepta cap client. Per poder gestionar els clients cal tenir clar el flux de dades, i cal tenir encara mes clar que a diferència dels sockets tradicionals el model IOCP permet que un mateix client tingui operacions de lectura i escriptura pendents, inclús es pot donar el cas que dos fils processin dues operacions diferents d'un mateix client. Per tant ara és el moment d'incloure la relació d'estructures que permetin fer la gestió que respon a les següents preguntes: 

- Qui és el client?
- Quina operació requereix aquest client?

Estableixo la nomenclatura del *context* per identificar i separar àmbits de referència de dades; declararé tres estructures de context: la del servidor, la del client i la de l'operació asíncrona.

- Context del servidor: per ara només hi tinc el port de finalització (iocp) i el socket d'escolta
- Context del client: contindrà totes les dades referents al client, inicialment el socket de comunicació
- Context d'operació asíncrona: contindrà totes les dades referents a l'operació sobre un client, per tant inclourà el context del client i la descripció d'un tipus d'operació.

Per ara aquestes estructures seran les següents:
```
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
```
I el tipus d'operació estarà identificada per aquest enum:
```
enum class IO_OPERATION {
	ACCEPT,
	READ,
	WRITE
};
```



