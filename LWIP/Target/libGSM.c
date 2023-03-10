#include "libGSM.h"
#include <stdio.h>
#include "cmsis_os.h"
#include "netif/ppp/pppos.h"
#include "netif/ppp/ppp.h"
#include "main.h"
#include <stdlib.h>
#include <memory.h>
#include <string.h>

#define GSM_DEBUG 1
#define GSM_OK_Str "OK"

#define CONFIG_GSM_APN "m-wap" //mobifone
// #ifdef SIM_VIETTEL 
// #define CONFIG_GSM_APN "v-internet" //viettel
// #else 
// #define CONFIG_GSM_APN "m-wap" //mobifone
// #endif

#define PPPOSMUTEX_TIMEOUT 1000
#define PPPOS_CLIENT_STACK_SIZE 1024*3
#define BUF_SIZE (1024)
#define PPP_User ""
#define PPP_Pass ""

typedef struct
{
    char *cmd;
    uint16_t cmdSize;
    char *cmdResponseOnOk;
    uint16_t timeoutMs;
    uint16_t delayMs;
    uint8_t skip;
} GSM_Cmd;

static GSM_Cmd cmd_AT =
    {
        .cmd = "AT\r\n",
        .cmdSize = sizeof("AT\r\n") - 1,
        .cmdResponseOnOk = GSM_OK_Str,
        .timeoutMs = 1111,
        .delayMs = 110,
        .skip = 0,
};
static GSM_Cmd cmd_modulereset =
    {
        .cmd = "AT+CRESET\r\n",
        .cmdSize = sizeof("AT+CRESET\r\n") - 1,
        .cmdResponseOnOk = GSM_OK_Str,
        .timeoutMs = 1111,
        .delayMs = 31000,
        .skip = 0,
};
static GSM_Cmd cmd_OFFRf =
    {
        .cmd = "AT+CFUN=0\r\n",
        .cmdSize = sizeof("AT+CFUN=0\r\n") - 1,
        .cmdResponseOnOk = GSM_OK_Str,
        .timeoutMs = 10000,
        .delayMs = 1111,
        .skip = 0,
};

static GSM_Cmd cmd_NoSMSInd =
    {
        .cmd = "AT+CNMI=0,0,0,0,0\r\n",
        .cmdSize = sizeof("AT+CNMI=0,0,0,0,0\r\n") - 1,
        .cmdResponseOnOk = "",//GSM_OK_Str,
        .timeoutMs = 1000,
        .delayMs = 1000,
        .skip = 0,
};

static GSM_Cmd cmd_select_Nw =
    {
        .cmd = "AT+COPS=0\r\n",
        .cmdSize = sizeof("AT+COPS=0\r\n") -1 ,
        .cmdResponseOnOk = GSM_OK_Str,
        .timeoutMs = 1000,
        .delayMs = 1000,
        .skip = 0,
};

static GSM_Cmd cmd_Reset =
    {
        .cmd = "ATZ\r\n",
        .cmdSize = sizeof("ATZ\r\n") - 1,
        .cmdResponseOnOk = GSM_OK_Str,
        .timeoutMs = 300,
        .delayMs = 0,
        .skip = 0,
};

static GSM_Cmd cmd_RFOn =
    {
        .cmd = "AT+CFUN=1\r\n",
        .cmdSize = sizeof("AT+CFUN=1\r\n") -1,
        .cmdResponseOnOk = GSM_OK_Str,
        .timeoutMs = 1000,
        .delayMs = 1000,
        .skip = 0,
};

static GSM_Cmd cmd_EchoOff =
    {
        .cmd = "ATE0\r\n",
        .cmdSize = sizeof("ATE0\r\n") - 1,
        .cmdResponseOnOk = GSM_OK_Str,
        .timeoutMs = 555,
        .delayMs = 110,
        .skip = 0,
};

static GSM_Cmd cmd_Pin =
    {
        .cmd = "AT+CPIN?\r\n",
        .cmdSize = sizeof("AT+CPIN?\r\n") - 1,
        .cmdResponseOnOk = "+CPIN: READY",
        .timeoutMs = 5000,
        .delayMs = 1100,
        .skip = 0,
};

static GSM_Cmd cmd_Reg =
    {
        .cmd = "AT+CREG?\r\n",
        .cmdSize = sizeof("AT+CREG?\r\n") - 1,
        .cmdResponseOnOk = "",//"CREG: 0",
        .timeoutMs = 300,
        .delayMs = 200,
        .skip = 0,
};

static GSM_Cmd cmd_Signal = 
{
        .cmd = "AT+CSQ\r\n",
        .cmdSize = sizeof("AT+CSQ\r\n"),
        .cmdResponseOnOk = "CSQ:",
        .timeoutMs = 8000,
        .delayMs = 2000,
        .skip = 0,
};

static GSM_Cmd cmd_APN =
    {
        .cmd = NULL,
        .cmdSize = 0,
        .cmdResponseOnOk = GSM_OK_Str,//"", //GSM_OK_Str,
        .timeoutMs = 2000,
        .delayMs = 500,
        .skip = 0,
};

static GSM_Cmd cmd_Connect =
    {
        //.cmd = "AT+CGDATA=\"PPP\",1\r\n",
        //.cmdSize = sizeof("AT+CGDATA=\"PPP\",1\r\n") - 1,
        //.cmd = "ATDT*99***1#\r\n",
        //.cmdSize = sizeof("ATDT*99***1#\r\n")-1,
        .cmd = "ATD*99#\r\n",
        .cmdSize = sizeof("ATD*99#\r\n")-1,
        .cmdResponseOnOk = "CONNECT",
        .timeoutMs = 30000,
        .delayMs = 1000,
        .skip = 0,
};


static GSM_Cmd *GSM_Init[] =
    {
        // &cmd_modulereset,
        &cmd_AT,
        &cmd_OFFRf,
        &cmd_Pin,
        &cmd_Reset,
        &cmd_EchoOff,
        &cmd_RFOn,
        // &cmd_select_Nw,
       &cmd_NoSMSInd,
        &cmd_Reg,
        // &cmd_Signal,
        &cmd_APN,
        &cmd_Connect,
};

#define GSM_InitCmdsSize (sizeof(GSM_Init) / sizeof(GSM_Cmd *))

UART_HandleTypeDef huart2;
static void MX_USART2_UART_Init(void);
osThreadId pppThreadHandle;
static osSemaphoreId pppos_mutex = NULL;
static int do_pppos_connect = 1;
static uint8_t pppos_task_started = 0;
static uint8_t gsm_status = GSM_STATE_FIRSTINIT;

static ppp_pcb *ppp = NULL;
struct netif ppp_netif;

static uint32_t pppos_rx_count;
static uint32_t pppos_tx_count;
static uint8_t gsm_rfOff = 0;

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

    /* USER CODE BEGIN USART2_Init 0 */

    /* USER CODE END USART2_Init 0 */

    /* USER CODE BEGIN USART2_Init 1 */

    /* USER CODE END USART2_Init 1 */
    huart2.Instance = USART2;
    huart2.Init.BaudRate = 115200;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart2) != HAL_OK)
    {
        Error_Handler();
    }
    /* USER CODE BEGIN USART2_Init 2 */

    printf("UART2 init done\r\n");
    /* USER CODE END USART2_Init 2 */
}

int UART_Read(char *dataToRead, int size, int timeout)
{
    int count = 0;
    while (timeout--)
    {
        uint8_t someByte = 0;
        if (HAL_OK == HAL_UART_Receive(&huart2, &someByte, 1, 1))
        {
            dataToRead[count] = someByte;
            count++;
        }
    }
    return count;
}

static void enableAllInitCmd()
{
	for (int idx = 0; idx < GSM_InitCmdsSize; idx++) {
		GSM_Init[idx]->skip = 0;
	}
}

static void infoCommand(char *cmd, int cmdSize, char *info)
{
    char buf[cmdSize + 2];
    memset(buf, 0, cmdSize + 2);

    for (int i = 0; i < cmdSize; i++)
    {
        if ((cmd[i] != 0x00) && ((cmd[i] < 0x20) || (cmd[i] > 0x7F)))
            buf[i] = '.';
        else
            buf[i] = cmd[i];
        if (buf[i] == '\0')
            break;
    }
    printf("GSM: %s [%s]\r\n", info, buf);
}

static int atCmd_waitResponse(char *cmd, char *resp, char *resp1, int cmdSize, int timeout, char **response, int size)
{
    char sresp[256] = {'\0'};
    char data[256] = {'\0'};
    int len, res = 1, idx = 0, tot = 0, timeoutCnt = 0;

    // ** Send command to GSM
    osDelay(100);
    if (cmd != NULL)
    {
        if (cmdSize == -1)
            cmdSize = strlen(cmd);
#if GSM_DEBUG
        infoCommand(cmd, cmdSize, "AT COMMAND:");
#endif
        HAL_UART_Transmit(&huart2, (uint8_t*)cmd, cmdSize, 100);
    }

    if (response != NULL)
    {
        // Read GSM response into buffer
        char *pbuf = *response;
        len = UART_Read(data, 256, timeout);
        while (len > 0)
        {
            if ((tot + len) >= size)
            {
                char *ptemp = realloc(pbuf, size + 512);
                if (ptemp == NULL)
                    return 0;
                size += 512;
                pbuf = ptemp;
            }
            memcpy(pbuf + tot, data, len);
            tot += len;
            response[tot] = '\0';
            len = UART_Read(data, 256, timeout);
        }
        *response = pbuf;
        return tot;
    }

    // ** Wait for and check the response
    idx = 0;
    while (1)
    {
        memset(data, 0, 256);
        len = 0;
        len = UART_Read(data, 256, 10);
        if (len > 0)
        {
            for (int i = 0; i < len; i++)
            {
                if (idx < 256)
                {
                    if ((data[i] >= 0x20) && (data[i] < 0x80))
                        sresp[idx++] = data[i];
                    else
                        sresp[idx++] = 0x2e;
                }
            }
            tot += len;
        }
        else
        {
            if (tot > 0)
            {
                // Check the response
                if (strstr(sresp, resp) != NULL)
                {
#if GSM_DEBUG
                    printf("GSM: AT RESPONSE: [%s]\r\n", sresp);
#endif
                    break;
                }
                else
                {
                    if (resp1 != NULL)
                    {
                        if (strstr(sresp, resp1) != NULL)
                        {
#if GSM_DEBUG
                            printf("GSM: AT RESPONSE (1): [%s]\r\n", sresp);
#endif
                            res = 2;
                            break;
                        }
                    }
// no match
#if GSM_DEBUG
                    printf("GSM: AT BAD RESPONSE: [%s]\r\n", sresp);
#endif
                    res = 0;
                    break;
                }
            }
        }

        timeoutCnt += 10;
        if (timeoutCnt > timeout)
        {
// timeout
#if GSM_DEBUG
            printf("GSM: AT: TIMEOUT\r\n");
#endif
            // HAL_NVIC_SystemReset();
            res = 0;
            break;
        }
    }

    return res;
}

int LTE_ppposInit()
{
    printf("GSM: LTE_ppposInit\r\n");

    MX_USART2_UART_Init();

    if (pppos_mutex != NULL) osSemaphoreWait(pppos_mutex, PPPOSMUTEX_TIMEOUT);
	do_pppos_connect = 1;
	int gstat = 0;
	int task_s = pppos_task_started;
	if (pppos_mutex != NULL) osSemaphoreRelease(pppos_mutex);

	if (task_s == 0) 
    {
		if (pppos_mutex == NULL) 
        {
            osSemaphoreDef(pposSem);
            pppos_mutex = osSemaphoreCreate(osSemaphore(pposSem), 1);
        }
		if (pppos_mutex == NULL) 
            return 0;


        //tcpip_init( NULL, NULL );
        // LWIP must be initialized before this.

        osThreadDef(mythread, PPPosClientThread, osPriorityNormal, 0, 1024);    
        pppThreadHandle = osThreadCreate(osThread(mythread), NULL);

		while (task_s == 0) 
        {
			osDelay(10);
			osSemaphoreWait(pppos_mutex, PPPOSMUTEX_TIMEOUT);
			task_s = pppos_task_started;
			osSemaphoreRelease(pppos_mutex);
		}
	}

	while (gstat != 1) 
    {
		osDelay(10);
		osSemaphoreWait(pppos_mutex, PPPOSMUTEX_TIMEOUT);
		gstat = gsm_status;
		task_s = pppos_task_started;
		osSemaphoreRelease(pppos_mutex);
		if (task_s == 0) 
        return 0;
	}

    printf("GSM: LTE Init done\r\n");
	return 1;
}
// extern ip4_addr_t ipaddr;
// extern ip4_addr_t netmask;
// extern ip4_addr_t gw;
// struct netif gnetif;


static void ppp_status_cb(ppp_pcb *pcb, int err_code, void *ctx)
{
	struct netif *pppif = ppp_netif(pcb);
	LWIP_UNUSED_ARG(ctx);

    printf("GSM: ppp_status_cb: %d\r\n", err_code);

	switch(err_code) 
    {
		case PPPERR_NONE: 
        {
			#if GSM_DEBUG
			printf("GSM: status_cb: Connected\r\n");
			#if PPP_IPV4_SUPPORT
			printf("GSM:    ipaddr    = %s\r\n", ipaddr_ntoa(&pppif->ip_addr));
			printf("GSM:    gateway   = %s\r\n", ipaddr_ntoa(&pppif->gw));
			printf("GSM:    netmask   = %s\r\n", ipaddr_ntoa(&pppif->netmask));
			#endif
            // ipaddr.addr  = &pppif->ip_addr;
            // netmask.addr = &pppif->netmask ;
            // gw.addr      =  &pppif->gw;
            // // IP4_ADDR(&ipaddr, 192, 168, 1, 10);
            // netif_add(&gnetif, &ipaddr, &netmask, &gw, NULL, &ethernetif_init, &tcpip_input);

            // IP4_ADDR(&netmask, 255, 255, 255, 0);
			#if PPP_IPV6_SUPPORT
			printf("GSM: ip6addr   = %s", ip6addr_ntoa(netif_ip6_addr(pppif, 0)));
			#endif
			#endif
			osSemaphoreWait(pppos_mutex, PPPOSMUTEX_TIMEOUT);
			gsm_status = GSM_STATE_CONNECTED;
			osSemaphoreRelease(pppos_mutex);
			break;
		}
		case PPPERR_PARAM: 
        {
			#if GSM_DEBUG
			printf("GSM: status_cb: Invalid parameter\r\n");
			#endif
			break;
		}
		case PPPERR_OPEN: 
        {
			#if GSM_DEBUG
			printf("GSM: status_cb: Unable to open PPP session\r\n");
			#endif
			break;
		}
		case PPPERR_DEVICE: 
        {
			#if GSM_DEBUG
			printf("GSM: status_cb: Invalid I/O device for PPP\r\n");
			#endif
			break;
		}
		case PPPERR_ALLOC: 
        {
			#if GSM_DEBUG
			printf("GSM: status_cb: Unable to allocate resources\r\n");
			#endif
			break;
		}
		case PPPERR_USER: 
        {
			/* ppp_free(); -- can be called here */
			#if GSM_DEBUG
			printf("GSM: status_cb: User interrupt (disconnected)\r\n");
			#endif
			osSemaphoreWait(pppos_mutex, PPPOSMUTEX_TIMEOUT);
			gsm_status = GSM_STATE_DISCONNECTED;
			osSemaphoreRelease(pppos_mutex);
			break;
		}
		case PPPERR_CONNECT: 
        {
			#if GSM_DEBUG
			printf("GSM: status_cb: Connection lost\r\n");
			#endif
			osSemaphoreWait(pppos_mutex, PPPOSMUTEX_TIMEOUT);
			gsm_status = GSM_STATE_DISCONNECTED;
			osSemaphoreRelease(pppos_mutex);
			break;
		}
		case PPPERR_AUTHFAIL: 
        {
			#if GSM_DEBUG
			printf("GSM: status_cb: Failed authentication challenge\r\n");
			#endif
			break;
		}
		case PPPERR_PROTOCOL: 
        {
			#if GSM_DEBUG
			printf("GSM: status_cb: Failed to meet protocol\r\n");
			#endif
			break;
		}
		case PPPERR_PEERDEAD: 
        {
			#if GSM_DEBUG
			printf("GSM: status_cb: Connection timeout\r\n");
			#endif
			break;
		}
		case PPPERR_IDLETIMEOUT: 
        {
			#if GSM_DEBUG
			printf("GSM: status_cb: Idle Timeout\r\n");
			#endif
			break;
		}
		case PPPERR_CONNECTTIME: 
        {
			#if GSM_DEBUG
			printf("GSM: status_cb: Max connect time reached\r\n");
			#endif
			break;
		}
		case PPPERR_LOOPBACK: 
        {
			#if GSM_DEBUG
			printf("GSM: status_cb: Loopback detected\r\n");
			#endif
			break;
		}
		default: 
        {
			#if GSM_DEBUG
			printf("GSM: status_cb: Unknown error code %d\r\n", err_code);
			#endif
			break;
		}
	}
}

static uint32_t ppp_output_callback(ppp_pcb *pcb, uint8_t *data, uint32_t len, void *ctx)
{
    HAL_UART_Transmit(&huart2, data, len, 10);
    return len;
}

//------------------------------------
static void _disconnect(uint8_t rfOff)
{
	int res = atCmd_waitResponse("AT\r\n", GSM_OK_Str, NULL, 4, 1000, NULL, 0);
	if (res == 1) 
    {
		if (rfOff) 
        {
			cmd_Reg.timeoutMs = 10000;
			res = atCmd_waitResponse("AT+CFUN=4\r\n", GSM_OK_Str, NULL, 11, 10000, NULL, 0); // disable RF function
		}
		return;
	}

	#if GSM_DEBUG
	printf("GSM: ONLINE, DISCONNECTING...\r\n");
	#endif
	osDelay(1000);
    HAL_UART_Transmit(&huart2, (uint8_t*)"+++", 3, 10);
	osDelay(1100);

	int n = 0;
	res = atCmd_waitResponse("ATH\r\n", GSM_OK_Str, "NO CARRIER", 5, 3000, NULL, 0);
	while (res == 0) 
    {
		n++;
		if (n > 10) 
        {
			#if GSM_DEBUG
			printf("GSM: STILL CONNECTED.\r\n");
			#endif
			n = 0;
			osDelay(1000);
            HAL_UART_Transmit(&huart2, (uint8_t*)"+++", 3, 10);
			osDelay(1000);
		}
		osDelay(100);
		res = atCmd_waitResponse("ATH\r\n", GSM_OK_Str, "NO CARRIER", 5, 3000, NULL, 0);
	}
	osDelay(100);
	if (rfOff) 
    {
		cmd_Reg.timeoutMs = 10000;
		res = atCmd_waitResponse("AT+CFUN=4\r\n", GSM_OK_Str, NULL, 11, 3000, NULL, 0);
	}
	#if GSM_DEBUG
	printf("GSM: DISCONNECTED.\r\n");
	#endif
}

void PPPosClientThread()
{
    osSemaphoreWait(pppos_mutex, PPPOSMUTEX_TIMEOUT);
	pppos_task_started = 1;
	osSemaphoreRelease(pppos_mutex);

    int gsmCmdIter = 0;
    int nfail = 0;
    printf("GSM: Starting Modem thread\r\n");

    char* data = (char*) malloc(BUF_SIZE);

    char PPP_ApnATReq[sizeof(CONFIG_GSM_APN) + 30];
    int s=0;
    s=sprintf(PPP_ApnATReq, "AT+CGDCONT=1,\"IP\",\"%s\",\"\",0,0\r\n", CONFIG_GSM_APN);
    // int s2= strlen("AT+CGDCONT=1,\"IP\",\"m-wap\"");
    // AT+CGDCONT=1,"IP","m-wap","",0,0
    // AT+CGDCONT=1,"IP","m-wap",\"\",0,0
    // printf("sprintf len =%u, strlen  = %u",s,s2);
    cmd_APN.cmd = PPP_ApnATReq;
    cmd_APN.cmdSize = strlen(PPP_ApnATReq) ;


    _disconnect(1); // Disconnect if connected

	osSemaphoreWait(pppos_mutex, PPPOSMUTEX_TIMEOUT);
    pppos_tx_count = 0;
    pppos_rx_count = 0;
	gsm_status = GSM_STATE_FIRSTINIT;
	osSemaphoreRelease(pppos_mutex);

	enableAllInitCmd();
    
    while (1)
    {
        while (gsmCmdIter < GSM_InitCmdsSize)
        {
            if (GSM_Init[gsmCmdIter]->skip)
            {
                #if GSM_DEBUG
                infoCommand(GSM_Init[gsmCmdIter]->cmd, GSM_Init[gsmCmdIter]->cmdSize, "Skip command:");
                #endif
                gsmCmdIter++;
                continue;
            }
            if (atCmd_waitResponse(GSM_Init[gsmCmdIter]->cmd,
                                   GSM_Init[gsmCmdIter]->cmdResponseOnOk, NULL,
                                   GSM_Init[gsmCmdIter]->cmdSize,
                                   GSM_Init[gsmCmdIter]->timeoutMs, NULL, 0) == 0)
            {
                // * No response or not as expected, start from first initialization command
                #if GSM_DEBUG
                printf("GSM: Wrong response, restarting...\r\n");
                #endif

                nfail++;
                if (nfail > 20)
                    goto exit;

                osDelay(3000);
                gsmCmdIter = 0;
                continue;
            }

            if (GSM_Init[gsmCmdIter]->delayMs > 0)
                osDelay(GSM_Init[gsmCmdIter]->delayMs);
            GSM_Init[gsmCmdIter]->skip = 1;
            if (GSM_Init[gsmCmdIter] == &cmd_Reg)
                GSM_Init[gsmCmdIter]->delayMs = 0;
            // Next command
            gsmCmdIter++;
        }

        #if GSM_DEBUG
		printf("GSM: GSM initialized.\r\n");
		#endif

        osSemaphoreWait(pppos_mutex, PPPOSMUTEX_TIMEOUT);
		if(gsm_status == GSM_STATE_FIRSTINIT)
        {
			osSemaphoreRelease(pppos_mutex);

			// ** After first successful initialization create PPP control block
			ppp = pppos_create(&ppp_netif, ppp_output_callback, ppp_status_cb, NULL);

			if (ppp == NULL) 
            {
				#if GSM_DEBUG
				printf("GSM: Error initializing PPPoS\r\n");
				#endif
				break; // end task
			}
		}
		else
            osSemaphoreRelease(pppos_mutex);

		//ppp_set_default(ppp);
        netif_set_default(&ppp_netif);
        //ppp_set_auth(ppp, PPPAUTHTYPE_ANY, "", "");

		osSemaphoreWait(pppos_mutex, PPPOSMUTEX_TIMEOUT);
		gsm_status = GSM_STATE_IDLE;
		osSemaphoreRelease(pppos_mutex);
		ppp_connect(ppp, 0);   

        while(1) 
        {
			// === Check if disconnect requested ===
			osSemaphoreWait(pppos_mutex, PPPOSMUTEX_TIMEOUT);
			if (do_pppos_connect <= 0) 
            {
				int end_task = do_pppos_connect;
				do_pppos_connect = 1;
				osSemaphoreRelease(pppos_mutex);
				#if GSM_DEBUG
				printf("\r\n");
				printf("GSM: Disconnect requested.\r\n");
				#endif

				ppp_close(ppp, 0);
				int gstat = 1;
				while (gsm_status != GSM_STATE_DISCONNECTED) 
                {
					// Handle data received from GSM
					memset(data, 0, BUF_SIZE);
                    int len = UART_Read(data, BUF_SIZE, 30);
					if (len > 0)	
                    {
						pppos_input_tcpip(ppp, (u8_t*)data, len);
						osSemaphoreWait(pppos_mutex, PPPOSMUTEX_TIMEOUT);
					    pppos_tx_count += len;
						osSemaphoreRelease(pppos_mutex);
					}
					osSemaphoreWait(pppos_mutex, PPPOSMUTEX_TIMEOUT);
					gstat = gsm_status;
					osSemaphoreRelease(pppos_mutex);
				}
				osDelay(1000);

				osSemaphoreWait(pppos_mutex, PPPOSMUTEX_TIMEOUT);
				uint8_t rfoff = gsm_rfOff;
				osSemaphoreRelease(pppos_mutex);
				_disconnect(rfoff); // Disconnect GSM if still connected

				#if GSM_DEBUG
				printf("GSM: Disconnected.\r\n");
				#endif

				gsmCmdIter = 0;
				enableAllInitCmd();
				osSemaphoreWait(pppos_mutex, PPPOSMUTEX_TIMEOUT);
				gsm_status = GSM_STATE_IDLE;
				do_pppos_connect = 0;
				osSemaphoreRelease(pppos_mutex);

				if (end_task < 0) goto exit;

				// === Wait for reconnect request ===
				gstat = 0;
				while (gstat == 0) 
                {
					osDelay(100);
					osSemaphoreWait(pppos_mutex, PPPOSMUTEX_TIMEOUT);
					gstat = do_pppos_connect;
					osSemaphoreRelease(pppos_mutex);
				}
				#if GSM_DEBUG
				printf("\r\n");
				printf("GSM: Reconnect requested.\r\n");
				#endif
				break;
			}

			// === Check if disconnected ===
			if (gsm_status == GSM_STATE_DISCONNECTED) 
            {
				osSemaphoreRelease(pppos_mutex);
				#if GSM_DEBUG
				printf("\r\n");
				printf("GSM: Disconnected, trying again...\r\n");
				#endif
				ppp_close(ppp, 0);
                _disconnect(1);

				enableAllInitCmd();
				gsmCmdIter = 0;
				gsm_status = GSM_STATE_IDLE;
				osDelay(10000);
				break;
			}
			else
                osSemaphoreRelease(pppos_mutex);

			// === Handle data received from GSM ===
			memset(data, 0, BUF_SIZE);
            int len = UART_Read(data, BUF_SIZE, 50);
			if (len > 0)	
            {
                //printf("RX: %d\r\n", len);
				pppos_input_tcpip(ppp, (u8_t*)data, len);
				osSemaphoreWait(pppos_mutex, PPPOSMUTEX_TIMEOUT);
			    pppos_tx_count += len;
				osSemaphoreRelease(pppos_mutex);
			}

		}  // Handle GSM modem responses & disconnects loop
	}  // main task loop

exit:
	if (data) free(data);  // free data buffer
	if (ppp) ppp_free(ppp);

	osSemaphoreWait(pppos_mutex, PPPOSMUTEX_TIMEOUT);
	pppos_task_started = 0;
	gsm_status = GSM_STATE_FIRSTINIT;
	osSemaphoreRelease(pppos_mutex);
	#if GSM_DEBUG
	printf("GSM: PPPoS TASK TERMINATED\r\n");
	#endif
	//osThreadTerminate(mythread); 
    osDelay(5000);
    HAL_NVIC_SystemReset();

}