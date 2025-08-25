#ifndef CLIENT_H
#define CLIENT_H

#include <map>
#include <iostream>
#include <algorithm>
#include <vector>
#include <string>
#include <cstring>
#include <thread>
#include <mutex>
#include <map>
#include <fstream>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits>
#include <iomanip>
#include "server.h"

struct DownloadInfo
{
    std::string filename;
    long total_size;
    long bytes_downloaded;
};

struct ChunkInfo
{
    int file_id;
    std::string filename;
    long start_byte;
    long chunk_size;
};

struct PortDownloadInfo
{
    int file_id;
    std::string filename;
    int port;
    std::vector<ChunkInfo> chunks;
    FILE *file_ptr;
};

struct FileInfo;

class Client
{
public:
    Client(const std::vector<int> &ports, int listen_port, const std::string &directory_path);
    void run();

private:
    std::vector<int> ports;
    int listen_port;
    std::string directory_path;
    std::map<int, FileInfo> available_files;
    std::map<int, DownloadInfo> current_downloads;
    std::mutex files_mutex;
    void print_menu();
    void list_available_files();
    struct RequestArgs
    {
        int *port_ptr;
        Client *client_ptr;
    };
    static void *request_files_helper(void *arg);
    void *request_files(int *port_ptr);
    void download_file();
    struct DownloadArgs
    {
        PortDownloadInfo *port_info;
        Client *client_ptr;
    };
    static void *download_from_specific_port_helper(void *arg);
    void *download_from_specific_port(PortDownloadInfo *port_info);
    int count_sources(int file_id, const std::string &filename);
    std::vector<int> find_ports_with_file(int file_id, const std::string &filename);
    void show_download_status();
    void download_status(const std::pair<const int, DownloadInfo> *entry);
    void cleanup_completed_downloads();
};

#endif
