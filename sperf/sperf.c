#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>

/****************************** debug **********************************/
#ifdef LOCAL_MACHINE
#define Assert(expr, fmt, ...) if (!(expr)) {printf(fmt "\n", ##__VA_ARGS__); assert(0); }
#define Debug(fmt, ...) printf(fmt, ## __VA_ARGS__)
#else
#define Assert(expr, fmt, ...)
#define Debug(fmt, ...)
#endif

/****************************** trace **********************************/
typedef struct syscall_trace {
    char   name[32];
    double time;
} sys_trace_t;

static sys_trace_t sys_trace[400];
static int     nr_sys_trace = 0;
static double  total_time = 0;

sys_trace_t *find_trace(char *name) {
    for (int i = 0; i < nr_sys_trace; ++i) {
        if (strcmp(name, sys_trace[i].name) == 0) return &sys_trace[i];
    }
    return NULL;
}

void update_trace(sys_trace_t *t) {
    sys_trace_t *ptrace = find_trace(t->name);
    if (!ptrace) {
        ptrace = &sys_trace[nr_sys_trace++];
        strncpy(ptrace->name, t->name, 32);
        ptrace->time = 0;
    }

    ptrace->time += t->time;
    total_time   += t->time;
}

int trace_compar(const void *a, const void *b) {
    return ((sys_trace_t *)a)->time < ((sys_trace_t *)b)->time;
}

void display_trace() {
    qsort(sys_trace, nr_sys_trace, sizeof(sys_trace_t), trace_compar);

    printf("====================\n");
    //printf("Time: %.1lfs\n", ((double)timer.utime) / 10);
    int max_line = (nr_sys_trace < 5) ? nr_sys_trace : 5;
    for (int i = 0; i < max_line; ++i) {
        printf("%s (%d%%)\n", sys_trace[i].name, (int)(sys_trace[i].time * 100.0 / total_time));
    }
    for (int i = 0; i < 80; ++i) {
        putchar('\0');
    }
    putchar('\n');
    fflush(stdout);
}

/****************************** timer **********************************/
typedef struct custom_timer {
    struct timeval start;
    int elapsed_time;
} custom_timer_t;

static custom_timer_t timer = {.elapsed_time = 0};

static inline void start_timer() {
    gettimeofday(&timer.start, NULL);
}

static int update_time() {
    static struct timeval now;
    gettimeofday(&now, NULL);
    int current_time = 
        (now.tv_sec - timer.start.tv_sec) * 10 + 
        (now.tv_usec - timer.start.tv_usec) / 100000;
    
    if (current_time > timer.elapsed_time) {
        timer.elapsed_time = current_time;
        return 1;
    }
    return 0;
}

/****************************** child **********************************/
#define NR_PRE_ARGS 3
#define DEFAULT_ENV_PATH "PATH=/bin:/usr/bin"

void execute_child(int argc, char *argv[], char *envp[]) {
    char  target_executable[64];
    char *exec_argv[argc + NR_PRE_ARGS + 1];
    char *path_env = DEFAULT_ENV_PATH;
    int i = 0;

    exec_argv[0] = target_executable;
    exec_argv[1] = "-T";
    exec_argv[2] = "-xx";
    while(argv[i]) {
        exec_argv[NR_PRE_ARGS + i] = argv[i];
        i++;
    }
    exec_argv[NR_PRE_ARGS + i] = NULL;

    i = 0;
    while(envp[i]) {
        if (strncmp("PATH=", envp[i], 5) == 0) 
            break;
        i++;
    }
    if (envp[i]) {
        path_env = (char *)malloc(strlen(envp[i]) + 1);
        strcpy(path_env, envp[i]);
    }
    
    char* exec_path = strtok(&(path_env[5]), ":");
    while(exec_path) {
        snprintf(target_executable, 64, "%s/strace", exec_path);
        execve(target_executable, exec_argv, envp);
        exec_path = strtok(NULL, ":");
    }

    Assert(0, "should not reach here");
}

/****************************** parent *********************************/
typedef enum {
    INITIAL, PROCESSING, FINAL
} trace_state;

#define SKIP_WHITESPACE() while((c = getchar()) == ' ')

void process_parent() {
    char c;
    char buffer[32];
    int  buffer_pos = 0;
    sys_trace_t current_trace;
    trace_state state = INITIAL;

    start_timer();
    while ((c = getchar()) != EOF) {
        switch(state) {
        case INITIAL:
            if (c != '(') {
                buffer[buffer_pos++] = c;
                if (buffer_pos == 32) buffer_pos = 0;
            }
            else {
                buffer[buffer_pos] = '\0';
                strcpy(current_trace.name, buffer);
                state = PROCESSING;
                buffer_pos = 0;
            }
            break;
        
        case PROCESSING:
            if (c == ')') {
                SKIP_WHITESPACE();
                if (c == '=') {
                    state = FINAL;
                    Assert(buffer_pos == 0, "buffer_pos != 0");
                }
            }
            break;

        case FINAL:
            if (c == '<') {
                while((c = getchar()) != '>') {
                    if (!(c == '.' || (c >= '0' && c <= '9'))) {
                        state = PROCESSING;
                        break;
                    }
                    buffer[buffer_pos++] = c;
                }
                if ((c = getchar()) != '\n') {
                    state = PROCESSING;
                    break;
                }
                buffer[buffer_pos] = '\0';
                current_trace.time = atof(buffer);
                update_trace(&current_trace);

                if (update_time())
                    display_trace();
                state = INITIAL;
                buffer_pos = 0;
            }
            break;
        }
    }
    update_time();
    display_trace();
}

int main(int argc, char *argv[], char *envp[]) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        exit(1);
    }

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        exit(1);
    }
    else if (pid) {
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        process_parent();
    }
    else {
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        int fd = open("/dev/null", O_WRONLY);
        dup2(fd, STDOUT_FILENO);
        execute_child(argc - 1, &argv[1], envp);
    }

    return 0;
}