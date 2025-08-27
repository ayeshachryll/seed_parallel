#include "Server.h"
#include "Client.h"

int main()
{
    std::vector<int> ports = {8999, 9000, 9002, 9003, 9004};
    std::string directory_path = "./files";
    std::cout << "Finding available ports...";
    Server server(directory_path, ports);
    server.start();
    int listen_port = server.get_listen_port();
    Client client(ports, listen_port, directory_path);
    client.run();
    return 0;
}
