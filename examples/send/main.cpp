/*! \file main.cpp
 *  \brief incppect basics
 *  \author Georgi Gerganov
 */

#include "incppect/incppect.h"

using namespace incppect;

int main(int argc, char ** argv) {
	printf("Usage: %s [port] [httpRoot]\n", argv[0]);

    int port = argc > 1 ? atoi(argv[1]) : 3000;
    std::string httpRoot = argc > 2 ? argv[2] : "../examples";

    incppect::Parameters parameters;
    parameters.portListen = port;
    parameters.maxPayloadLength_bytes = 256*1024;
    parameters.httpRoot = httpRoot + "/send";
    parameters.resources = { "", "index.html", };

    // handle input from the clients
    incppect::getInstance().handler = [&](int clientId, incppect::EventType etype, std::string_view data) {

        using enum incppect::EventType;

        switch (etype) {
            case Connect:
                {
                    printf("Client %d connected\n", clientId);
                }
                break;
            case Disconnect:
                {
                    printf("Client %d disconnected\n", clientId);
                }
                break;
            case Custom:
                {
                    printf("Client %d: '%s'\n", clientId, std::string(data.data(), data.size()).c_str());
                }
                break;
        };
    };

    incppect::getInstance().runAsync(parameters).detach();

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return 0;
}
