// socket_prov.c
#include "socket_prov.h"
#include "esp_log.h"
#include <errno.h>
#include <fcntl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <netdb.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static const char *TAG = "socket_prov";

static int g_sock = -1;
static SemaphoreHandle_t g_mutex = NULL;

int socket_prov_init(void) {
  if (g_mutex != NULL) {
    ESP_LOGW(TAG, "Component already initialized");
    return 0;
  }

  g_mutex = xSemaphoreCreateMutex();
  if (g_mutex == NULL) {
    ESP_LOGE(TAG, "Failed to create mutex");
    return -1;
  }

  g_sock = -1;
  ESP_LOGI(TAG, "Socket provider initialized");
  return 0;
}

void socket_prov_deinit(void) {
  if (g_mutex) {
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    if (g_sock != -1) {
      close(g_sock);
      g_sock = -1;
    }
    xSemaphoreGive(g_mutex);
    vSemaphoreDelete(g_mutex);
    g_mutex = NULL;
  }
  ESP_LOGI(TAG, "Socket provider deinitialized");
}

int socket_prov_connect(const char *host, uint16_t port) {
  if (g_mutex == NULL) {
    ESP_LOGE(TAG, "Component not initialized");
    return -1;
  }

  xSemaphoreTake(g_mutex, portMAX_DELAY);

  // Close existing socket if any
  if (g_sock != -1) {
    close(g_sock);
    g_sock = -1;
  }

  struct addrinfo hints = {
      .ai_family = AF_INET,
      .ai_socktype = SOCK_STREAM,
  };
  struct addrinfo *res;
  char port_str[6];
  snprintf(port_str, sizeof(port_str), "%d", port);
  int ret = getaddrinfo(host, port_str, &hints, &res);
  if (ret != 0) {
    ESP_LOGE(TAG, "getaddrinfo failed: %s", strerror(ret));
    xSemaphoreGive(g_mutex);
    return -1;
  }

  int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (sock < 0) {
    ESP_LOGE(TAG, "socket() failed: %s", strerror(errno));
    freeaddrinfo(res);
    xSemaphoreGive(g_mutex);
    return -1;
  }

  // Optional: set non-blocking, etc.

  if (connect(sock, res->ai_addr, res->ai_addrlen) != 0) {
    ESP_LOGE(TAG, "connect() failed: %s", strerror(errno));
    close(sock);
    freeaddrinfo(res);
    xSemaphoreGive(g_mutex);
    return -1;
  }

  freeaddrinfo(res);
  g_sock = sock;
  xSemaphoreGive(g_mutex);
  ESP_LOGI(TAG, "Connected to %s:%d", host, port);
  return 0;
}

int socket_prov_get_socket(void) {
  int sock;
  if (g_mutex == NULL) {
    ESP_LOGE(TAG, "Component not initialized");
    return -1;
  }
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  sock = g_sock;
  xSemaphoreGive(g_mutex);
  return sock;
}

ssize_t socket_prov_send(const void *data, size_t len, int flags) {
  if (g_mutex == NULL) {
    ESP_LOGE(TAG, "Component not initialized");
    return -1;
  }
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  ssize_t ret = -1;
  if (g_sock != -1) {
    ret = send(g_sock, data, len, flags);
    if (ret < 0) {
      ESP_LOGE(TAG, "send() failed: %s", strerror(errno));
    }
  } else {
    ESP_LOGE(TAG, "Socket not connected");
    errno = ENOTCONN;
  }
  xSemaphoreGive(g_mutex);
  return ret;
}

ssize_t socket_prov_recv(void *buffer, size_t len, int flags) {
  if (g_mutex == NULL) {
    ESP_LOGE(TAG, "Component not initialized");
    return -1;
  }
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  ssize_t ret = -1;
  if (g_sock != -1) {
    ret = recv(g_sock, buffer, len, flags);
    if (ret < 0) {
      ESP_LOGE(TAG, "recv() failed: %s", strerror(errno));
    }
  } else {
    ESP_LOGE(TAG, "Socket not connected");
    errno = ENOTCONN;
  }
  xSemaphoreGive(g_mutex);
  return ret;
}

int socket_prov_close(void) {
  if (g_mutex == NULL) {
    ESP_LOGE(TAG, "Component not initialized");
    return -1;
  }
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  int ret = 0;
  if (g_sock != -1) {
    if (close(g_sock) != 0) {
      ESP_LOGE(TAG, "close() failed: %s", strerror(errno));
      ret = -1;
    } else {
      g_sock = -1;
    }
  }
  xSemaphoreGive(g_mutex);
  return ret;
}