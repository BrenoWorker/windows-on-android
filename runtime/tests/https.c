#include <windows.h>
#include <winhttp.h>
#include <stdio.h>

int main(void) {
    HINTERNET session = WinHttpOpen(L"windroid-test", WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0);
    HINTERNET connection = session ? WinHttpConnect(session, L"github.com", INTERNET_DEFAULT_HTTPS_PORT, 0) : NULL;
    HINTERNET request = connection ? WinHttpOpenRequest(connection, L"HEAD", L"/", NULL, NULL, NULL, WINHTTP_FLAG_SECURE) : NULL;
    DWORD status = 0, size = sizeof status;
    if (!request || !WinHttpSendRequest(request, NULL, 0, NULL, 0, 0, 0) || !WinHttpReceiveResponse(request, NULL) ||
        !WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &size, NULL)) {
        printf("https-error=%lu\n", GetLastError());
        return 1;
    }
    printf("https=%lu\n", status);
    return 0;
}
