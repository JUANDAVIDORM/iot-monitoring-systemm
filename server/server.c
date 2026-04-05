#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

#define MAX_SENSORS 64
#define MAX_OPERATORS 32
#define BUFFER_SIZE 1024
#define SENSOR_ID_LEN 32
#define SENSOR_TYPE_LEN 32
#define USERNAME_LEN 64
#define PASSWORD_LEN 64

// Datos de sensores
typedef struct {
    int active;
    char id[SENSOR_ID_LEN];
    char type[SENSOR_TYPE_LEN];
    double last_value;
    char last_timestamp[64];
} SensorInfo;

// Datos de operadores conectados
typedef struct {
    int active;
    int socket_fd;
    char username[USERNAME_LEN];
    int subscribed_alerts;
} OperatorInfo;

static SensorInfo sensors[MAX_SENSORS];
static OperatorInfo operators[MAX_OPERATORS];
static pthread_mutex_t sensors_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t operators_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

static FILE *log_file = NULL;
static int app_listen_port = 0;
static int http_listen_port = 0;

// Utilidades de logging
void log_message(const char *level, const char *client_ip, int client_port, const char *msg, const char *response) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_buffer[32];
    strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", tm_info);

    pthread_mutex_lock(&log_mutex);
    if (log_file) {
        fprintf(log_file, "[%s] [%s] %s:%d | %s | %s\n", time_buffer, level,
                client_ip ? client_ip : "-", client_port, msg ? msg : "-", response ? response : "-");
        fflush(log_file);
    }
    fprintf(stdout, "[%s] [%s] %s:%d | %s | %s\n", time_buffer, level,
            client_ip ? client_ip : "-", client_port, msg ? msg : "-", response ? response : "-");
    fflush(stdout);
    pthread_mutex_unlock(&log_mutex);
}

// Funciones auxiliares
static void trim_newline(char *s) {
    if (!s) return;
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == '\n' || s[len-1] == '\r')) {
        s[len-1] = '\0';
        len--;
    }
}

ssize_t read_line(int fd, char *buffer, size_t maxlen) {
    size_t i = 0;
    while (i < maxlen - 1) {
        char c;
        ssize_t n = recv(fd, &c, 1, 0);
        if (n <= 0) {
            if (n < 0 && (errno == EINTR)) continue;
            return n;
        }
        if (c == '\n') break;
        buffer[i++] = c;
    }
    buffer[i] = '\0';
    return (ssize_t)i;
}

int send_line(int fd, const char *line) {
    size_t len = strlen(line);
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, line + sent, len - sent, 0);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

int send_line_ln(int fd, const char *line) {
    if (send_line(fd, line) < 0) return -1;
    if (send_line(fd, "\n") < 0) return -1;
    return 0;
}

// Manejo de sensores
static SensorInfo *find_or_create_sensor(const char *id, const char *type) {
    int free_index = -1;
    for (int i = 0; i < MAX_SENSORS; ++i) {
        if (sensors[i].active && strcmp(sensors[i].id, id) == 0) {
            return &sensors[i];
        }
        if (!sensors[i].active && free_index == -1) {
            free_index = i;
        }
    }
    if (free_index == -1) return NULL;
    SensorInfo *s = &sensors[free_index];
    memset(s, 0, sizeof(SensorInfo));
    s->active = 1;
    strncpy(s->id, id, SENSOR_ID_LEN - 1);
    strncpy(s->type, type, SENSOR_TYPE_LEN - 1);
    return s;
}

static void broadcast_alert(const char *alert_msg) {
    pthread_mutex_lock(&operators_mutex);
    for (int i = 0; i < MAX_OPERATORS; ++i) {
        if (operators[i].active && operators[i].subscribed_alerts) {
            send_line_ln(operators[i].socket_fd, alert_msg);
        }
    }
    pthread_mutex_unlock(&operators_mutex);
}

static void process_sensor_data(SensorInfo *sensor, double value, const char *timestamp_str) {
    sensor->last_value = value;
    strncpy(sensor->last_timestamp, timestamp_str, sizeof(sensor->last_timestamp) - 1);

    // Reglas simples de alerta según tipo de sensor
    char alert[256];
    int should_alert = 0;

    if (strcmp(sensor->type, "TEMP") == 0) {
        if (value > 80.0 || value < 0.0) {
            snprintf(alert, sizeof(alert), "ALERT %s TEMP %.2f %s", sensor->id, value, timestamp_str);
            should_alert = 1;
        }
    } else if (strcmp(sensor->type, "VIB") == 0) {
        if (value > 5.0) {
            snprintf(alert, sizeof(alert), "ALERT %s VIB %.2f %s", sensor->id, value, timestamp_str);
            should_alert = 1;
        }
    } else if (strcmp(sensor->type, "ENERGY") == 0) {
        if (value > 1000.0) {
            snprintf(alert, sizeof(alert), "ALERT %s ENERGY %.2f %s", sensor->id, value, timestamp_str);
            should_alert = 1;
        }
    }

    if (should_alert) {
        broadcast_alert(alert);
    }
}

// Comunicación con servicio de autenticación externo (protocolo de texto simple)
int authenticate_user(const char *username, const char *password, char *role_buffer, size_t role_len) {
    const char *auth_host = getenv("AUTH_HOST");
    const char *auth_port_str = getenv("AUTH_PORT");
    if (!auth_host) auth_host = "auth.iot.local"; // nombre de dominio, no IP
    if (!auth_port_str) auth_port_str = "6000";

    struct addrinfo hints, *res, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int err = getaddrinfo(auth_host, auth_port_str, &hints, &res);
    if (err != 0) {
        fprintf(stderr, "Error en resolución de nombre para servicio de autenticación: %s\n", gai_strerror(err));
        return -1;
    }

    int sockfd = -1;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd == -1) continue;
        if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(sockfd);
        sockfd = -1;
    }
    freeaddrinfo(res);

    if (sockfd == -1) {
        fprintf(stderr, "No se pudo conectar al servicio de autenticación.\n");
        return -1;
    }

    char request[BUFFER_SIZE];
    snprintf(request, sizeof(request), "AUTH %s %s\n", username, password);
    if (send_line(sockfd, request) < 0 || send_line(sockfd, "\n") < 0) {
        close(sockfd);
        return -1;
    }

    char response[BUFFER_SIZE];
    ssize_t n = read_line(sockfd, response, sizeof(response));
    close(sockfd);
    if (n <= 0) return -1;

    trim_newline(response);
    // Respuesta esperada: "OK ROLE <rol>" o "ERROR <mensaje>"
    if (strncmp(response, "OK ROLE ", 8) == 0) {
        strncpy(role_buffer, response + 8, role_len - 1);
        role_buffer[role_len - 1] = '\0';
        return 0;
    }
    return -1;
}

// Manejo de clientes de aplicación (sensores u operadores)
void *handle_app_client(void *arg) {
    int client_fd = *(int *)arg;
    free(arg);

    struct sockaddr_storage addr;
    socklen_t addr_len = sizeof(addr);
    getpeername(client_fd, (struct sockaddr *)&addr, &addr_len);
    char client_ip[INET6_ADDRSTRLEN] = "-";
    int client_port = 0;

    if (addr.ss_family == AF_INET) {
        struct sockaddr_in *s = (struct sockaddr_in *)&addr;
        inet_ntop(AF_INET, &s->sin_addr, client_ip, sizeof(client_ip));
        client_port = ntohs(s->sin_port);
    } else if (addr.ss_family == AF_INET6) {
        struct sockaddr_in6 *s = (struct sockaddr_in6 *)&addr;
        inet_ntop(AF_INET6, &s->sin6_addr, client_ip, sizeof(client_ip));
        client_port = ntohs(s->sin6_port);
    }

    log_message("INFO", client_ip, client_port, "Nueva conexión en puerto de aplicación", "");

    char buffer[BUFFER_SIZE];
    ssize_t n = read_line(client_fd, buffer, sizeof(buffer));
    if (n <= 0) {
        close(client_fd);
        pthread_exit(NULL);
    }
    trim_newline(buffer);

    // Mensaje inicial esperado: HELLO SENSOR ... o HELLO OPERATOR
    if (strncmp(buffer, "HELLO SENSOR", 12) == 0) {
        char sensor_id[SENSOR_ID_LEN];
        char sensor_type[SENSOR_TYPE_LEN];
        if (sscanf(buffer, "HELLO SENSOR %31s %31s", sensor_id, sensor_type) != 2) {
            send_line_ln(client_fd, "ERROR Invalid SENSOR hello");
            log_message("WARN", client_ip, client_port, buffer, "ERROR Invalid SENSOR hello");
            close(client_fd);
            pthread_exit(NULL);
        }

        send_line_ln(client_fd, "OK WELCOME SENSOR");
        log_message("INFO", client_ip, client_port, buffer, "OK WELCOME SENSOR");

        while ((n = read_line(client_fd, buffer, sizeof(buffer))) > 0) {
            trim_newline(buffer);
            if (strlen(buffer) == 0) continue;
            if (strcmp(buffer, "QUIT") == 0) {
                send_line_ln(client_fd, "OK BYE");
                log_message("INFO", client_ip, client_port, "QUIT", "OK BYE");
                break;
            }

            if (strncmp(buffer, "DATA", 4) == 0) {
                char recv_id[SENSOR_ID_LEN];
                double value;
                char ts[64];
                if (sscanf(buffer, "DATA %31s %lf %63s", recv_id, &value, ts) == 3) {
                    pthread_mutex_lock(&sensors_mutex);
                    SensorInfo *sensor = find_or_create_sensor(recv_id, sensor_type);
                    if (sensor) {
                        process_sensor_data(sensor, value, ts);
                        pthread_mutex_unlock(&sensors_mutex);
                        send_line_ln(client_fd, "OK DATA RECEIVED");
                        log_message("INFO", client_ip, client_port, buffer, "OK DATA RECEIVED");
                    } else {
                        pthread_mutex_unlock(&sensors_mutex);
                        send_line_ln(client_fd, "ERROR Too many sensors");
                        log_message("ERROR", client_ip, client_port, buffer, "ERROR Too many sensors");
                    }
                } else {
                    send_line_ln(client_fd, "ERROR Invalid DATA format");
                    log_message("WARN", client_ip, client_port, buffer, "ERROR Invalid DATA format");
                }
            } else {
                send_line_ln(client_fd, "ERROR Unknown command");
                log_message("WARN", client_ip, client_port, buffer, "ERROR Unknown command");
            }
        }
    } else if (strncmp(buffer, "HELLO OPERATOR", 14) == 0) {
        send_line_ln(client_fd, "AUTH REQUIRED");
        log_message("INFO", client_ip, client_port, buffer, "AUTH REQUIRED");

        // Esperar LOGIN
        n = read_line(client_fd, buffer, sizeof(buffer));
        if (n <= 0) {
            close(client_fd);
            pthread_exit(NULL);
        }
        trim_newline(buffer);

        char username[USERNAME_LEN];
        char password[PASSWORD_LEN];
        if (sscanf(buffer, "LOGIN %63s %63s", username, password) != 2) {
            send_line_ln(client_fd, "ERROR Invalid LOGIN format");
            log_message("WARN", client_ip, client_port, buffer, "ERROR Invalid LOGIN format");
            close(client_fd);
            pthread_exit(NULL);
        }

        char role[32];
        if (authenticate_user(username, password, role, sizeof(role)) == 0) {
            send_line_ln(client_fd, "OK LOGIN");
            log_message("INFO", client_ip, client_port, buffer, "OK LOGIN");
        } else {
            send_line_ln(client_fd, "ERROR Authentication failed");
            log_message("WARN", client_ip, client_port, buffer, "ERROR Authentication failed");
            close(client_fd);
            pthread_exit(NULL);
        }

        // Registrar operador
        int operator_index = -1;
        pthread_mutex_lock(&operators_mutex);
        for (int i = 0; i < MAX_OPERATORS; ++i) {
            if (!operators[i].active) {
                operator_index = i;
                operators[i].active = 1;
                operators[i].socket_fd = client_fd;
                operators[i].subscribed_alerts = 0;
                strncpy(operators[i].username, username, USERNAME_LEN - 1);
                break;
            }
        }
        pthread_mutex_unlock(&operators_mutex);

        if (operator_index == -1) {
            send_line_ln(client_fd, "ERROR Too many operators");
            log_message("ERROR", client_ip, client_port, "Too many operators", "");
            close(client_fd);
            pthread_exit(NULL);
        }

        // Ciclo de comandos del operador
        while ((n = read_line(client_fd, buffer, sizeof(buffer))) > 0) {
            trim_newline(buffer);
            if (strlen(buffer) == 0) continue;

            if (strcmp(buffer, "QUIT") == 0) {
                send_line_ln(client_fd, "OK BYE");
                log_message("INFO", client_ip, client_port, "QUIT", "OK BYE");
                break;
            } else if (strcmp(buffer, "SUBSCRIBE ALERTS") == 0) {
                pthread_mutex_lock(&operators_mutex);
                operators[operator_index].subscribed_alerts = 1;
                pthread_mutex_unlock(&operators_mutex);
                send_line_ln(client_fd, "OK SUBSCRIBED");
                log_message("INFO", client_ip, client_port, buffer, "OK SUBSCRIBED");
            } else if (strcmp(buffer, "GET SENSORS") == 0) {
                pthread_mutex_lock(&sensors_mutex);
                for (int i = 0; i < MAX_SENSORS; ++i) {
                    if (sensors[i].active) {
                        char line[256];
                        snprintf(line, sizeof(line), "SENSOR %s %s %.2f %s",
                                 sensors[i].id, sensors[i].type, sensors[i].last_value, sensors[i].last_timestamp);
                        send_line_ln(client_fd, line);
                    }
                }
                pthread_mutex_unlock(&sensors_mutex);
                send_line_ln(client_fd, "END");
                log_message("INFO", client_ip, client_port, buffer, "SENSORS LIST SENT");
            } else {
                send_line_ln(client_fd, "ERROR Unknown command");
                log_message("WARN", client_ip, client_port, buffer, "ERROR Unknown command");
            }
        }

        // Cerrar y limpiar operador
        pthread_mutex_lock(&operators_mutex);
        if (operator_index >= 0 && operator_index < MAX_OPERATORS) {
            operators[operator_index].active = 0;
            operators[operator_index].socket_fd = -1;
            operators[operator_index].subscribed_alerts = 0;
            memset(operators[operator_index].username, 0, sizeof(operators[operator_index].username));
        }
        pthread_mutex_unlock(&operators_mutex);
    } else {
        send_line_ln(client_fd, "ERROR Unknown client type");
        log_message("WARN", client_ip, client_port, buffer, "ERROR Unknown client type");
    }

    close(client_fd);
    pthread_exit(NULL);
}

// Servidor HTTP muy simple para estado del sistema
void *handle_http_client(void *arg) {
    int client_fd = *(int *)arg;
    free(arg);

    char buffer[BUFFER_SIZE];
    ssize_t n = read_line(client_fd, buffer, sizeof(buffer));
    if (n <= 0) {
        close(client_fd);
        pthread_exit(NULL);
    }
    trim_newline(buffer);

    char method[8], path[256], version[16];
    if (sscanf(buffer, "%7s %255s %15s", method, path, version) != 3) {
        close(client_fd);
        pthread_exit(NULL);
    }

    // Leer y descartar cabeceras restantes
    while ((n = read_line(client_fd, buffer, sizeof(buffer))) > 0) {
        trim_newline(buffer);
        if (strlen(buffer) == 0) break; // línea en blanco
    }

    // Si hay parámetros de login en la URL (?user=&pass=), se podría integrar con el servicio de autenticación.
    // Para simplificar, aquí solo se muestra el estado general y sensores activos.

    char body[4096];
    char sensors_html[3072] = "";

    pthread_mutex_lock(&sensors_mutex);
    for (int i = 0; i < MAX_SENSORS; ++i) {
        if (sensors[i].active) {
            char line[256];
            snprintf(line, sizeof(line),
                     "<tr><td>%s</td><td>%s</td><td>%.2f</td><td>%s</td></tr>",
                     sensors[i].id, sensors[i].type, sensors[i].last_value, sensors[i].last_timestamp);
            strncat(sensors_html, line, sizeof(sensors_html) - strlen(sensors_html) - 1);
        }
    }
    pthread_mutex_unlock(&sensors_mutex);

    snprintf(body, sizeof(body),
             "<html><head><title>IoT Monitoring</title></head><body>"
             "<h1>Sistema de Monitoreo IoT</h1>"
             "<p>Puerto de aplicación: %d, Puerto HTTP: %d</p>"
             "<h2>Sensores activos</h2>"
             "<table border='1'><tr><th>ID</th><th>Tipo</th><th>Último valor</th><th>Última medición</th></tr>%s</table>"
             "</body></html>",
             app_listen_port, http_listen_port, sensors_html);

    char header[512];
    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: text/html; charset=utf-8\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n\r\n", strlen(body));

    send_line(client_fd, header);
    send_line(client_fd, body);

    close(client_fd);
    pthread_exit(NULL);
}

// Bucle de aceptación de conexiones de aplicación
void *app_accept_loop(void *arg) {
    int listen_fd = *(int *)arg;
    while (1) {
        struct sockaddr_storage client_addr;
        socklen_t client_len = sizeof(client_addr);
        int *client_fd = malloc(sizeof(int));
        if (!client_fd) continue;
        *client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
        if (*client_fd < 0) {
            free(client_fd);
            if (errno == EINTR) continue;
            perror("accept app");
            continue;
        }
        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_app_client, client_fd) != 0) {
            perror("pthread_create app");
            close(*client_fd);
            free(client_fd);
        } else {
            pthread_detach(tid);
        }
    }
    return NULL;
}

// Bucle de aceptación de conexiones HTTP
void *http_accept_loop(void *arg) {
    int listen_fd = *(int *)arg;
    while (1) {
        struct sockaddr_storage client_addr;
        socklen_t client_len = sizeof(client_addr);
        int *client_fd = malloc(sizeof(int));
        if (!client_fd) continue;
        *client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
        if (*client_fd < 0) {
            free(client_fd);
            if (errno == EINTR) continue;
            perror("accept http");
            continue;
        }
        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_http_client, client_fd) != 0) {
            perror("pthread_create http");
            close(*client_fd);
            free(client_fd);
        } else {
            pthread_detach(tid);
        }
    }
    return NULL;
}

int create_listen_socket(const char *port) {
    struct addrinfo hints, *res, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int err = getaddrinfo(NULL, port, &hints, &res);
    if (err != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
        return -1;
    }

    int listen_fd = -1;
    int optval = 1;

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        listen_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (listen_fd == -1) continue;

        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

        if (bind(listen_fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            if (listen(listen_fd, 16) == 0) {
                break;
            }
        }
        close(listen_fd);
        listen_fd = -1;
    }

    freeaddrinfo(res);
    return listen_fd;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Uso: %s puerto archivoDeLogs\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *port_str = argv[1];
    const char *log_path = argv[2];

    app_listen_port = atoi(port_str);
    if (app_listen_port <= 0 || app_listen_port > 65535) {
        fprintf(stderr, "Puerto inválido.\n");
        return EXIT_FAILURE;
    }
    http_listen_port = app_listen_port + 1; // HTTP en puerto adyacente

    log_file = fopen(log_path, "a");
    if (!log_file) {
        perror("No se pudo abrir archivo de logs");
        // Continuar solo con salida a consola
    }

    char app_port_buf[16];
    snprintf(app_port_buf, sizeof(app_port_buf), "%d", app_listen_port);
    int app_listen_fd = create_listen_socket(app_port_buf);
    if (app_listen_fd < 0) {
        fprintf(stderr, "No se pudo crear socket de escucha de aplicación.\n");
        return EXIT_FAILURE;
    }

    char http_port_buf[16];
    snprintf(http_port_buf, sizeof(http_port_buf), "%d", http_listen_port);
    int http_listen_fd = create_listen_socket(http_port_buf);
    if (http_listen_fd < 0) {
        fprintf(stderr, "No se pudo crear socket de escucha HTTP.\n");
        close(app_listen_fd);
        return EXIT_FAILURE;
    }

    printf("Servidor de monitoreo IoT iniciado. Puerto aplicación: %d, Puerto HTTP: %d\n",
           app_listen_port, http_listen_port);

    pthread_t app_thread, http_thread;
    if (pthread_create(&app_thread, NULL, app_accept_loop, &app_listen_fd) != 0) {
        perror("pthread_create app_accept_loop");
        close(app_listen_fd);
        close(http_listen_fd);
        return EXIT_FAILURE;
    }
    if (pthread_create(&http_thread, NULL, http_accept_loop, &http_listen_fd) != 0) {
        perror("pthread_create http_accept_loop");
        close(app_listen_fd);
        close(http_listen_fd);
        return EXIT_FAILURE;
    }

    pthread_join(app_thread, NULL);
    pthread_join(http_thread, NULL);

    if (log_file) fclose(log_file);
    close(app_listen_fd);
    close(http_listen_fd);
    return EXIT_SUCCESS;
}
