# IOCP Winsock
Creació des de zero d'un servidor d'alt rendiment per a sistemes Windows amb I/O Completion Ports (IOCP), en llenguatge C++.

# Objectiu
L'objectiu d'aquest projecte és construir el servidor documentant cada pas i explicant les diferents parts de la implementació per entendre com funciona l'arquitectura d'IOCP i com es pot utilitzar per gestionar comunicacions de xarxa de manera asíncrona i concurrent.

# Fonaments d'IOCP
**IOCP** (Input Output Completion port) és un mecanisme propi del nucli de Windows que permet associar operacions d'I/O asíncrones amb una cua de complecions, i també permet que diversos processos accedeixin a aquesta cua per processar-les. La paraula *port* de IOCP fa referència al terme que Windows utilitza per identificar el sistema de distribució (i sincronització) de les notificacions de finalitzacions de processos asíncrons.
**Completion** (o compleció) és el resultat d'una operació asíncrona d'entrada / sortida que el sistema Windows posa a la cua de l'IOCP quan aquesta ha finalitzat.

Per tant es pot simplificar el model de funcionament dient que primer es genera una operació d'I/O asíncrona; la funció que registra l'operació asíncrona finalitza immediatament però no l'operació en si. Quan l'operació acaba s'envia una notificació junt amb les dades de l'operació al port de compleció (prèviament preparat), i un procediment (o varis) accedeixen a la cua de notificacions d'operacions finalitzades per processar els resultats. Aquest processament pot generar noves operacions asíncrones que al seu moment finalitzaran i seran enviades a la cua de complecions i un procediment (el mateix o un altre) processarà aquest resultat.

Les operacions asíncrones porten vinculades les dades necessàries per fer tot el seguiment i persisteixen durant tota l'operació. En el cas del servidor les dades importants son la zona de memòria on s'executa l'operació i les dades del client; com que es pot donar el cas que per servir una petició d'un client calguin varies operacions d'IO asincrones, caldrà que la seva informació persisteixi fins al final del procés. Per tant cada com que el procediment recupera les dades de l'operació i n'inicia una de nova vinculada al mateix client (per exemple llegeix el request però no el reb tot, ha d'iniciar una nova operació de lectura) ha de propagar les dades del client a la nova operació per a que el proper procediment pugui identificar el client i la situació en que es troba.

És un model que consumeix pocs recursos de CPU, ja que enlloc de fer *polling* esperant una notificació utilitza una funció que literalment desperta el procediment, de manera que els procediments esperen de forma passiva a ser despertats.

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
Per poder fer funcionar aquest esquema cal organitzar l'estructura del context de l'operació *IO_CONTEXT* d'una forma determinada: el primer component de l'estructura ha de ser l'OVERLAPPED, i la resta els que jo defineixo. L'estructura s'organitza en memòria de forma seqüencial a com estan declarats els seus membres (el compilador pot afegir padding entre els membres):
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

## 15.1. Obtenir punter a l'extensió d'AcceptEx
Tal com he comentat al punt anterior no es pot accedir directament a la funció *AcceptEx()*, sinó que s'ha de fer la crida a través d'un punter que puc obtenir des de *WSAIoctl()*. La crida a *WSAIoctl* tindrà la següent forma:

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

## 15.2. Crear accept socket
Un cop ja disposo del punter a la funció em faltrà encara el socket per acceptar les connexions (al codi serà l'*acceptSocket*), que al igual que he fet anteriorment amb el *listeningSocket* també el crearé amb *WSASocket()*, i, de fet, el crearé amb els mateixos paràmetres:

```
serverContext.listeningSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);//linia de codi ja implementada anteriorment
acceptSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
```

La clau d'aquests dos sockets i la funció *AcceptEx()* passa per entendre correctament la seva relació: la funció *AcceptEx()* agafarà una connexió que arribi al *listeningSocket* i farà que l'*acceptSocket* passi a representar aquesta connexió.

## 15.3. Associar l'accept socket al port de compleció
En aquest punt encara no tinc cap socket associat al iocp, per tant abans de poder cridar a *AcceptEx()* hauré de fer aquesta assignació. Amb la mateixa funció que he utilitzat anteriorment *CreateIoCompletionPort()* amb un socket no vàlid per crear l'IOCP, ara la torno a cridar però indicant-li que vull utilitzar el nou socket *acceptSocket* al port de compleció (també creat anteriorment) *serverContext.hCompletionPort*:

```
CreateIoCompletionPort(reinterpret_cast<HANDLE>(acceptSocket), serverContext.hCompletionPort, 0, 0)
```

Un cop feta aquesta crida a *CreateIoCompletionPort()* totes les notificacions d'operacions sobreposades sobre el socket *acceptSocket* s'enviaran al port de compleció *serverContext.hCompletionPort*. El tercer argument és la **clau de compleció**: aquest clau de compleció em servirà per identificar la connexió del client, però com que de moment no en tinc cap puc indicar el valor 0 per defect. I finalment quart argument és el nombre màxim de processos que el sistema permetrà executar de forma concurrent, i el valor 0 equival al nombre de nuclis del processador.

## 15.4. Instanciar IO_CONTEXT
El *IO_CONTEXT* del meu projecte és una estructura que representa una operació (qualsevol) d'entrada / sortida (I/O) pendent. Tal com he comentat al punt 10 l'organització dels membres d'aquesta estructura no és aleatòria, sinó que respon al meu esquema de disseny (es pot fer d'altres formes). Per tant creo una instància de *IO_CONTEXT* amb l'operació pendent *IO_OPERATION::ACCEPT* i l'*acceptSocket* que he creat al punt 15.2:

```
ioContext = new IO_CONTEXT();
ioContext->operation = IO_OPERATION::ACCEPT;
ioContext->acceptSocket = acceptSocket;
```

En aquest punt cal matissar una cosa important: l'operació *AcceptEx()* no utilitza el membre *WSABUF buffer{}* de l'estructura *IO_CONTEXT*, però les crides a *WSARecv()* i *WSASend()* si que el necessitaran; per tant m'avanço una mica als esdeveniments i ja deixo aquest membre dins l'estructura. El mateix passa amb *CLIENT_CONTEXT*.

També en aquest punt hi ha una decisió de disseny important que ja he comentat anteriorment només de passada: el IO_CONTEXT ha de persistir fins que hagi finalitzat l'operació, per tant qui serà l'encarregat de crear-lo i eliminar-lo? I quan? El fer d'implementar un servidor d'alt rendiment implica no abusar d'operacions *new / delete*, sinó que el mes convenient és utilitzar un pool de contextes reutilitzables previament creats. Però per ara em permeto la llicència de centrar-me en la implementació funcional del model IOCP i deixar la implementació del pool de contextes per una posterior millora del projecte.

## 15.5. Cridar a AcceptEx
És important entendre exactament la funció *AcceptEx()*, sobretot els arguments que espera. La seva signatura es pot trobar a l'[API de Win32](https://learn.microsoft.com/ca-es/windows/win32/api/winsock/nf-winsock-acceptex), i és la següent:

```
BOOL AcceptEx(
  [in]  SOCKET       sListenSocket,
  [in]  SOCKET       sAcceptSocket,
  [in]  PVOID        lpOutputBuffer,
  [in]  DWORD        dwReceiveDataLength,
  [in]  DWORD        dwLocalAddressLength,
  [in]  DWORD        dwRemoteAddressLength,
  [out] LPDWORD      lpdwBytesReceived,
  [in]  LPOVERLAPPED lpOverlapped
);
```

Els dos primers arguments son molt evidents: **sListenSocketel** és el socket d'escolta (la porta d'entrada al servidor), i **sAcceptSocket** és el socket acceptat (client entrant). Son els mateixos que utilitzaria en un servidor normal.
**lpOutputBuffer** és on es rep informació de la connexió, que pot ser: l'adreça local, l'adreça remota i opcionalment dades que el client envia immediatament després de connectar.
**dwReceiveDataLength** té a veure amb l'argument anterior: indica el nombre de bytes que es volen rebre (o mes ben dit, intentar rebre) del client juntament amb l'acceptació de connexió. En aquest projecte jo indicaré que durant l'acceptació de connexió entrant no vull rebre cap dada extra, ja faré les operacions de lectura després.
**dwLocalAddressLength** és el nombre de bytes que es guarden dins de **lpOutputBuffer** per guardar l'adreça local. Això em porta a la constant **ADDRESS_BUFFER_SIZE** i al membre **char acceptBuffer[ACCEPT_BUFFER_SIZE]{}** de l'estructura *IO_CONTEXT*: l'API de Windows indica explícitament que aquest buffer ha de ser suficient per l'adreça mes 16 bytes addicionals. Per tant al **+16** no és un valor aleatori ni orientatiu ni ajustable després de fer proves.
**dwRemoteAddressLength** és el mateix que l'anterior però per l'adreça remota.
**lpdwBytesReceived** aquí es guardarien el nombre de bytes rebuts, però com que és una operació sobreposada (OVERLAPPED) quan aquesta funció retorni no sabré quants bytes he rebut, sinó que m'hauré d'esperar a la notificació d'operació i recuperar-los amb *GetQueuedCompletionStatus()*; entraré amb mes detall sobre aquest punt mes endavant.
**lpOverlapped** és l'argument que dona sentit a l'operació sobreposada (OVERLAPPED), i, de fet, és el que converteix la crida a *AcceptEx()* en una operació asincrona. Tal com he comentat anteriorment quan l'operació sobreposada acabi IOCP em permetrà accedir a aquest OVERLAPPED i des d'aquest recuperar l'espai de memòria del IO_CONTEXT. Personalment trobo aquest punt molt brillant.

Ara que ja he analitzat amb detall els arguments ja puc implementar la crida al punter a *AcceptEx()*:

```
result = serverContext.lpfnAcceptEx(serverContext.listeningSocket, ioContext->acceptSocket, ioContext->acceptBuffer, 0, ADDRESS_BUFFER_SIZE, ADDRESS_BUFFER_SIZE, &bytesReceived, &ioContext->overlapped);
```

Ara que he analitzat tots els arguments, en la crida anterior només hi ha un punt que pot semblar interessant (a excepció del OVERLAPPED, clar): indico per separat els bytes que he reservat per a l'adreça local i per l'adreça remota. I tornant a les constants **ADDRESS_BUFFER_SIZE** i **ACCEPT_BUFFER_SIZE**, una indica quants bytes vull reservar per cada adreça (podrien ser diferents), i l'altre quants bytes vull reservar per les dues adreces alhora que es tal com ho guardarà *AcceptEx()*, una adreça després de l'altra; per això és important separar aquestes dues longituds.

## 15.6. Valor de retorn d'AcceptEx
Com que és una funció sobreposada (overlaped) el valor de retorn no es pot avaluar de forma directa tal com hauria pensat inicialment: true vol dir tot correcte i false vol dir que s'ha produït un error. Contra tot pronòstic el valor de retorn *TRUE* indica que l'operació s'ha completat immediatament, i això és un cas poc habitual en IOCP. En canvi quan el valor de retorn és *FALSE* caldrà contrastar amb el valor de **WSAGetLastError()**:
	+ result == FALSE && WSAGetLastError() == ERROR_IO_PENDING significa que no hi ha hagut cap error, sinó que és el cas més habitual: significa que s'ha iniciat l'operació d'acceptació de nova connexió entrant però al moment de retornar la funció aquesta acceptació encara està en procés.
	+ result == FALSE && WSAGetLastError() != ERROR_IO_PENDING significa que s'ha produït un error i l'operació no s'ha pogut iniciar correctament.

## 15.7. Gestió de IO_CONTEXT després d'AcceptEx
En cas que la funció retorni sense cap error (és a dir, retorna TRUE o retorna FALSE amb ERROR_IO_PENDING) és un bon moment per revisar que s'ha de fer amb l'IO_CONTEXT. No es pot destruïr, ja que WIndows de forma interna encara hi està treballant, necessita que l'adreça de memòria sigui accessible des de l'aplicació. Per tant per una banda no he d'eliminar el IO_CONTEXT, i per l'altra no he de tancar el socket ja que l'operació d'acceptació encara no ha finalitzat (quan retorna TRUE si que ha finalitzat, però no s'ha acabat el treball amb el client). A més és important remarcar que no es pot tornar a cridar AcceptEx amb al el mateix IO_CONTEXT. El IO_CONTEXT que he utilitzat el deixo pendent i ja el recuperaré quan arribi la notificació al port de compleció dins del procés del worker.

## 16 WorkerThread
Si tot ha funcionat correctament, ara ja tinc el procés d'acceptació en marxa; quan acabi d'executar-se la funció asíncrona que he llençat al pas anterior arribarà una notificació al port de compleció, i aquest port de compleció despertarà el procés que tinc al **WorkerThread**. Per tant és ara quan entra en joc el worker. Tal com he descrit anteriorment el worker no consumeix cpu esperant que hi hagi una notificació disponible, sinó que queda bloquejat mentres no hi ha notificacions de complecions disponibles. Per posar el procediment en espera mentres no hi ha complecions disponibles s'utilitza la funció *GetQueuedCompletionStatus()*, implemento ja la crida dins del procediment:

```
DWORD WINAPI WorkerThread(LPVOID lpParam) {
	SERVER_CONTEXT *lpServerContext = static_cast<SERVER_CONTEXT*>(lpParam);
	DWORD bytesTransferred = 0;
	ULONG_PTR completionKey = 0;
	OVERLAPPED *pOverlapped = nullptr;
	BOOL result;

	result = GetQueuedCompletionStatus(lpServerContext->hCompletionPort, &bytesTransferred, &completionKey, &pOverlapped, INFINITE);

	return 0;
}
```

## 16.1. Valor de retorn de GetQueuedCompletionStatus
El primer pas després del retorn de la funció és comprovar el propi valor de retorn. Pot retornar TRUE i FALSE. Si retorna TRUE vol dir que la funció ha finalitzat correctament, i ja es pot gestionar el següent pas. En canvi si retorna FALSE no vol dir que simplement ha fallat i cal oblidar-se'n, sinó que cal comprovar si realment ha arribat una compleció. Si el retorn és FALSE i ha arribat una compleció caldrà gestionar-la correctament ja que estarà apuntant a una adreça de memòria vàlida que caldrà alliberar correctament quan acabi tota la gestió d'aquell client. En cas que *GetQueuedCompletionStatus* retorni FALSE i *pOverlapped* no sigui nul voldrà dir que l'operació associada ha fallat. Per tant modifico una mica la crida a *GetQueuedCompletionStatus*:

```
//espera indefinidament que arribi la notificació d'una compleció
if(!(result = GetQueuedCompletionStatus(lpServerContext->hCompletionPort, &bytesTransferred, &completionKey, &pOverlapped, INFINITE))) {
	if(pOverlapped == nullptr) {
		//error intern en la pròpia GetQueuedCompletionStatus
	}
	else {
		//error en l'operació associada
	}
}
else {
	//operació associada finalitzada correctament
}
```

Ara veig que tal com està plantejat el procediment, aquest mor si o si després del retorn de *GetQueuedCompletionStatus*. Per no perdre els workers he de cridar *GetQueuedCompletionStatus* dins d'un bucle. Preveig ara que no serà un bucle infinit, sinó que l'hauré de poder controlar per finalitzar correctament el servidor. Però això son implementacions posteriors. Per ara només m'interessa avaluar els casos d'error o èxit i veure com gestionar l'operació completada. L'únic cas clar que hi ha ara és quan la funció retorna FALSE i el punter a OVERLAPED és nul: està clar que no puc reintentar la crida ja que el punter nul a l'adreça de memòria del OVERLAPPED m'impedeix recuperar el IO_CONTEXT. Per tant en aquest punt del projecte puc fer que en el cas que *GetQueuedCompletionStatus* retorni FALSE i el punter a OVERLAPPED sigui nul finalitzi el procediment retornant codi d'error 1:

```
//espera indefinidament que arribi la notificació d'una compleció
if(!(result = GetQueuedCompletionStatus(lpServerContext->hCompletionPort, &bytesTransferred, &completionKey, &pOverlapped, INFINITE))) {
	DWORD error = GetLastError();
	if(pOverlapped == nullptr) {
		//error intern en la pròpia GetQueuedCompletionStatus
		cout << "Error en GetQueuedCompletionStatus: " << error << endl;
		return 1;
	}
	else {
		//error en l'operació associada
	}
}
else {
	//operació associada finalitzada correctament
}
```

Això només es una solució temporal, ja que no m'interessa que vagin morint fils ni en cas d'error ni després de gestionar una operació finalitzada.

## 16.2. OVERLAPPED rebut
El *lpServerContext* és l'argument que envio al crear els fils del procés; tots els fils tindran el mateix context de servidor. Aquest context de servidor conté la instància del port de compleció del que vull rebre notificacions. Pot haver-n'hi mes d'un, però per fer aquest projecte i analitzar el funcionament només n'utilitzo un. El paràmetre *INFINITE* indica el temps que ha de durar l'inactivitat abans de reprendre. Si acaba el temps d'inactivitat sense que hi hagi disponible cap compleció la funció retorna FALSE i assigna NULL al paràmetre *lpOverlapped*. Si el temps d'espera és *INFINITE* la funció quedarà en espera de forma indefinida. Al manual de referència de la funció s'indica que la interpretació d'aquest temps d'espera varia segons la versió del sistema operatiu, però per aquest projecte en principi no m'afectarà.

Quan la funció retorna obtenim 3 valors importants: *bytesTransferred*, *completionKey* i *pOverlapped*. D'aquests tres paràmetres el més immediat és **pOverlapped**. Quan crido a *lpfnAcceptEx()* li envio un punter al membre OVERLAPPED del IO_CONTEXT; el sistema s'encarrega d'assignar el valor a OVERLAPPED, i això afecta al disseny de l'estructura IO_CONTEXT i com s'utilitza per recuperar les dades de l'operació. Quan inicio l'operació asíncrona *lpfnAcceptEx()* li envio el punter al OVERLAPPED de l'estructura IO_CONTEXT. Aquest estructura IO_CONTEXT l'he creat amb *new* però no n'he fet cap delete. En condicions descontrolades una crida successiva a *new* implicaria una fuita de memòria (memory leak): reservo memòria en un punter, no l'allibero i després torno a reservar memòria sobre el mateix punter, i per tant no puc recuperar la zona de memòria del primer new. En aquest model el sistema desconeix que és IO_CONTEXT, però si que sap que és OVERLAPPED; i com que l'estructura es guarda en memòria de forma seqüencial (encara que el compilador pugui afegir padding entre els membres) i el primer membre de IO_CONTEXT és OVERLAPPED i en C++ l'adreça de memòria d'una estructura coincideix amb l'adreça de memòria del seu primer membre això implica que quan rebi la compleció i amb ella el OVERLAPPED sabré segur que l'adreça de meòria del OVERLAPPED rebut és la mateixa que l'adreça de memòria del IO_CONTEXT, amb la qual cosa el podré recuperar. Per això aquest paràmetre de retorn és imprescindible per la persistència de les dades de gestió del client en aquest model de servidor. Tal com he plantejat l'estructura del IO_CONTEXT el possible padding que pugui afegir el compilador entre els membres no afecta el funcionament, ja que el membre OVERLAPPED del que se l'adreça de memòria és el primer de l'estructura i per tant no tindrà padding previ.

Sabent això ja puc recuperar l'estructura del context de l'operació *IO_CONTEXT*; C++ ja incorpora el mecanisme per recuperar tota l'estructura a partir de l'adreça del primer membre:

```
IO_CONTEXT *ioContext = reinterpret_cast<IO_CONTEXT*>(pOverlapped);
```

## 16.3. Clau de compleció rebuda
La definició de la clau de compleció és: una dada definida pel programa que queda associada al handle quan aquest s'associa a l'IOCP i que Windows retorna al worker juntament amb la compleció. Això vol dir que puc disposar de dos mecanismes d'identificació: el OVERLAPPED i la clau de compleció. En el projecte que he plantejat creo el port de compleció amb la clau 0, ja que disposo del OVERLAPED per recuperar l'operació. Per tant ara mateix deixaré aquest valor per defecte a 0.

## 16.4. Bytes transferits
El següent paràmetre de la funció que m'interessa comprovar és *bytesTransferred*. El manual de referència diu que és el nombre de bytes transferits en una operació d'IO finalitzada correctament. Però l'operació que he llençat per rebre aquesta compleció és la de *AcceptEx* i aquí entra en joc el valor que he indicat al fer la crida a *AcceptEx* per indicar el nombre de bytes que permeto llegir de la primera tramesa del client. Com que a la meva crida he indicat que no espero rebre cap byte doncs ara aquest *bytesTransferred* no l'hauré de tenir en compte.

## 17. Tractament dels retorn de GetQueuedCompletionStatus amb error

