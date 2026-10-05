#include "FreeRTOS.h"
#include "task.h"
#include <platform/platform_stdlib.h>
#include <lwip/sockets.h>
#include <lwip_netconf.h>
#include <wifi_conf.h>
//#include <osdep_api.h>
#include <osdep_service.h>
#include "serial_api.h"
#include "serial_ex_api.h"

#include "gpio_api.h"   // mbed
#include "gateway_diag.h"

extern struct netif xnetif[NET_IF_NUM];

#define SERVER_PORT     80
#define LISTEN_QLEN     2

#define UART_TX    	_PA_18	//UART0  TX
#define UART_RX    	_PA_19	//UART0  RX
#define UART_RTS  	_PA_16	//UART0  RTS
#define UART_CTS   	_PA_17	//UART0  CTS
#define LED_BLUE    _PB_22 	//Blue LED

gpio_t led_blue;

static int tx_exit = 0, rx_exit = 0;
//static _Sema tcp_tx_rx_sema;
static _sema tcp_tx_rx_sema;

// i`m not sure if this is needed
// maybe UART can work in separate tasks
// maybe later i`ll check
static _sema uart_tx_rx_sema;

//#define Z_UART_DEBUG
serial_t sobj;


void uart_send_data(serial_t *sobj, void *data, int count)
{
    unsigned int i=0;
    gateway_diag_task_state(GW_DIAG_ROLE_RX, GW_DIAG_STATE_UART_SEM);
    rtw_down_sema(&uart_tx_rx_sema);
    /* serial_putc may wait indefinitely for writable/flow-control state.
     * This marker describes the API wait; it does not sample the CTS pin. */
    gateway_diag_task_state(GW_DIAG_ROLE_RX, GW_DIAG_STATE_UART_WRITE);
    for (i=0;i<count;i++) {
        serial_putc(sobj, ((char *)data)[i]);
        gateway_diag_task_progress(GW_DIAG_ROLE_RX);
    }
    rtw_up_sema(&uart_tx_rx_sema);
    gateway_diag_add(GW_DIAG_UART_TX_BYTES, (unsigned long)count);
    gateway_diag_task_state(GW_DIAG_ROLE_RX, GW_DIAG_STATE_IDLE);
}


static void tx_thread(void *param)
{
	int client_fd = * (int *) param;
	unsigned long exit_reason = 0;
	unsigned char buffer[1024];
	gateway_diag_task_start(GW_DIAG_ROLE_TX);
	memset(buffer, 0, sizeof(buffer));
	gpio_write(&led_blue, 1); // turn on blue LED
	printf("\n%s start\n", __FUNCTION__);

	while(1) {
		int ret = 0;
        gateway_diag_task_heartbeat(GW_DIAG_ROLE_TX);
        gateway_diag_task_state(GW_DIAG_ROLE_TX, GW_DIAG_STATE_IDLE);


        // simple buffering
        // to avoid sending frame with few bytes
        // if there is more in the pipe
        int i = 0;
        int j = 0;
        for (i=0;i<sizeof(buffer);i++){
            if (serial_readable(&sobj))
            {
                gateway_diag_task_state(GW_DIAG_ROLE_TX, GW_DIAG_STATE_UART_SEM);
                rtw_down_sema(&uart_tx_rx_sema);
                gateway_diag_task_state(GW_DIAG_ROLE_TX, GW_DIAG_STATE_UART_READ);
                buffer[j++] = serial_getc(&sobj);
                gateway_diag_task_progress(GW_DIAG_ROLE_TX);
                rtw_up_sema(&uart_tx_rx_sema);
            }
        }

        gateway_diag_task_state(GW_DIAG_ROLE_TX, GW_DIAG_STATE_IDLE);
        if (j>0)
        {
            gateway_diag_add(GW_DIAG_UART_RX_BYTES, (unsigned long)j);
#ifdef Z_UART_DEBUG
            printf("%d bytes UART->TCP: ", j);
            int i = 0;
            for (i = 0; i < j ; i++)
            {
                printf("0x%x ",buffer[i]);
            }
            printf("\n");
#endif
            if (j<sizeof(buffer)){
                gateway_diag_task_state(GW_DIAG_ROLE_TX, GW_DIAG_STATE_TCP_SEM);
                rtw_down_sema(&tcp_tx_rx_sema);
                gateway_diag_task_state(GW_DIAG_ROLE_TX, GW_DIAG_STATE_TCP_SEND);
                ret = send(client_fd, buffer, j, 0);
                if (ret > 0)
                    gateway_diag_task_progress(GW_DIAG_ROLE_TX);
                rtw_up_sema(&tcp_tx_rx_sema);
                gateway_diag_task_state(GW_DIAG_ROLE_TX, GW_DIAG_STATE_IDLE);
                if (ret > 0) {
                    gateway_diag_add(GW_DIAG_TCP_TX_BYTES, (unsigned long)ret);
                    if (ret < j)
                        gateway_diag_add(GW_DIAG_TCP_SHORT_SENDS, 1);
                } else {
                    gateway_diag_add(GW_DIAG_TCP_SEND_ERRORS, 1);
                    exit_reason = 2;
                }
            }
            else
            {
                gateway_diag_add(GW_DIAG_UART_BUFFER_DROPS, (unsigned long)j);
                exit_reason = 3;
                printf("buff corrupted");
            }
        }
        else
        {
            if (rx_exit){
                exit_reason = 1;
                goto exit;
            }
            continue; // avoid quit
        }

		if(ret <= 0)
			goto exit;

		// vTaskDelay(100);
	}

exit:
	gpio_write(&led_blue, 0); // turn off blue LED
	printf("\n%s exit\n", __FUNCTION__);
	gateway_diag_event(GW_DIAG_TX_EXIT, exit_reason);
	gateway_diag_task_stop(GW_DIAG_ROLE_TX);
	tx_exit = 1;
	vTaskDelete(NULL);
}

static void rx_thread(void *param)
{
	int client_fd = * (int *) param;
	unsigned long exit_reason = 0;
	unsigned char buffer[1024];
	gateway_diag_task_start(GW_DIAG_ROLE_RX);
	gpio_write(&led_blue, 1); // turn on blue LED
	printf("\n%s start\n", __FUNCTION__);

	while(1) {
		int ret = 0, sock_err = 0;
		size_t err_len = sizeof(sock_err);

        gateway_diag_task_heartbeat(GW_DIAG_ROLE_RX);
        gateway_diag_task_state(GW_DIAG_ROLE_RX, GW_DIAG_STATE_IDLE);
        gateway_diag_task_state(GW_DIAG_ROLE_RX, GW_DIAG_STATE_TCP_SEM);
		rtw_down_sema(&tcp_tx_rx_sema);
		gateway_diag_task_state(GW_DIAG_ROLE_RX, GW_DIAG_STATE_TCP_RECV);
		ret = recv(client_fd, buffer, sizeof(buffer), MSG_DONTWAIT);
        if (ret > 0)
            gateway_diag_task_progress(GW_DIAG_ROLE_RX);
        gateway_diag_task_state(GW_DIAG_ROLE_RX, GW_DIAG_STATE_TCPIP);
		getsockopt(client_fd, SOL_SOCKET, SO_ERROR, &sock_err, &err_len);
		rtw_up_sema(&tcp_tx_rx_sema);
        gateway_diag_task_state(GW_DIAG_ROLE_RX, GW_DIAG_STATE_IDLE);
        if (ret > 0){
            gateway_diag_add(GW_DIAG_TCP_RX_BYTES, (unsigned long)ret);
#ifdef Z_UART_DEBUG
            printf("%d bytes TCP->UART: ", ret);
            int i=0;
            for (i=0;i<ret;i++)
            {
                printf("0x%x ",buffer[i]);
            }
            printf("\n");
#endif
            uart_send_data( &sobj, buffer, ret);
        }

		// ret == -1 and socket error == EAGAIN when no data received for nonblocking
		if((ret == -1) && ((sock_err == EAGAIN)
#if LWIP_VERSION_MAJOR >= 2
			||(sock_err == 0)
#endif
		))
			continue;
		else if(ret <= 0) {
			exit_reason = ret == 0 ? 1 : 2;
			if (ret < 0)
				gateway_diag_add(GW_DIAG_TCP_RECV_ERRORS, 1);
			goto exit;
		}

		gateway_diag_task_state(GW_DIAG_ROLE_RX, GW_DIAG_STATE_DELAY);
		vTaskDelay(10);
	}

exit:
	gpio_write(&led_blue, 0); // turn off blue LED
	printf("\n%s exit\n", __FUNCTION__);
	gateway_diag_event(GW_DIAG_RX_EXIT, exit_reason);
	gateway_diag_task_stop(GW_DIAG_ROLE_RX);
	rx_exit = 1;
	vTaskDelete(NULL);
}

static void example_socket_tcp_trx_thread(void *param)
{
	int server_fd = -1, client_fd = -1;
	struct sockaddr_in server_addr, client_addr;
	socklen_t client_addr_size;

	gateway_diag_task_start(GW_DIAG_ROLE_BRIDGE);
	gateway_diag_event(GW_DIAG_BRIDGE_START, SERVER_PORT);

	// Delay to wait for IP by DHCP
	int counter = 0;

	// Init blue LED
	gpio_init(&led_blue, LED_BLUE);
	gpio_dir(&led_blue, PIN_OUTPUT);    // Direction: Output
	gpio_mode(&led_blue, PullNone);     // No pull

	while (counter < 10) {
        gateway_diag_task_heartbeat(GW_DIAG_ROLE_BRIDGE);
        gateway_diag_task_state(GW_DIAG_ROLE_BRIDGE, GW_DIAG_STATE_IDLE);
		if (counter % 2 == 0) {
			gpio_write(&led_blue, 1); // turn on blue LED
		} else {
			gpio_write(&led_blue, 0); // turn off blue LED
		}
		counter++;
		gateway_diag_task_state(GW_DIAG_ROLE_BRIDGE, GW_DIAG_STATE_DELAY);
		vTaskDelay(500);
	}

	printf("\nExample: socket tx/rx 1\n");
	// gpio_write(&led_blue, 1); // uncomment to turn on blue LED permanently

	gateway_diag_task_state(GW_DIAG_ROLE_BRIDGE, GW_DIAG_STATE_NETWORK_WAIT);
	while (wifi_is_ready_to_transceive(RTW_STA_INTERFACE) != RTW_SUCCESS ||
	       ip_addr_get_ip4_u32(netif_ip_addr4(&xnetif[0])) == 0) {
		gateway_diag_task_heartbeat(GW_DIAG_ROLE_BRIDGE);
		vTaskDelay(pdMS_TO_TICKS(100));
	}
	gateway_diag_task_state(GW_DIAG_ROLE_BRIDGE, GW_DIAG_STATE_TCPIP);
	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd < 0) {
		gateway_diag_event(GW_DIAG_BRIDGE_ERROR, 1);
		goto exit;
	}
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(SERVER_PORT);
	/* Keep the Zigbee bridge on the station LAN; the setup AP owns its
	 * separate HTTP listener on the AP interface. */
	server_addr.sin_addr.s_addr = ip_addr_get_ip4_u32(netif_ip_addr4(&xnetif[0]));

	if(bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) != 0) {
		gateway_diag_event(GW_DIAG_BRIDGE_ERROR, 2);
		printf("ERROR: bind\n");
		goto exit;
	}

	if(listen(server_fd, LISTEN_QLEN) != 0) {
		gateway_diag_event(GW_DIAG_BRIDGE_ERROR, 3);
		printf("ERROR: listen\n");
		goto exit;
	}

	gateway_diag_event(GW_DIAG_BRIDGE_LISTEN, SERVER_PORT);
	while(1) {
		fd_set readable;
		struct timeval timeout = { .tv_sec = 0, .tv_usec = 200000 };
		int selected;

		gateway_diag_task_heartbeat(GW_DIAG_ROLE_BRIDGE);
		if (client_fd >= 0 && tx_exit && rx_exit) {
			gateway_diag_task_state(GW_DIAG_ROLE_BRIDGE, GW_DIAG_STATE_TCPIP);
			close(client_fd);
			client_fd = -1;
			rtw_free_sema(&tcp_tx_rx_sema);
			gateway_diag_add(GW_DIAG_BRIDGE_DISCONNECTS, 1);
			gateway_diag_event(GW_DIAG_BRIDGE_DISCONNECT, 0);
		}
		FD_ZERO(&readable);
		FD_SET(server_fd, &readable);
		gateway_diag_task_state(GW_DIAG_ROLE_BRIDGE,
		                        client_fd >= 0 ? GW_DIAG_STATE_WAIT_TASKS : GW_DIAG_STATE_ACCEPT);
		selected = select(server_fd + 1, &readable, NULL, NULL, &timeout);
		if (selected <= 0)
			continue;

		client_addr_size = sizeof(client_addr);
		int incoming_fd = accept(server_fd, (struct sockaddr *) &client_addr, &client_addr_size);
		if (incoming_fd < 0)
			continue;
		if (client_fd >= 0) {
			close(incoming_fd);
			continue;
		}

		client_fd = incoming_fd;
		gateway_diag_add(GW_DIAG_BRIDGE_CONNECTIONS, 1);
		gateway_diag_event(GW_DIAG_BRIDGE_CONNECT, 0);
		rtw_init_sema(&tcp_tx_rx_sema, 1);
		tx_exit = 0;
		rx_exit = 0;

		if(xTaskCreate(tx_thread, ((const char*)"tx_thread"), 512, &client_fd,
		               tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
			gateway_diag_event(GW_DIAG_TASK_ERROR, 1);
			tx_exit = 1;
		}
		if(xTaskCreate(rx_thread, ((const char*)"rx_thread"), 512, &client_fd,
		               tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
			gateway_diag_event(GW_DIAG_TASK_ERROR, 2);
			rx_exit = 1;
		}
	}

exit:
	gateway_diag_task_state(GW_DIAG_ROLE_BRIDGE, GW_DIAG_STATE_TCPIP);
	close(server_fd);
	gateway_diag_task_stop(GW_DIAG_ROLE_BRIDGE);
	vTaskDelete(NULL);
}

void example_socket_tcp_trx_1(void)
{
	gateway_diag_start();
	sobj.uart_idx = 0;

    // mbed uart test
    rtw_init_sema(&uart_tx_rx_sema, 1);
    serial_init(&sobj,UART_TX,UART_RX);
    serial_baud(&sobj,115200);
    serial_format(&sobj, 8, ParityNone, 1);
	serial_rx_fifo_level(&sobj, FifoLvHalf);
	serial_set_flow_control(&sobj, FlowControlRTSCTS, UART_RTS, UART_CTS);    // Pin assignment is ignored
	gateway_diag_boot_event(GW_DIAG_UART_READY, 115200);
	if(xTaskCreate(example_socket_tcp_trx_thread, ((const char*)"example_socket_tcp_trx_thread"), 1024, NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
		gateway_diag_boot_event(GW_DIAG_TASK_ERROR, 3);
		printf("\n\r%s xTaskCreate(example_socket_tcp_trx_thread) failed", __FUNCTION__);
	}
}