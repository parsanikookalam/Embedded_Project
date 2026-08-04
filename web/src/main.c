#include "http_server.h"

int main(void) {
    // Start the server on port 8080 (we will add port 443 SSL handling in section b)
    start_http_server(8080);
    return 0;
}