#ifndef APP_NETWORK_CONFIG_H
#define APP_NETWORK_CONFIG_H

#define APP_NETWORK_W5500_SOCKET_COUNT          8U

/* Mutex只保护芯片访问，不能改变Socket所有者。 */
#define APP_NETWORK_SOCKET_MQTT                 0U
#define APP_NETWORK_SOCKET_FTP_CONTROL          1U
#define APP_NETWORK_SOCKET_FTP_DATA             2U

_Static_assert(
    APP_NETWORK_SOCKET_MQTT < APP_NETWORK_W5500_SOCKET_COUNT,
    "MQTT socket is out of range");
_Static_assert(
    APP_NETWORK_SOCKET_FTP_CONTROL < APP_NETWORK_W5500_SOCKET_COUNT,
    "FTP control socket is out of range");
_Static_assert(
    APP_NETWORK_SOCKET_FTP_DATA < APP_NETWORK_W5500_SOCKET_COUNT,
    "FTP data socket is out of range");
_Static_assert(
    APP_NETWORK_SOCKET_MQTT != APP_NETWORK_SOCKET_FTP_CONTROL,
    "MQTT and FTP control sockets must differ");
_Static_assert(
    APP_NETWORK_SOCKET_MQTT != APP_NETWORK_SOCKET_FTP_DATA,
    "MQTT and FTP data sockets must differ");
_Static_assert(
    APP_NETWORK_SOCKET_FTP_CONTROL != APP_NETWORK_SOCKET_FTP_DATA,
    "FTP control and data sockets must differ");

#endif /* APP_NETWORK_CONFIG_H */