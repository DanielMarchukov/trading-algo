#include "restclient-cpp/connection.h"
#include "restclient-cpp/restclient.h"
#include <iostream>
#include <ql/quantlib.hpp>

int main(int argc, char *argv[]) {
    RestClient::init();
    RestClient::Connection *conn =
        new RestClient::Connection("https://paper-api.alpaca.markets");
    std::cout << conn->get("/v2/account").body;
    return 0;
}
