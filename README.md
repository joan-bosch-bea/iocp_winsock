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
## 9. Descripció d'OVERLAPPED
L'estructura *OVERLAPPED* està definida a *minwinbase.h* i en entorns Windows representa una operació I/O que es duu a terme de forma asíncrona. La seva importància rau en el fet que permet identificar a qui pertanyen els resultats d'una operació asíncrona. És pot interpretar com una referència a l'operació o, dit d'una altra manera' és com internament Windows identifica una operació asíncrona.

El funcionament d'aquest OVERLAPPED és important. A nivell d'imatge mental es podria dir que OVERLAPPED és equivalent a un UNIQUE d'una base de dades, de manera que cada operació és identificada per una referència. Però el que fa especialment interessant la solució de Windows és que OVERLAPPED no necessita una taula d'assignacions (mapa), sinó que retorna directament l'adreça de memòria on està l'operació; això implica que no cal fer una cerca per trobar l'operació (millora de rendiment).

## 10. Organització de memòria
Per poder fer funcionar aquest esquema cal organitzar l'estructura del context de l'operació *IO_CONTEXT* d'una forma determinada: el primer component de l'estructura ha de ser l'OVERLAPPED, i la resta els que jo defineixo. L'estructura s'organitza en memòria de forma seqüencial a com estan declarats els seus membres:
```
struct IO_CONTEXT {
	OVERLAPPED overlapped{};
	WSABUF buffer{};
	IO_OPERATION operation;
	CLIENT_CONTEXT *client = nullptr;
};
```
El bloc de l'estructura es guardarà en memòria a la mateixa adressa que el seu primer membre, que en aquest cas és OVERLAPPED. Com que OVERLAPED serà l'adressa de memòria de l'operació posteriorment es pot accedir als components de l'estructura accedint a l'adressa de memòria de l'OVERLAPPED; de fet C++ ja incorpora la macro *CONTAINING_RECORD* que permet fer exactament el pas d'identificar a quin objecte pertany un membre.

## 11. Vida de IO_CONTEXT
L'estructura IO_CONTEXT ha d'existir en memòria des de que es llença el procés fins, com a mínim, que acabi; per tant això afecta al disseny. Cal separar el context del client del context de l'operació: el context del client es considera de llarga durada, en canvi el context de l'operació es considera de curta durada. Això implica que el context del client no es pot destruir fins que no hagin acabat totes les operacions pendents relacionades amb aquell client.
La idea bàsica és que una connexió (client) pot tenir moltes operacions al llarg de la seva vida, i cada operació tindrà el seu propi context amb el seu propi OVERLAPPED.

## 12. Esquema de GetQueuedCompletionStatus
Abans d'implementar mes codi encara falta entendre que fa la funció *GetQueuedCompletionStatus()* i per què és important. Aquesta funció fa que el worker pugui esperar a que una operació I/O finalitzi. Aquesta funció està escoltant els moviment sobre la cua de *completions* (la cua de tasques completades). Té varis arguments, però els més rellevants son:
+ HANDLE CompletionPort: és l'iocp que s'està utilitzant
+ LPDWORD lpNumberOfBytesTransferred: bytes que s'han intercanviat (llegit o escrit)
+ PULONG_PTR lpCompletionKey: informació pròpia que associo al HANDLE al moment de connectar al iocp
+ LPOVERLAPPED *lpOverlapped: zona de memòria de l'operació, tal com he explicat anteriorment als punts **9** i **10**

El fet de treballar amb fils permet que aquest s'adormi fins que *GetQueuedCompletionStatus()* el desperta per processar l'operació. Aquesta és la diferència clau entre fer polling (gastar cpu) i bloquejar / desbloquejar fils.

## 13. Clau de compleció (completion key)
Al punt anterior he mencionat l'argument PULONG_PTR lpCompletionKey. En aquest punt vaig a revisar quin és exactament el seu paper en aquest esquema que he anat desgranant. La clau de compleció identifica l'objecte (HANDLE) associat al IOCP. No s'ha de confondre amb OVERLAPPED, que identifica l'operació.
La clau de compleció és una clau que definiré jo dins del codi, i que s'utilitza per associar HANDLEs al port de compleció. Al codi per ara tinc aquesta crida:

```
serverContext.hCompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
```
En aquesta linia de codi només es crea un iocp, però no se li envia cap socket. Per assignar un socket al IOCP utilitzaré la mateixa funció, excepte que enviaré (per ordre) el socket client, el port de compleció amb el que vull treballar, la clau de compleció i el valor 0. En el meu projecte utilitzaré com a clau de compleció un punter al context del client, d'aquesta manera podré recuperar el context del client en el que s'ha fet l'operació: la clau de compleció em donarà informació sobre el client i l'OVERLAPPED em donarà informació sobre l'operació. El fet d'enviar el punter al context del client com a clau de compleció m'estalviarà haver de gestionar una taula paral·lela (mapa) amb les relacions de clau de compleció - context de client; he seguit la mateixa filosofia del model del OVERLAPPED.

## 14. Preparar socket per AcceptEx
La funció *AcceptEx* funciona d'una forma diferent a la funció *accept* del sockets tradicionals. La diferència principal és que mentre *accept* retorna un SOCKET, *AcceptEx* espera que se li proposi un SOCKET ja existent però no inicialitzat. Per tant el socket del client és crea abans d'acceptar el client, i, per tant, abans d'iniciar el procés asíncron que gestionarà el client, amb la qual cosa m'asseguro que sobrevisqui mentre no hagi finalitzat la gestió del client. Per aquest motiu afegeixo el nou SOCKET destinat a l'acceptació del client:

```
struct IO_CONTEXT {
	OVERLAPPED overlapped{};
	WSABUF buffer{};
	IO_OPERATION operation;
	CLIENT_CONTEXT *client = nullptr;
	SOCKET acceptSocket = INVALID_SOCKET;
};
```

Pel que fa a l'anàlisi del model que he implementat que respon a la pregunta: perquè l'*acceptSocket* que serà el socket de comunicació amb el client setà al IO_CONTEXT i no al CLIENT_CONTEXT? La pròpia funció *AcceptEx()* m'ha donat la resposta a aquesta pregunta: necessita un socket obert no lligat (bind) a cap adressa ni connectat. Per tant encara que després l'assigni al socket client aquesta variable a nivell general no està associada a cap client concret.

## 15. La funció AcceptEx
La definició de la funció *AcceptEx* es pot trobar a la pàgina de [documentació de Winsocks]([https://pages.github.com/](https://learn.microsoft.com/es-es/windows/win32/api/winsock/nf-winsock-acceptex)) i és la següent:

> Esta función es una extensión específica de Microsoft para la especificación de Windows Sockets.

El fet de ser una extensió vol dir que no es pot cridar directament, sinó que se n'ha d'obtenir un punter per poder-la cridar. Aquest punter s'ha d'obtenir cridant a la funció *WSAIoctl()*, també tal com s'indica a la [pàgina de referència de AcceptEx](https://learn.microsoft.com/es-es/windows/win32/api/winsock/nf-winsock-acceptex):

> El puntero de función para la función AcceptEx debe obtenerse en tiempo de ejecución realizando una llamada a la función WSAIoctl con el código de operación SIO_GET_EXTENSION_FUNCTION_POINTER especificado. El búfer de entrada pasado a la función WSAIoctl debe contener WSAID_ACCEPTEX, un identificador único global (GUID) cuyo valor identifica la función de extensión AcceptEx . Si se ejecuta correctamente, la salida devuela por la función WSAIoctl contiene un puntero a la función AcceptEx. El GUID de WSAID_ACCEPTEX se define en el archivo de encabezado Mswsock.h

Llavors la crida a *WSAIoctl* tindrà la següent forma:

```
WSAIoctl(
	listeningSocket, //socket sobre el que vull l'extensió
	SIO_GET_EXTENSION_FUNCTION_POINTER,//vull un punter a la funció
	&guidAcceptEx,//identificador de la funció de la que en vull obtenir el punter
	sizeof(guidAcceptEx),//tamany del valor anterior
	&serverContext.lpfnAcceptEx,//destí del punter a la funció
	sizeof(serverContext.lpfnAcceptEx),//tamany del destí anterior
	&bytesReturned,//bytes escrits al buffer de sortida, en aquesta crida aquest valor no m'aportarà cap informació
	nullptr, nullptr
);
```

Un cop ja disposo del punter a la funció em faltrà encara el socket per acceptar les connexions (al codi serà l'*acceptSocket*), que al igual que he fet anteriorment amb el *listeningSocket* també el crearé amb *WSASocket()*, i, de fet, el crearé amb els mateixos paràmetres:

```
serverContext.listeningSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
acceptSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
```

La clau d'aquests dos sockets i la funció *AcceptEx()* passa per entendre correctament la seva relació: la funció *AcceptEx()* agafarà una connexió que arribi al *listeningSocket* i farà que l'*acceptSocket* passi a representar aquesta connexió.

En aquest punt encara no tinc cap socket associat al iocp, per tant abans de poder cridar a *AcceptEx()* hauré de fer aquesta assignació. Amb la mateixa funció que he utilitzat anteriorment *CreateIoCompletionPort()* amb un socket no vàlid per crear l'IOCP, ara la torno a cridar però indicant-li que vull utilitzar el nou socket *acceptSocket* al port de compleció (també creat anteriorment) *serverContext.hCompletionPort*:

```
CreateIoCompletionPort(reinterpret_cast<HANDLE>(acceptSocket), serverContext.hCompletionPort, 0, 0)
```

