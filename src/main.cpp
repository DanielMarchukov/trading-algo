#include "restclient-cpp/connection.h"
#include "restclient-cpp/restclient.h"
#include <cstdlib>
#include <iostream>
#include <ql/quantlib.hpp>

int main(int argc, char *argv[]) {
    RestClient::init();
    RestClient::Connection *conn =
        new RestClient::Connection("https://paper-api.alpaca.markets");
    RestClient::HeaderFields headers;
    headers["APCA-API-KEY-ID"] = std::getenv("APCA_API_KEY_ID");
    headers["APCA-API-SECRET-KEY"] = std::getenv("APCA_API_SECRET_KEY");
    conn->SetHeaders(headers);
    std::cout << conn->get("/v2/account").body;
    RestClient::disable();
    delete conn;
    return 0;
}
