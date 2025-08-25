
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

struct FileInfo
{
    std::string filename;
    long size;
};

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

class Server
{
public:
    Server(const std::string &dir, const std::vector<int> &ports)
        : directory_path(dir), ports(ports), listen_fd(-1), listen_port(-1) {}

    void start()
    {
        if (!bind_available())
        {
            std::cerr << "No available ports found. Exiting.\n";
            exit(1);
        }
        std::cout << "Found port " << listen_port << "\n";
        std::cout << "Listening at port " << listen_port << "\n";
        pthread_t accept_thread_id;
        pthread_create(&accept_thread_id, nullptr, accept_thread_helper, this);
    }

    int get_listen_port() const { return listen_port; }

    const std::vector<int> &get_ports() const { return ports; }

    std::string get_directory_path() const { return directory_path; }

private:
    std::string directory_path;
    std::vector<int> ports;
    int listen_fd;
    int listen_port;

    static void *accept_thread_helper(void *arg)
    {
        return static_cast<Server *>(arg)->accept_thread();
    }

    void *accept_thread()
    {
        struct sockaddr_in addr{};
        socklen_t addrlen = sizeof(addr);
        while (1)
        {
            int client_socket = accept(listen_fd, (struct sockaddr *)&addr, &addrlen);
            if (client_socket < 0)
            {
                perror("Accept failed");
                continue;
            }
            else
            {
                int *fd_ptr = new int(client_socket);
                pthread_t handler;
                pthread_create(&handler, nullptr, handle_client_thread_helper, new AcceptArgs{fd_ptr, this});
                pthread_detach(handler);
            }
        }
        return nullptr;
    }

    struct AcceptArgs
    {
        int *fd_ptr;
        Server *server_ptr;
    };

    static void *handle_client_thread_helper(void *arg)
    {
        AcceptArgs *args = static_cast<AcceptArgs *>(arg);
        void *ret = args->server_ptr->handle_client_thread(args->fd_ptr);
        delete args;
        return ret;
    }

    void *handle_client_thread(int *client_fd_ptr)
    {
        int client_fd = *client_fd_ptr;
        delete client_fd_ptr;
        char buffer[1024];
        ssize_t bytes = recv(client_fd, buffer, sizeof(buffer), 0);
        if (bytes <= 0)
        {
            close(client_fd);
            return nullptr;
        }
        buffer[bytes] = '\0';
        std::string request(buffer);
        if (request == "LIST")
        {
            auto files = list_files();
            std::string response;
            for (auto &f : files)
            {
                response += "[" + std::to_string(f.first) + "] " + f.second.filename + " - " + std::to_string(f.second.size) + " bytes\n";
            }
            send(client_fd, response.c_str(), response.size(), 0);
        }
        else if (request.find("DOWNLOAD ") != std::string::npos)
        {
            std::string file_id = request.substr(9);
            std::string file_path = directory_path + "/" + file_id;
            DIR *dir = opendir(file_path.c_str());
            if (dir == NULL)
            {
                perror("Error opening directory");
                exit(EXIT_FAILURE);
            }
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL)
            {
                std::string name = entry->d_name;
                if (name == "." || name == "..")
                    continue;
                if (entry->d_type == DT_REG)
                {
                    file_path += "/" + name;
                    break;
                }
            }
            closedir(dir);
            FILE *file = fopen(file_path.c_str(), "rb");
            if (!file)
            {
                std::cerr << "Failed to open file\n";
                close(client_fd);
                return nullptr;
            }
            char buffer_file[32];
            size_t bytes_read;
            while ((bytes_read = fread(buffer_file, 1, 32, file)) > 0)
            {
                if (send(client_fd, buffer_file, bytes_read, 0) == -1)
                {
                    perror("Error sending file");
                    break;
                }
            }
            fclose(file);
        }
        close(client_fd);
        return nullptr;
    }

    std::map<int, FileInfo> list_files()
    {
        std::map<int, FileInfo> map_files;
        DIR *dir = opendir(directory_path.c_str());
        if (dir == NULL)
        {
            perror("Error opening directory");
            exit(EXIT_FAILURE);
        }
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL)
        {
            std::string name = entry->d_name;
            if (name == "." || name == "..")
                continue;
            std::string full_path = directory_path + "/" + name;
            if (entry->d_type == DT_DIR)
            {
                DIR *subdir = opendir(full_path.c_str());
                if (subdir == NULL)
                {
                    perror("Error opening directory");
                    exit(EXIT_FAILURE);
                }
                struct dirent *subentry;
                while ((subentry = readdir(subdir)) != NULL)
                {
                    std::string file = subentry->d_name;
                    if (file == "." || file == "..")
                        continue;
                    if (subentry->d_type == DT_REG)
                    {
                        int key = std::stoi(name);
                        std::string file_path = full_path + "/" + file;
                        struct stat file_stat;
                        long file_size = 0;
                        if (stat(file_path.c_str(), &file_stat) == 0)
                        {
                            file_size = file_stat.st_size;
                        }
                        map_files[key] = {file, file_size};
                    }
                }
                closedir(subdir);
            }
        }
        closedir(dir);
        return map_files;
    }

    bool bind_available()
    {
        int opt = 1;
        struct sockaddr_in addr{};
        for (int i = 0; i < ports.size(); ++i)
        {
            int fd = socket(AF_INET, SOCK_STREAM, 0);
            if (fd == -1)
            {
                perror("Socket creation failed");
                exit(EXIT_FAILURE);
            }
            if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
            {
                perror("setsockopt failed");
                exit(EXIT_FAILURE);
            }
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_ANY);
            addr.sin_port = htons(ports[i]);
            if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0)
            {
                listen_fd = fd;
                listen_port = ports[i];
                if (listen(listen_fd, 4) < 0)
                {
                    perror("Listen failed");
                    close(listen_fd);
                    exit(EXIT_FAILURE);
                }
                return true;
            }
            close(fd);
        }
        return false;
    }
};

class Client
{
public:
    Client(const std::vector<int> &ports, int listen_port, const std::string &directory_path)
        : ports(ports), listen_port(listen_port), directory_path(directory_path) {}

    void run()
    {
        while (1)
        {
            print_menu();
            int choice = 0;
            if (!(std::cin >> choice))
            {
                std::cout << "Invalid choice. Enter a valid number.\n";
                std::cin.clear();
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                continue;
            }
            else if (choice == 1)
            {
                list_available_files();
            }
            else if (choice == 2)
            {
                download_file();
            }
            else if (choice == 3)
            {
                show_download_status();
            }
            else if (choice == 4)
            {
                std::cout << "Exiting...\n";
                break;
            }
            else
            {
                std::cout << "Invalid choice. Enter a valid number.\n";
            }
        }
    }

private:
    std::vector<int> ports;
    int listen_port;
    std::string directory_path;
    std::map<int, FileInfo> available_files;
    std::map<int, DownloadInfo> current_downloads;
    std::mutex files_mutex;

    void print_menu()
    {
        std::cout << "\nSeed App\n";
        std::cout << "[1] List available files.\n";
        std::cout << "[2] Download file.\n";
        std::cout << "[3] Download status.\n";
        std::cout << "[4] Exit.\n";
        std::cout << "\n? ";
    }

    void list_available_files()
    {
        std::cout << "\nSearching for files...";
        available_files.clear();
        std::vector<pthread_t> threads;
        for (int p : ports)
        {
            if (p != listen_port)
            {
                pthread_t thread;
                int *port_ptr = new int(p);
                pthread_create(&thread, nullptr, request_files_helper, new RequestArgs{port_ptr, this});
                threads.push_back(thread);
            }
        }
        for (pthread_t &thread : threads)
        {
            pthread_join(thread, nullptr);
        }
        std::cout << " done.\n";
        if (available_files.empty())
        {
            std::cout << "No files available.\n";
        }
        else
        {
            std::cout << "Files available:\n";
            for (const auto &entry : available_files)
            {
                int file_id = entry.first;
                const std::string &filename = entry.second.filename;
                long file_size = entry.second.size;
                std::cout << "[" << file_id << "] " << filename << " (" << file_size << " bytes)\n";
            }
        }
    }

    struct RequestArgs
    {
        int *port_ptr;
        Client *client_ptr;
    };

    static void *request_files_helper(void *arg)
    {
        RequestArgs *args = static_cast<RequestArgs *>(arg);
        void *ret = args->client_ptr->request_files(args->port_ptr);
        delete args;
        return ret;
    }

    void *request_files(int *port_ptr)
    {
        int port = *port_ptr;
        delete port_ptr;
        if (port == listen_port)
            return nullptr;
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0)
        {
            perror("Failed to create socket");
            return nullptr;
        }
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        if (connect(sock, (sockaddr *)&addr, sizeof(addr)) == 0)
        {
            send(sock, "LIST", 4, 0);
            char buffer[1024];
            ssize_t n = recv(sock, buffer, sizeof(buffer), 0);
            if (n > 0)
            {
                buffer[n] = '\0';
                std::string data(buffer);
                size_t pos = 0;
                std::lock_guard<std::mutex> lock(files_mutex);
                while ((pos = data.find('\n')) != std::string::npos)
                {
                    std::string f = data.substr(0, pos);
                    data.erase(0, pos + 1);
                    size_t bracket_pos = f.find("] ");
                    if (bracket_pos != std::string::npos && f[0] == '[')
                    {
                        int key = std::stoi(f.substr(1, bracket_pos - 1));
                        std::string rest = f.substr(bracket_pos + 2);
                        size_t dash_pos = rest.find(" - ");
                        std::string filename = rest.substr(0, dash_pos);
                        size_t size_start = dash_pos + 3;
                        size_t size_end = rest.find(" bytes");
                        std::string size_str = rest.substr(size_start, size_end - size_start);
                        long file_size = std::stol(size_str);
                        std::string local_path = "files/" + std::to_string(key) + "/" + filename;
                        FILE *local_file = fopen(local_path.c_str(), "rb");
                        if (local_file)
                        {
                            fclose(local_file);
                            continue;
                        }
                        available_files[key] = {filename, file_size};
                    }
                }
            }
        }
        close(sock);
        return nullptr;
    }

    void download_file()
    {
        int file_id;
        std::cout << "\nEnter file ID: ";
        std::cin >> file_id;
        std::cout << "Locating seeders...";
        if (available_files.find(file_id) == available_files.end())
        {
            std::cout << "Failed.\n";
            std::cout << "No seeders for file ID " << file_id << "\n";
        }
        else
        {
            int seeders_count = count_sources(file_id, available_files[file_id].filename);
            long file_size = available_files[file_id].size;
            std::string filename = available_files[file_id].filename;
            std::cout << "Found " << seeders_count << " seeder/s.\n";
            std::cout << "Download started. File: [" << file_id << "] " << filename << " (" << file_size << " bytes)\n";
            std::vector<int> available_ports = find_ports_with_file(file_id, filename);
            if (available_ports.empty())
            {
                std::cout << "No available ports found for this file.\n";
                return;
            }
            std::string dir_path = "files/" + std::to_string(file_id);
            mkdir(dir_path.c_str(), 0700);
            std::string file_path = dir_path + "/" + filename;
            FILE *shared_file = fopen(file_path.c_str(), "w+b");
            if (!shared_file)
            {
                std::cout << "Failed to create file.\n";
                return;
            }
            fseek(shared_file, file_size - 1, SEEK_SET);
            fputc(0, shared_file);
            fflush(shared_file);
            current_downloads[file_id] = {filename, file_size, 0};
            int num_chunks = file_size / 32 + (file_size % 32 != 0 ? 1 : 0);
            int num_ports = available_ports.size();
            std::cout << "Downloading " << num_chunks << " chunks using " << num_ports << " port/s...\n";
            std::vector<std::vector<ChunkInfo>> port_chunks(num_ports);
            int current_port = 0;
            for (int i = 0; i < num_chunks; i++)
            {
                ChunkInfo chunk;
                chunk.file_id = file_id;
                chunk.filename = filename;
                chunk.start_byte = i * 32;
                long remaining_bytes = file_size - chunk.start_byte;
                if (remaining_bytes > 32)
                {
                    chunk.chunk_size = 32;
                }
                else
                {
                    chunk.chunk_size = remaining_bytes;
                }
                port_chunks[current_port].push_back(chunk);
                current_port++;
                if (current_port >= num_ports)
                    current_port = 0;
            }
            std::vector<pthread_t> download_threads;
            for (int i = 0; i < num_ports; i++)
            {
                PortDownloadInfo *port_info = new PortDownloadInfo();
                port_info->file_id = file_id;
                port_info->filename = filename;
                port_info->port = available_ports[i];
                port_info->chunks = port_chunks[i];
                port_info->file_ptr = shared_file;
                pthread_t thread;
                pthread_create(&thread, nullptr, download_from_specific_port_helper, new DownloadArgs{port_info, this});
                download_threads.push_back(thread);
            }
            for (pthread_t thread : download_threads)
            {
                pthread_detach(thread);
            }
        }
    }

    struct DownloadArgs
    {
        PortDownloadInfo *port_info;
        Client *client_ptr;
    };

    static void *download_from_specific_port_helper(void *arg)
    {
        DownloadArgs *args = static_cast<DownloadArgs *>(arg);
        void *ret = args->client_ptr->download_from_specific_port(args->port_info);
        delete args;
        return ret;
    }

    void *download_from_specific_port(PortDownloadInfo *port_info)
    {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0)
        {
            delete port_info;
            return nullptr;
        }
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_info->port);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        if (connect(sock, (sockaddr *)&addr, sizeof(addr)) != 0)
        {
            close(sock);
            delete port_info;
            return nullptr;
        }
        std::string request = "DOWNLOAD " + std::to_string(port_info->file_id);
        send(sock, request.c_str(), request.size(), 0);
        char buffer[32];
        long current_position = 0;
        for (const auto &chunk : port_info->chunks)
        {
            while (current_position < chunk.start_byte)
            {
                long skip_bytes = chunk.start_byte - current_position;
                long to_read = std::min(32L, skip_bytes);
                ssize_t n = recv(sock, buffer, to_read, 0);
                if (n <= 0)
                {
                    close(sock);
                    delete port_info;
                    return nullptr;
                }
                current_position += n;
            }
            fseek(port_info->file_ptr, chunk.start_byte, SEEK_SET);
            long bytes_read = 0;
            while (bytes_read < chunk.chunk_size)
            {
                long to_read = std::min(32L, chunk.chunk_size - bytes_read);
                ssize_t n = recv(sock, buffer, to_read, 0);
                if (n <= 0)
                    break;
                fwrite(buffer, 1, n, port_info->file_ptr);
                fflush(port_info->file_ptr);
                bytes_read += n;
                current_position += n;
                sleep(10);
                {
                    std::lock_guard<std::mutex> lock(files_mutex);
                    current_downloads[port_info->file_id].bytes_downloaded += n;
                }
            }
        }
        close(sock);
        delete port_info;
        return nullptr;
    }

    int count_sources(int file_id, const std::string &filename)
    {
        int sources_count = 0;
        for (int target_port : ports)
        {
            if (target_port == listen_port)
                continue;
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0)
                continue;
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(target_port);
            inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
            if (connect(sock, (sockaddr *)&addr, sizeof(addr)) == 0)
            {
                send(sock, "LIST", 4, 0);
                char buffer[1024];
                ssize_t n = recv(sock, buffer, sizeof(buffer), 0);
                if (n > 0)
                {
                    buffer[n] = '\0';
                    std::string data(buffer);
                    std::string search_pattern = "[" + std::to_string(file_id) + "] " + filename;
                    if (data.find(search_pattern) != std::string::npos)
                        sources_count++;
                }
            }
            close(sock);
        }
        return sources_count;
    }

    std::vector<int> find_ports_with_file(int file_id, const std::string &filename)
    {
        std::vector<int> available_ports;
        for (int target_port : ports)
        {
            if (target_port == listen_port)
                continue;
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0)
                continue;
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(target_port);
            inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
            if (connect(sock, (sockaddr *)&addr, sizeof(addr)) == 0)
            {
                send(sock, "LIST", 4, 0);
                char buffer[1024];
                ssize_t n = recv(sock, buffer, sizeof(buffer), 0);
                if (n > 0)
                {
                    buffer[n] = '\0';
                    std::string data(buffer);
                    std::string search_pattern = "[" + std::to_string(file_id) + "] " + filename;
                    if (data.find(search_pattern) != std::string::npos)
                        available_ports.push_back(target_port);
                }
            }
            close(sock);
        }
        return available_ports;
    }

    void show_download_status()
    {
        cleanup_completed_downloads();
        if (current_downloads.empty())
        {
            std::cout << "No active downloads.\n";
        }
        else
        {
            std::cout << "Download status:\n";
            for (const auto &entry : current_downloads)
            {
                download_status(&entry);
            }
        }
    }

    void download_status(const std::pair<const int, DownloadInfo> *entry)
    {
        std::lock_guard<std::mutex> lock(files_mutex);
        int file_id = entry->first;
        const std::string &filename = entry->second.filename;
        long total_size = entry->second.total_size;
        double downloaded_kb = static_cast<double>(entry->second.bytes_downloaded) / 1024.0;
        double total_kb = static_cast<double>(total_size) / 1024.0;
        double percentage = 0.0;
        if (total_size > 0)
        {
            percentage = (static_cast<double>(entry->second.bytes_downloaded) / total_size) * 100.0;
        }
        std::cout << "[" << file_id << "] " << filename << " - "
                  << std::fixed << std::setprecision(2) << downloaded_kb
                  << " / " << total_kb << " KB ("
                  << std::setprecision(1) << percentage << "%)";
        if (entry->second.bytes_downloaded >= total_size)
        {
            std::cout << " - COMPLETED";
        }
        std::cout << "\n";
    }

    void cleanup_completed_downloads()
    {
        std::lock_guard<std::mutex> lock(files_mutex);
        auto it = current_downloads.begin();
        while (it != current_downloads.end())
        {
            if (it->second.bytes_downloaded >= it->second.total_size)
            {
                std::cout << "Download completed: [" << it->first << "] " << it->second.filename << std::endl;
                it = current_downloads.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
};

struct FileInfo
{
    std::string filename;
    long size;
};

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

const int num_ports = 5;
const int buffer_size = 1024;
const int ports[num_ports] = {8999, 9000, 9002, 9003, 9004};
int listen_fd, listen_port;
std::string directory_path = "./files";
std::map<int, FileInfo> available_files;
std::map<int, DownloadInfo> current_downloads;
std::mutex files_mutex;

std::vector<int> find_ports_with_file(int file_id, const std::string &filename)
{
    std::vector<int> available_ports;

    for (int target_port : ports)
    {
        if (target_port == listen_port)
        {
            continue;
        }

        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0)
        {
            continue;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(target_port);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

        if (connect(sock, (sockaddr *)&addr, sizeof(addr)) == 0)
        {
            send(sock, "LIST", 4, 0);
            char buffer[buffer_size];
            ssize_t n = recv(sock, buffer, sizeof(buffer), 0);

            if (n > 0)
            {
                buffer[n] = '\0';
                std::string data(buffer);
                std::string search_pattern = "[" + std::to_string(file_id) + "] " + filename;
                if (data.find(search_pattern) != std::string::npos)
                {
                    available_ports.push_back(target_port);
                }
            }
        }
        close(sock);
    }

    return available_ports;
}

// Function to download multiple chunks from a specific port
void *download_from_specific_port(void *arg)
{
    PortDownloadInfo *port_info = (PortDownloadInfo *)arg;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        delete port_info;
        return nullptr;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_info->port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(sock, (sockaddr *)&addr, sizeof(addr)) != 0)
    {
        close(sock);
        delete port_info;
        return nullptr;
    }

    // Send download request
    std::string request = "DOWNLOAD " + std::to_string(port_info->file_id);
    send(sock, request.c_str(), request.size(), 0);

    // Download the entire file and extract assigned chunks
    char buffer[32];
    long current_position = 0;

    for (const auto &chunk : port_info->chunks)
    {
        // Read data until we reach this chunk's start position
        while (current_position < chunk.start_byte)
        {
            long skip_bytes = chunk.start_byte - current_position;
            long to_read = std::min(32L, skip_bytes);
            ssize_t n = recv(sock, buffer, to_read, 0);
            if (n <= 0)
            {
                close(sock);
                delete port_info;
                return nullptr;
            }
            current_position += n;
        }

        // Now read and save this chunk
        fseek(port_info->file_ptr, chunk.start_byte, SEEK_SET);

        long bytes_read = 0;
        while (bytes_read < chunk.chunk_size)
        {
            long to_read = std::min(32L, chunk.chunk_size - bytes_read);
            ssize_t n = recv(sock, buffer, to_read, 0);
            if (n <= 0)
            {
                break;
            }

            fwrite(buffer, 1, n, port_info->file_ptr);
            fflush(port_info->file_ptr);
            bytes_read += n;
            current_position += n;

            sleep(10);

            {
                std::lock_guard<std::mutex> lock(files_mutex);
                current_downloads[port_info->file_id].bytes_downloaded += n;
            }
        }
    }

    close(sock);
    delete port_info;
    return nullptr;
}

void *request_files(void *arg)
{
    int port = *(int *)arg;
    delete (int *)arg;

    if (port == listen_port)
    {
        return nullptr;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        perror("Failed to create socket");
        return nullptr;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(sock, (sockaddr *)&addr, sizeof(addr)) == 0)
    {
        send(sock, "LIST", 4, 0);
        char buffer[buffer_size];
        ssize_t n = recv(sock, buffer, sizeof(buffer), 0);

        if (n > 0)
        {
            buffer[n] = '\0';
            std::string data(buffer);
            size_t pos = 0;

            {
                std::lock_guard<std::mutex> lock(files_mutex);

                while ((pos = data.find('\n')) != std::string::npos)
                {
                    std::string f = data.substr(0, pos);
                    data.erase(0, pos + 1);

                    size_t bracket_pos = f.find("] ");
                    if (bracket_pos != std::string::npos && f[0] == '[')
                    {
                        int key = std::stoi(f.substr(1, bracket_pos - 1));
                        std::string rest = f.substr(bracket_pos + 2);
                        size_t dash_pos = rest.find(" - ");

                        std::string filename = rest.substr(0, dash_pos);
                        size_t size_start = dash_pos + 3;
                        size_t size_end = rest.find(" bytes");
                        std::string size_str = rest.substr(size_start, size_end - size_start);
                        long file_size = std::stol(size_str);

                        std::string local_path = "files/" + std::to_string(key) + "/" + filename;
                        FILE *local_file = fopen(local_path.c_str(), "rb");

                        if (local_file)
                        {
                            fclose(local_file);
                            continue;
                        }

                        available_files[key] = {filename, file_size};
                    }
                }
            }
        }
    }

    close(sock);
    return nullptr;
}

void download_status(const std::pair<const int, DownloadInfo> *entry)
{
    {
        std::lock_guard<std::mutex> lock(files_mutex);

        int file_id = entry->first;
        const std::string &filename = entry->second.filename;
        long total_size = entry->second.total_size;

        double downloaded_kb = static_cast<double>(entry->second.bytes_downloaded) / 1024.0;
        double total_kb = static_cast<double>(total_size) / 1024.0;
        double percentage = 0.0;

        if (total_size > 0)
        {
            percentage = (static_cast<double>(entry->second.bytes_downloaded) / total_size) * 100.0;
        }

        std::cout << "[" << file_id << "] " << filename << " - "
                  << std::fixed << std::setprecision(2) << downloaded_kb
                  << " / " << total_kb << " KB ("
                  << std::setprecision(1) << percentage << "%)";

        if (entry->second.bytes_downloaded >= total_size)
        {
            std::cout << " - COMPLETED";
        }

        std::cout << "\n";
    }
}

void cleanup_completed_downloads()
{
    std::lock_guard<std::mutex> lock(files_mutex);

    auto it = current_downloads.begin();
    while (it != current_downloads.end())
    {
        if (it->second.bytes_downloaded >= it->second.total_size)
        {
            std::cout << "Download completed: [" << it->first << "] " << it->second.filename << std::endl;
            it = current_downloads.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

std::map<int, FileInfo> list_files()
{
    std::map<int, FileInfo> map_files;
    DIR *dir = opendir(directory_path.c_str());

    if (dir == NULL)
    {
        perror("Error opening directory");
        exit(EXIT_FAILURE);
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        std::string name = entry->d_name;
        if (name == "." || name == "..")
        {
            continue;
        }

        std::string full_path = directory_path + "/" + name;

        if (entry->d_type == DT_DIR)
        {
            DIR *subdir = opendir(full_path.c_str());

            if (subdir == NULL)
            {
                perror("Error opening directory");
                exit(EXIT_FAILURE);
            }

            struct dirent *subentry;

            while ((subentry = readdir(subdir)) != NULL)
            {
                std::string file = subentry->d_name;
                if (file == "." || file == "..")
                {
                    continue;
                }

                if (subentry->d_type == DT_REG)
                {
                    int key = std::stoi(name);

                    std::string file_path = full_path + "/" + file;
                    struct stat file_stat;
                    long file_size = 0;

                    if (stat(file_path.c_str(), &file_stat) == 0)
                    {
                        file_size = file_stat.st_size;
                    }

                    map_files[key] = {file, file_size};
                }
            }

            closedir(subdir);
        }
    }

    closedir(dir);
    return map_files;
}

void *handle_client_thread(void *arg)
{
    int client_fd = *(int *)arg;
    delete (int *)arg;
    char buffer[buffer_size];
    ssize_t bytes = recv(client_fd, buffer, sizeof(buffer), 0);

    if (bytes <= 0)
    {
        close(client_fd);
        return nullptr;
    }

    buffer[bytes] = '\0';
    std::string request(buffer);

    if (request == "LIST")
    {
        auto files = list_files();
        std::string response;

        for (auto &f : files)
        {
            response += "[" + std::to_string(f.first) + "] " + f.second.filename + " - " + std::to_string(f.second.size) + " bytes\n";
        }
        send(client_fd, response.c_str(), response.size(), 0);
    }

    else if (request.find("DOWNLOAD ") != std::string::npos)
    {
        std::string file_id = request.substr(9);
        std::string file_path = directory_path + "/" + file_id;

        DIR *dir = opendir(file_path.c_str());

        if (dir == NULL)
        {
            perror("Error opening directory");
            exit(EXIT_FAILURE);
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL)
        {
            std::string name = entry->d_name;
            if (name == "." || name == "..")
            {
                continue;
            }

            if (entry->d_type == DT_REG)
            {
                file_path += "/" + name;
                break;
            }
        }

        closedir(dir);

        FILE *file = fopen(file_path.c_str(), "rb");
        if (!file)
        {
            std::cerr << "Failed to open file\n";
            close(client_fd);
            return nullptr;
        }

        char buffer_file[32];
        size_t bytes_read;

        while ((bytes_read = fread(buffer_file, 1, 32, file)) > 0)
        {
            if (send(client_fd, buffer_file, bytes_read, 0) == -1)
            {
                perror("Error sending file");
                break;
            }
        }

        fclose(file);
    }

    close(client_fd);
    return nullptr;
}

void *accept_thread(void *arg)
{
    struct sockaddr_in addr{};
    socklen_t addrlen = sizeof(addr);

    while (1)
    {
        int client_socket = accept(listen_fd, (struct sockaddr *)&addr, &addrlen);

        if (client_socket < 0)
        {
            perror("Accept failed");
            continue;
        }
        else
        {
            int *fd_ptr = new int(client_socket);
            pthread_t handler;
            pthread_create(&handler, nullptr, handle_client_thread, fd_ptr);
            pthread_detach(handler);
        }
    }

    return nullptr;
}

bool bind_available(int &out_fd, int &out_port)
{
    int opt = 1;
    struct sockaddr_in addr{};

    for (int i = 0; i < num_ports; ++i)
    {
        int fd = socket(AF_INET, SOCK_STREAM, 0);

        if (fd == -1)
        {
            perror("Socket creation failed");
            exit(EXIT_FAILURE);
        }

        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
        {
            perror("setsockopt failed");
            exit(EXIT_FAILURE);
        }

        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(ports[i]);

        if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0)
        {
            listen_fd = fd;
            listen_port = ports[i];

            if (listen(listen_fd, 4) < 0)
            {
                perror("Listen failed");
                close(listen_fd);
                exit(EXIT_FAILURE);
            }

            return true;
        }

        close(fd);
    }

    return false;
}

void print_menu()
{
    std::cout << "\nSeed App\n";
    std::cout << "[1] List available files.\n";
    std::cout << "[2] Download file.\n";
    std::cout << "[3] Download status.\n";
    std::cout << "[4] Exit.\n";
    std::cout << "\n? ";
}

int count_sources(int file_id, const std::string &filename)
{
    int sources_count = 0;

    for (int target_port : ports)
    {
        if (target_port == listen_port)
        {
            continue;
        }

        int sock = socket(AF_INET, SOCK_STREAM, 0);

        if (sock < 0)
        {
            continue;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(target_port);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

        if (connect(sock, (sockaddr *)&addr, sizeof(addr)) == 0)
        {
            send(sock, "LIST", 4, 0);
            char buffer[buffer_size];
            ssize_t n = recv(sock, buffer, sizeof(buffer), 0);

            if (n > 0)
            {
                buffer[n] = '\0';
                std::string data(buffer);
                std::string search_pattern = "[" + std::to_string(file_id) + "] " + filename;
                if (data.find(search_pattern) != std::string::npos)
                {
                    sources_count++;
                }
            }
        }
        close(sock);
    }

    return sources_count;
}

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
