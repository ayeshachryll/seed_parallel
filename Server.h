
#ifndef SERVER_H
#define SERVER_H

#include <string>
#include <vector>
#include <map>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <thread>
#include <mutex>
#include <fstream>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits>
#include <iomanip>

struct FileInfo
{
    std::string filename;
    long size;
};

class Server
{
public:
    Server(const std::string &dir, const std::vector<int> &ports);
    void start();
    int get_listen_port() const;
    const std::vector<int> &get_ports() const;
    std::string get_directory_path() const;

private:
    std::string directory_path;
    std::vector<int> ports;
    int listen_fd;
    int listen_port;
    static void *accept_thread_helper(void *arg);
    void *accept_thread();
    struct AcceptArgs
    {
        int *fd_ptr;
        Server *server_ptr;
    };
    static void *handle_client_thread_helper(void *arg);
    void *handle_client_thread(int *client_fd_ptr);
    std::map<int, FileInfo> list_files();
    bool bind_available();
};

#endif
