#include "Server.h"

Server::Server(const std::string &dir, const std::vector<int> &ports)
{
    directory_path = dir;
    this->ports = ports;
    listen_fd = -1;
    listen_port = -1;
}

void Server::start()
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

int Server::get_listen_port() const
{
    return listen_port;
}

const std::vector<int> &Server::get_ports() const
{
    return ports;
}

std::string Server::get_directory_path() const
{
    return directory_path;
}

void *Server::accept_thread_helper(void *arg)
{
    return static_cast<Server *>(arg)->accept_thread();
}

void *Server::accept_thread()
{
    struct sockaddr_in addr{};
    socklen_t addrlen = sizeof(addr);
    while (1)
    {
        int *client_socket = new int(accept(listen_fd, (struct sockaddr *)&addr, &addrlen));
        if (*client_socket < 0)
        {
            perror("Accept failed");
            continue;
        }
        else
        {
            pthread_t handler;
            pthread_create(&handler, nullptr, handle_client_thread_helper, new AcceptArgs{client_socket, this});
            pthread_detach(handler);
        }
    }
    return nullptr;
}

void *Server::handle_client_thread_helper(void *arg)
{
    AcceptArgs *args = static_cast<AcceptArgs *>(arg);
    void *ret = args->server_ptr->handle_client_thread(args->fd_ptr);
    delete args;
    return ret;
}

void *Server::handle_client_thread(int *client_fd_ptr)
{
    int client_fd = *client_fd_ptr;
    delete client_fd_ptr;

    while (1)
    {
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
            std::istringstream iss(request);
            std::string cmd;
            int file_id;
            long start_byte, chunk_size;
            iss >> cmd >> file_id >> start_byte >> chunk_size;
            std::string file_dir = directory_path + "/" + std::to_string(file_id);
            DIR *dir = opendir(file_dir.c_str());
            std::string file_path;

            if (dir == NULL)
            {
                perror("Error opening directory");
                break;
            }
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL)
            {
                std::string name = entry->d_name;
                if (name == "." || name == "..")
                    continue;
                if (entry->d_type == DT_REG)
                {
                    file_path = file_dir + "/" + name;
                    break;
                }
            }
            closedir(dir);
            if (file_path.empty())
            {
                std::cerr << "File not found in directory\n";
                break;
            }
            FILE *file = fopen(file_path.c_str(), "rb");
            if (!file)
            {
                std::cerr << "Failed to open file\n";
                break;
            }
            fseek(file, start_byte, SEEK_SET);
            char buffer_file[32];
            long bytes_left = chunk_size;
            while (bytes_left > 0)
            {
                size_t to_read = std::min(32L, bytes_left);
                size_t bytes_read = fread(buffer_file, 1, to_read, file);
                if (bytes_read == 0)
                    break;
                if (send(client_fd, buffer_file, bytes_read, 0) == -1)
                {
                    perror("Error sending file chunk");
                    break;
                }
                bytes_left -= bytes_read;
            }
            fclose(file);
        }
    }

    close(client_fd);
    return nullptr;
}

std::map<int, FileInfo> Server::list_files()
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

bool Server::bind_available()
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