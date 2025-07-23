#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <sys/time.h>
#include <ctype.h>

#define BUFFER_SIZE 4096
#define MAX_PATH_LENGTH 1024
#define DEFAULT_PORT 8080
#define MAX_THREADS 4

void *handle_request(void *arg);
void log_request(const char *method, const char *path, int status_code);

// Thread synchronization
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
int thread_count = 0;

// Log sequence control
int request_counter = 0;
int next_log_seq = 1;
pthread_mutex_t seq_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t seq_cond = PTHREAD_COND_INITIALIZER;

typedef struct {
    int client_socket;
    int seq;
} thread_arg_t;

int main(int argc, char *argv[]) {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    int port = (argc > 1) ? atoi(argv[1]) : DEFAULT_PORT;

    signal(SIGPIPE, SIG_IGN);

    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Setsockopt failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket, SOMAXCONN) < 0) {
        perror("Listen failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", port);

    while (1) {
        if ((client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len)) < 0) {
            perror("Accept failed");
            continue;
        }

        struct timeval timeout = {30, 0};
        setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(client_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

        pthread_mutex_lock(&mutex);
        while (thread_count >= MAX_THREADS) {
            pthread_cond_wait(&cond, &mutex);
        }

        pthread_mutex_lock(&seq_mutex);
        int seq = ++request_counter;
        pthread_mutex_unlock(&seq_mutex);

        thread_arg_t *arg = malloc(sizeof(thread_arg_t));
        if (!arg) {
            perror("Malloc failed");
            close(client_socket);
            pthread_mutex_unlock(&mutex);
            continue;
        }
        arg->client_socket = client_socket;
        arg->seq = seq;

        pthread_t thread;
        if (pthread_create(&thread, NULL, handle_request, arg) != 0) {
            perror("Thread creation failed");
            close(client_socket);
            free(arg);
            pthread_mutex_unlock(&mutex);
            continue;
        }
        
        thread_count++;
        pthread_detach(thread); // Detach thread to avoid memory leaks
        pthread_mutex_unlock(&mutex);
    }

    close(server_socket);
    return 0;
}

void send_error_response(int client_socket, int status_code, const char *status_text) {
    char response[1024];
    int len = snprintf(response, sizeof(response),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/html\r\n"
        "Connection: close\r\n\r\n"
        "<html><body><h1>%d %s</h1></body></html>",
        status_code, status_text, status_code, status_text);
    if (len > 0) {
        send(client_socket, response, len, 0);
    }
}


int parse_status_code(const char *response) {
    
    if (strncmp(response, "HTTP/1.", 7) == 0) {
        const char *status_start = strchr(response, ' ');
        if (status_start) {
            return atoi(status_start + 1);
        }
    }
    
   
    const char *status_header = strstr(response, "Status: ");
    if (status_header) {
        return atoi(status_header + 8);
    }
    
    
    return 200;
}

void *handle_request(void *arg) {
    int status_code;
    thread_arg_t *targ = (thread_arg_t *)arg;
    int client_socket = targ->client_socket;
    int seq = targ->seq;
    free(targ);
    char buffer[BUFFER_SIZE];
    int bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
    //printf("%s\n", buffer);
    if (bytes_received <= 0) {
        status_code = 400;
        close(client_socket);
        goto cleanup;
    }
    buffer[bytes_received] = '\0';
    //-----------------------------
    char method[1024];
    char path[1024];
    int index1,index2;
    for (int i=0;i<bytes_received;i++){
        if (buffer[i] == ' '){
            index1 = i;
            break;
        }
    }
    for (int i=0;i<index1;i++){
        method[i] = buffer[i];
    }
    method[index1] = '\0';
    for (int i=index1+1;i<bytes_received;i++){
        if (buffer[i] == ' '){
            index2 = i;
            break;
        }
    }
    for (int i=index1+1;i<index2;i++){
        path[i-index1-1] = buffer[i];
    }
    path[strlen(path)] = '\0';
    //char *method = strtok(buffer, " ");
    //char *path = strtok(NULL, " ");
    const char *path_without_query = path;
    /*if (!method || !path) {
        status_code = 400;
        send_error_response(client_socket, status_code, "Bad Request");
        close(client_socket);
        goto cleanup;
    }*/

    char *query_string = strchr(path, '?');
    path_without_query = path;
    if (query_string) {
        *query_string = '\0';
        query_string++;
    } 
    else {
        query_string = "";
    }

    if (strncmp(path_without_query, "/cgi-bin/", 9) != 0) {
        status_code = 404;
        send_error_response(client_socket, status_code, "Not Found");
        close(client_socket);
        goto cleanup;
    }

    char script_path[MAX_PATH_LENGTH];
    snprintf(script_path, MAX_PATH_LENGTH, ".%s", path_without_query);
    //printf("%s %s %s %s %s\n", method, path, protocol, script_path, query_string);
    if (access(script_path, X_OK) == -1) {
        status_code = 404;
        send_error_response(client_socket, status_code, "Not Found");
        close(client_socket);
        goto cleanup;
    }

    
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("Pipe creation failed");
        status_code = 500;
        send_error_response(client_socket, status_code, "Internal Server Error");
        close(client_socket);
        goto cleanup;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("Fork failed");
        close(pipefd[0]);
        close(pipefd[1]);
        status_code = 500;
        send_error_response(client_socket, status_code, "Internal Server Error");
        close(client_socket);
        goto cleanup;
    }

    if (pid == 0) { 
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        char *envp[3];
        envp[0] = "REQUEST_METHOD=GET";
        char arr[1024];
        snprintf(arr, 1024, "QUERY_STRING=%s", query_string);
        envp[1] = arr;
        envp[2] = NULL;
        execle(script_path, script_path, NULL, envp);
        _exit(1);
    }
    else {
        close(pipefd[1]);
        char cgi_output[BUFFER_SIZE];
        ssize_t total_bytes = 0;
        ssize_t bytes_read;
        
        while ((bytes_read = read(pipefd[0], cgi_output + total_bytes, 
                                BUFFER_SIZE - total_bytes - 1)) > 0) {
            total_bytes += bytes_read;
            if (total_bytes >= BUFFER_SIZE - 1) {
                break;
            }
        }
        cgi_output[total_bytes] = '\0';
        close(pipefd[0]);
        
        
        int child_status;
        waitpid(pid, &child_status, 0);
        if (WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0) {
            status_code = parse_status_code(cgi_output);
            send(client_socket, cgi_output, total_bytes, 0);
            goto cleanup;
        }
        else {
            status_code = 500;
            send_error_response(client_socket, status_code, "Internal Server Error");
            goto cleanup;
        }
        
        //close(client_socket);
    }

cleanup:
    /*pthread_mutex_lock(&seq_mutex);
    while (seq != next_log_seq) {
        pthread_cond_wait(&seq_cond, &seq_mutex);
    }*/
    log_request(method, path, status_code);
    //next_log_seq++;
    //pthread_cond_broadcast(&seq_cond);
    //pthread_mutex_unlock(&seq_mutex);

    

    close(client_socket);
    pthread_mutex_lock(&mutex);
    thread_count--;
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&mutex);
    return NULL;
}



void log_request(const char *method, const char *path, int status_code) {
    time_t now;
    struct tm *tm_info;
    char timestamp[26];

    time(&now);
    tm_info = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

    printf("[%s] [%s] [%s] [%d]\n", timestamp, method, path, status_code);
    fflush(stdout);
}