// WirelessInput360 - receptor de logs por UDP
//
// El plugin de la Xbox manda cada linea de log como un datagrama UDP al puerto
// 3001, a broadcast y tambien a la IP configurada en el ini. Este programa
// escucha, imprime lo que llega y lo guarda en wi360.log.
//
// Compilado con CRT estatico (/MT): no necesita ningun redistribuible.

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

#define LOG_PORT 3001

static const char* ExplainWsa(int code) {
    switch (code) {
        case 10013: return "WSAEACCES - permiso denegado";
        case 10047: return "WSAEAFNOSUPPORT - familia de direcciones no soportada";
        case 10049: return "WSAEADDRNOTAVAIL - esa IP no es valida desde aqui";
        case 10050: return "WSAENETDOWN - la red esta caida";
        case 10051: return "WSAENETUNREACH - red inalcanzable (ruta o gateway)";
        case 10054: return "WSAECONNRESET - el otro extremo cerro de golpe";
        case 10060: return "WSAETIMEDOUT - salen paquetes pero no vuelve nada";
        case 10061: return "WSAECONNREFUSED - llega, pero nadie escucha en ese puerto";
        case 10065: return "WSAEHOSTUNREACH - host inalcanzable";
        default:    return NULL;
    }
}

// Busca "wsa=NNNN" en la linea y devuelve el numero, o -1.
static int FindWsaCode(const char* text) {
    const char* p = strstr(text, "wsa=");
    if (!p) return -1;
    p += 4;
    if (*p < '0' || *p > '9') return -1;
    int v = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        p++;
    }
    return v;
}

int main(void) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("WSAStartup fallo: %d\n", WSAGetLastError());
        return 1;
    }

    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        printf("No se pudo crear el socket: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    BOOL yes = TRUE;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, (char*)&yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(LOG_PORT);

    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        int e = WSAGetLastError();
        printf("No se pudo escuchar en el puerto %d: %d\n", LOG_PORT, e);
        if (e == WSAEADDRINUSE) {
            printf("Ya hay otra copia de este programa corriendo.\n");
        }
        closesocket(s);
        WSACleanup();
        printf("\nPulsa Enter para salir...");
        getchar();
        return 1;
    }

    FILE* log = fopen("wi360.log", "a");

    printf("==============================================================\n");
    printf(" WirelessInput360 - receptor de logs\n");
    printf(" Escuchando UDP 0.0.0.0:%d      (Ctrl+C para salir)\n", LOG_PORT);
    printf(" Guardando en wi360.log\n");
    printf("==============================================================\n\n");
    fflush(stdout);

    char buf[2048];

    for (;;) {
        struct sockaddr_in from;
        int fromlen = sizeof(from);

        int n = recvfrom(s, buf, sizeof(buf) - 1, 0, (struct sockaddr*)&from, &fromlen);
        if (n == SOCKET_ERROR) {
            printf("recvfrom fallo: %d\n", WSAGetLastError());
            break;
        }

        buf[n] = '\0';
        // quitar saltos de linea del final
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
            buf[--n] = '\0';
        }

        SYSTEMTIME t;
        GetLocalTime(&t);

        char origen[64];
        strcpy(origen, inet_ntoa(from.sin_addr));

        printf("[%02d:%02d:%02d] %-15s %s\n", t.wHour, t.wMinute, t.wSecond, origen, buf);

        int code = FindWsaCode(buf);
        if (code > 0) {
            const char* why = ExplainWsa(code);
            if (why) {
                printf("                           ^^^  %s\n", why);
            } else {
                printf("                           ^^^  codigo winsock %d (no catalogado)\n", code);
            }
        }
        fflush(stdout);

        if (log) {
            fprintf(log, "[%02d:%02d:%02d] %s  %s\n", t.wHour, t.wMinute, t.wSecond, origen, buf);
            fflush(log);
        }
    }

    if (log) fclose(log);
    closesocket(s);
    WSACleanup();
    return 0;
}
