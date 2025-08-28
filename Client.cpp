#include "Client.h"

Client::Client(const std::vector<int> &ports, int listen_port, const std::string &directory_path)
{
    this->ports = ports;
    this->listen_port = listen_port;
    this->directory_path = directory_path;
}

void Client::run()
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

void Client::print_menu()
{
    std::cout << "\nSeed App\n";
    std::cout << "[1] List available files.\n";
    std::cout << "[2] Download file.\n";
    std::cout << "[3] Download status.\n";
    std::cout << "[4] Exit.\n";
    std::cout << "\n? ";
}

void Client::list_available_files()
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

void *Client::request_files_helper(void *arg)
{
    RequestArgs *args = static_cast<RequestArgs *>(arg);
    void *ret = args->client_ptr->request_files(args->port_ptr);
    delete args;
    return ret;
}

void *Client::request_files(int *port_ptr)
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

void Client::download_file()
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

void *Client::download_from_specific_port_helper(void *arg)
{
    DownloadArgs *args = static_cast<DownloadArgs *>(arg);
    void *ret = args->client_ptr->download_from_specific_port(args->port_info);
    delete args;
    return ret;
}

void *Client::download_from_specific_port(PortDownloadInfo *port_info)
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
    char buffer[32];
    std::string file_path = "files/" + std::to_string(port_info->file_id) + "/" + port_info->filename;
    for (const auto &chunk : port_info->chunks)
    {
        std::string request = "DOWNLOAD " + std::to_string(chunk.file_id) + " " + std::to_string(chunk.start_byte) + " " + std::to_string(chunk.chunk_size) + "\n";
        ssize_t sent = send(sock, request.c_str(), request.size(), 0);
        if (sent < 0)
        {
            continue;
        }
        long bytes_received = 0;
        FILE *file = fopen(file_path.c_str(), "r+b");
        if (!file)
        {
            continue;
        }

        fseek(file, chunk.start_byte, SEEK_SET);
        while (bytes_received < chunk.chunk_size)
        {
            long to_read = std::min(32L, chunk.chunk_size - bytes_received);
            ssize_t n = recv(sock, buffer, to_read, 0);
            if (n <= 0)
            {
                break;
            }
            fwrite(buffer, 1, n, file);
            fflush(file);
            bytes_received += n;
            {
                std::lock_guard<std::mutex> lock(files_mutex);
                current_downloads[port_info->file_id].bytes_downloaded += n;
            }
        }
        fclose(file);
    }
    close(sock);
    delete port_info;
    return nullptr;
}

int Client::count_sources(int file_id, const std::string &filename)
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

std::vector<int> Client::find_ports_with_file(int file_id, const std::string &filename)
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

void Client::show_download_status()
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

void Client::download_status(const std::pair<const int, DownloadInfo> *entry)
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

void Client::cleanup_completed_downloads()
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
            it++;
        }
    }
}