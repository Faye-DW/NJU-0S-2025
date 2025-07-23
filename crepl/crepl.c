/*#include <stdio.h>
#include <stdbool.h>

// Compile a function definition and load it
bool compile_and_load_function(const char* function_def) {
    return false;
}

// Evaluate an expression
bool evaluate_expression(const char* expression, int* result) {
    return false;
}

int main() {
}*/

#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <setjmp.h>

//#define TMPC "lib.c"
//#define TMPO "lib.so"
//#define TMP  "tmp.c"
//#define LOG  "log.txt"

#define TMPC "/tmp/lib.c"
#define TMPO "/tmp/lib.so"
#define TMP  "/tmp/tmp.c"
#define LOG  "/tmp/log.txt"

#define P printf("%d\n", __LINE__);

char *save_origin();
void use_origin(char *origin_content);
int compile();
void clean_tmpfiles();
void check_error(int sig);

static sigjmp_buf env;
int expr_num = 0;

int main(){
    clean_tmpfiles();
    signal(SIGFPE, check_error);

    char buffer[1024];
    void *handle = NULL;
    char* origin_file = save_origin();

    while (1){
        //----------rewrite the origin good file-----------
        FILE *tmp = fopen(TMP, "w");
        if (!tmp){
            perror("fail to open TMP");
            return 1;
        }
        fprintf(tmp, "%s", origin_file);
        fflush(tmp);
        //-------------------------------------------------
        if (fgets(buffer, sizeof(buffer), stdin) == NULL){
            fclose(tmp);
            return 0;
        }
        /*if (strncmp(buffer, "quit", 4) == 0){
            fclose(tmp);
            break;
        }*/
        //--------process the info cin-------
        int len = strlen(buffer);
        buffer[len-1] = '\0';
        //-----------------------------------
        int lib;
        if (buffer[0] == 'i'){
            fprintf(tmp, "%s\n", buffer);
            fflush(tmp);
            fclose(tmp);
            rename(TMP, TMPC);
            lib = compile();
        
            if (lib == -1){
                fprintf(stdout, "Compile Error:%d\n", __LINE__);
                fflush(stdout);
                use_origin(origin_file);
            }
	        else{
		        fprintf(stdout, "Compile Ok:%d\n", __LINE__);
		        fflush(stdout);
                free(origin_file);
                origin_file = save_origin();
	        }
        }
        else{
            fprintf(tmp, "int expr_%d(){return %s;}\n", expr_num, buffer);
            fflush(tmp);
            fclose(tmp);
            rename(TMP, TMPC);
            lib = compile();
            if (lib == -1){
                fprintf(stdout, "Compile Error:%d\n", __LINE__);
                fflush(stdout);
                use_origin(origin_file);
            }
            else{
		        fprintf(stdout, "Compile Ok:%d\n", __LINE__);
		        fflush(stdout);
                free(origin_file);
                origin_file = save_origin();
	        }
            if (handle){
                dlclose(handle);
            }
            handle = dlopen(TMPO, RTLD_LAZY);
            char func_name[1024];
            snprintf(func_name, 1024, "expr_%d", expr_num);
            expr_num++;
            int (*func)() = (int (*)())dlsym(handle, func_name);
            if (!func) {
                fprintf(stdout, "Compile Error:%d\n", __LINE__);
                fflush(stdout);
                use_origin(origin_file);
                continue;
            }
            if (sigsetjmp(env, 1) == 0) {
                int result = func();
                printf("=%d\n", result);
                fflush(stdout);
            } 
            else {
                printf("=ERROR: Division by zero\n");
                fflush(stdout);
            }
        }  
    }
    return 0;
}

int compile() {
    pid_t pid = fork();
    int stderr_back = dup(STDERR_FILENO);
    int log_fd = open(LOG, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (pid == 0){
        if (log_fd == -1){
            perror("log_fd failed\n");
        }
        if (dup2(log_fd, STDERR_FILENO) == -1){
            perror("dup2 failed\n");
            return -1;
        }
        char *argv[] = {
            "gcc",
            "-fPIC",
            "-shared",
            "-Werror",
            "-o", TMPO,
            TMPC,
            NULL
        };
        execvp("gcc", argv);
        perror("execvp failed\n");
        return -1;
    }
    else{
        int status;
        waitpid(pid, &status, 0);
        dup2(stderr_back, STDERR_FILENO);
        close(stderr_back);
        close(log_fd);
        if (WIFEXITED(status)) {
            return (WEXITSTATUS(status) == 0) ? 0 : -1;
        } else {
            return -1;
        }
    }
}

void clean_tmpfiles() {
    remove(TMPC);
    remove(TMPO);
    return;
}

char* save_origin(){
    if (access(TMPC, F_OK) == -1){
        FILE *tmp = fopen(TMPC, "w");
        if (!tmp){
            perror("open failed in save origin");
            return NULL;
        }
        fclose(tmp);
    }
    FILE *tmp = fopen(TMPC, "r");
    if (!tmp){
        perror("fail to open file to read");
        return NULL;
    }
    fseek(tmp, 0, SEEK_END);
    long size = ftell(tmp);
    fseek(tmp, 0, SEEK_SET);
    char *origin_file_content = malloc(size + 1);
    if (!origin_file_content){
        perror("fail to malloc");
        fclose(tmp);
        return NULL;
    }
    size_t read_len = fread(origin_file_content, 1, size, tmp);
    if (read_len == -1){
        return NULL;
    }
    origin_file_content[size] = '\0';
    fclose(tmp);
    return origin_file_content;
}

void use_origin(char *origin_content){
    FILE *tmp = fopen(TMPC, "w");
    if (!tmp){
        perror("fail to open file to write");
        fclose(tmp);
        return;
    }
    fprintf(tmp, "%s", origin_content);
    fclose(tmp);
    return;
}

void check_error(int sig){
    //fprintf(stderr, "Caught division by zero! Signal: %d\n", sig);
    //exit(EXIT_FAILURE);
    siglongjmp(env, 1);
}
