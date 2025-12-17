#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

// General lwIP options
#define NO_SYS 0
#define SYS_LIGHTWEIGHT_PROT 1
#define LWIP_SOCKET 0
#define LWIP_NETCONN 0

// MQTT / application protocols
#define LWIP_MQTT 1
#define MQTT_OUTPUT_RINGBUF_SIZE 1024
#define MQTT_REQ_MAX_IN_FLIGHT 4

// DHCP options
#define LWIP_DHCP 1
#define LWIP_DHCP_CHECK_LINK_UP 1

// ARP options
#define LWIP_ARP 1
#define LWIP_ETHERNET 1

// IP options
#define LWIP_IPV4 1
#define LWIP_ICMP 1

// TCP options
#define LWIP_TCP 1
#define TCP_MSS 1460
#define TCP_SND_BUF (8 * TCP_MSS)
#define TCP_WND (8 * TCP_MSS)

// UDP options
#define LWIP_UDP 1

// DNS options
#define LWIP_DNS 1
#define DNS_MAX_SERVERS 2

// IGMP options (for multicast)
#define LWIP_IGMP 1

// Network interface options
#define LWIP_NETIF_HOSTNAME 1
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_LINK_CALLBACK 1

// Stats options
#define LWIP_STATS 0
#define LWIP_STATS_DISPLAY 0

// Checksum options
#define LWIP_CHECKSUM_ON_COPY 1

// Thread options (required with NO_SYS=0)
#define TCPIP_THREAD_STACKSIZE 2048
#define TCPIP_THREAD_PRIO 3
#define DEFAULT_THREAD_STACKSIZE 1024
#define DEFAULT_THREAD_PRIO 3

// Memory options
#define MEM_ALIGNMENT 4
#define MEMP_NUM_TCP_SEG 32
#define MEMP_NUM_PBUF 24
#define PBUF_POOL_SIZE 24

#endif /* _LWIPOPTS_H */
