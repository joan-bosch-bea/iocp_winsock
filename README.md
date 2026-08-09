# IOCP Winsock
Creació des de zero d'un servidor per a sistemes Windows utilitzant C++, Winsock i I/O Completion Ports (IOCP).

# Objectiu
L'objectiu d'aquest projecte és construir el servidor documentant cada pas i explicant les diferents parts de la implementació per entendre com funciona l'arquitectura d'IOCP i com es pot utilitzar per gestionar comunicacions de xarxa de manera asíncrona i concurrent.

# Desenvolupament
## 1. Inicialitzar winsock
Habilitar la funcionalitat de winsock per accedir als recursos de xarxa. És el procés estàndard, no hi ha cap diferència amb el que hauria fet per treballar sense IOCP.

## 2. Creació del listening socket
Per poder treballar amb IOCP és requereix un model de socket especial, concretament WSASocket ja que aquest està preparat per treballar amb entrada i sortida sobreposades (overlapped io). Les funcions AcceptEx(), WSARecv(), WSASend(), etc... estàn adaptades a IOCP, i per poder-les utilitzar és necessari crear el WSASocket amb el flag WSA_FLAG_OVERLAPPED:

WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);

Aquest socket d'escolta estarà dedicat única i exclusivament a escoltar connexions d'entrada

