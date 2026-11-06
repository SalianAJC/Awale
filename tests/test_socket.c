
#include <winsock2.h>
#include <stdio.h>

int main(void)
{
    WSADATA wsa;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("Winsock initialization failed.\n");
        return 1;
    }

    printf("Winsock is working!\n");

    WSACleanup();

    return 0;
}
