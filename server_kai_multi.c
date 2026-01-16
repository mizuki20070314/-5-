#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define LISTEN_PORT 12345
#define BACKLOG 5
#define BUF_SIZE 4096
#define MAX_CLIENTS 16
#define MAX_DEVICES 8 

static int listen_fd = -1;
static int client_fds[MAX_CLIENTS];
static int rssi_table[MAX_DEVICES];
static int found_table[MAX_DEVICES];
static int swing_table[MAX_DEVICES];
static int device_to_fd[MAX_DEVICES]; 

static int received_count = 0; 

static void handle_sigint(int sig) {
    (void)sig;
    if (listen_fd != -1) close(listen_fd);
    for (int i = 0; i < MAX_CLIENTS; i++) if (client_fds[i] != -1) close(client_fds[i]);
    exit(0);
}

void init_tables() {
    for (int i = 0; i < MAX_DEVICES; i++) {
        rssi_table[i] = -128; found_table[i] = 0; swing_table[i] = 0;
        device_to_fd[i] = -1;
    }
    received_count = 0;
}

// JSON内のIDに基づいて適切なテーブルインデックスに保存する
static int parse_and_store_json(const char *data) {
    int swing, id, rssi, found;
    if (sscanf(data, "{\"swing\":%d,\"id\":%d,\"rssi\":%d,\"found\":%d}", &swing, &id, &rssi, &found) == 4) {
        if (id >= 0 && id < MAX_DEVICES) {
            swing_table[id] = swing;
            rssi_table[id] = rssi;
            found_table[id] = found;
            printf("Updated Table ID[%d]: RSSI=%d, Found=%d\n", id, rssi, found);
            return id;
        }
    }
    return -1;
}

static char read_command_char(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return '\0';
    int c = fgetc(f);
    while (c != EOF && (c == '\n' || c == '\r')) c = fgetc(f);
    fclose(f);
    return (c == EOF) ? '\0' : (char)c;
}

int main(void) {
    struct sockaddr_in srv_addr;
    int yes = 1;
    fd_set readfds;
    char prev_cmd = '\0';
    const char *cmd_path = "c:\\\\cygwin64\\\\home\\\\USER\\\\Creative-design-group-5\\\\command.txt";

    init_tables();
    signal(SIGINT, handle_sigint);
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_addr.s_addr = INADDR_ANY;
    srv_addr.sin_port = htons(LISTEN_PORT);
    bind(listen_fd, (struct sockaddr *)&srv_addr, sizeof(srv_addr));
    listen(listen_fd, BACKLOG);
    printf("Listening on 0.0.0.0:%d (Targeted ID storage mode)\n", LISTEN_PORT);

    for (int i = 0; i < MAX_CLIENTS; i++) client_fds[i] = -1;

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(listen_fd, &readfds);
        int maxfd = listen_fd;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_fds[i] != -1) {
                FD_SET(client_fds[i], &readfds);
                if (client_fds[i] > maxfd) maxfd = client_fds[i];
            }
        }

        select(maxfd + 1, &readfds, NULL, NULL, NULL);

        if (FD_ISSET(listen_fd, &readfds)) {
            int newfd = accept(listen_fd, NULL, NULL);
            if (newfd >= 0) {
                int assigned = 0;
                for (int d = 0; d < MAX_DEVICES; d++) {
                    if (device_to_fd[d] == -1) {
                        device_to_fd[d] = newfd;
                        for (int i = 0; i < MAX_CLIENTS; i++) {
                            if (client_fds[i] == -1) { client_fds[i] = newfd; break; }
                        }
                        printf(">>> New Connection (FD:%d) assigned to receive ID:%d results\n", newfd, d);
                        assigned = 1; break;
                    }
                }
                if (!assigned) close(newfd);
            }
        }

        for (int i = 0; i < MAX_CLIENTS; i++) {
            int fd = client_fds[i];
            if (fd != -1 && FD_ISSET(fd, &readfds)) {
                char buf[BUF_SIZE];
                ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
                if (n <= 0) {
                    for(int d=0; d<MAX_DEVICES; d++) if(device_to_fd[d] == fd) device_to_fd[d] = -1;
                    close(fd); client_fds[i] = -1; continue;
                }
                buf[n] = '\0';
                char *line = strtok(buf, "\n");
                while (line) {
                    if (line[0] != '\0') {
                        const char *redirect = (received_count == 0) ? ">" : ">>";
                        char shell_cmd[512];
                        snprintf(shell_cmd, sizeof(shell_cmd), "echo \"%s\" %s received_data.txt", line, redirect);
                        system(shell_cmd);

                        // JSONの中身のIDを見て、そのテーブル位置に保存する
                        if (parse_and_store_json(line) != -1) {
                            received_count++;
                            if (received_count >= 3) {
                                char cur_cmd = read_command_char(cmd_path);
                                for (int d = 0; d < MAX_DEVICES; d++) {
                                    int target_fd = device_to_fd[d];
                                    if (target_fd != -1) {
                                        int val = 0;
                                        if (cur_cmd != '0' && prev_cmd == '0') {
                                            val = (cur_cmd == 'A') ? 4 : 5;
                                        } else {
                                            // 接続順で決まったID(d)の最新ステータスをチェック
                                            if (found_table[d] && rssi_table[d] > -40) val = 1;
                                            else if (found_table[d] && rssi_table[d] > -53) val = 2;
                                            else if (found_table[d] && rssi_table[d] > -128) val = 3;
                                        }
                                        char res[16]; snprintf(res, 16, "%d\n", val);
                                        send(target_fd, res, strlen(res), 0);
                                    }
                                }
                                if (cur_cmd != '\0') prev_cmd = cur_cmd;
                                received_count = 0;
                                printf(">>> Batch complete (3 lines processed)\n");
                            }
                        }
                    }
                    line = strtok(NULL, "\n");
                }
            }
        }
    }
    return 0;
}
