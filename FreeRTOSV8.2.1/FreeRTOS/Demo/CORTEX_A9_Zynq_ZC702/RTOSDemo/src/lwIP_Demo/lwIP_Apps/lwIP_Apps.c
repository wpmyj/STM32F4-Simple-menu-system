/*
    FreeRTOS V7.0.2 - Copyright (C) 2011 Real Time Engineers Ltd.
	

    ***************************************************************************
     *                                                                       *
     *    FreeRTOS tutorial books are available in pdf and paperback.        *
     *    Complete, revised, and edited pdf reference manuals are also       *
     *    available.                                                         *
     *                                                                       *
     *    Purchasing FreeRTOS documentation will not only help you, by       *
     *    ensuring you get running as quickly as possible and with an        *
     *    in-depth knowledge of how to use FreeRTOS, it will also help       *
     *    the FreeRTOS project to continue with its mission of providing     *
     *    professional grade, cross platform, de facto standard solutions    *
     *    for microcontrollers - completely free of charge!                  *
     *                                                                       *
     *    >>> See http://www.FreeRTOS.org/Documentation for details. <<<     *
     *                                                                       *
     *    Thank you for using FreeRTOS, and thank you for your support!      *
     *                                                                       *
    ***************************************************************************


    This file is part of the FreeRTOS distribution.

    FreeRTOS is free software; you can redistribute it and/or modify it under
    the terms of the GNU General Public License (version 2) as published by the
    Free Software Foundation AND MODIFIED BY the FreeRTOS exception.
    >>>NOTE<<< The modification to the GPL is included to allow you to
    distribute a combined work that includes FreeRTOS without being obliged to
    provide the source code for proprietary components outside of the FreeRTOS
    kernel.  FreeRTOS is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
    or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
    more details. You should have received a copy of the GNU General Public
    License and the FreeRTOS license exception along with FreeRTOS; if not it
    can be viewed here: http://www.freertos.org/a00114.html and also obtained
    by writing to Richard Barry, contact details for whom are available on the
    FreeRTOS WEB site.

    1 tab == 4 spaces!

    http://www.FreeRTOS.org - Documentation, latest information, license and
    contact details.

    http://www.SafeRTOS.com - A version that is certified for use in safety
    critical systems.

    http://www.OpenRTOS.com - Commercial support, development, porting,
    licensing and training services.
*/

/* Standard includes. */
#include <string.h>

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* lwIP core includes */
#include "lwip/opt.h"
#include "lwip/tcpip.h"
#include "lwip/inet.h"
#include "lwip/dhcp.h"

/* applications includes */
#include "apps/httpserver_raw_from_lwIP_download/httpd.h"

/* include the port-dependent configuration */
#include "lwipcfg_msvc.h"

/* Dimensions the cTxBuffer array - which is itself used to hold replies from 
command line commands.  cTxBuffer is a shared buffer, so protected by the 
xTxBufferMutex mutex. */
#define lwipappsTX_BUFFER_SIZE	1024

/* The maximum time to block waiting to obtain the xTxBufferMutex to become
available. */
#define lwipappsMAX_TIME_TO_WAIT_FOR_TX_BUFFER_MS	( 100 / portTICK_RATE_MS )

/* Definitions of the various SSI callback functions within the pccSSITags 
array.  If pccSSITags is updated, then these definitions must also be updated. */
#define ssiTASK_STATS_INDEX			0
#define ssiRUN_TIME_STATS_INDEX		1

/*-----------------------------------------------------------*/

/*
 * The function that implements the lwIP based sockets command interpreter
 * server.
 */
extern void vBasicSocketsCommandInterpreterTask( void *pvParameters );

/*
 * The SSI handler callback function passed to lwIP.
 */
static unsigned short uslwIPAppsSSIHandler( int iIndex, char *pcBuffer, int iBufferLength );

/*-----------------------------------------------------------*/

/* The SSI strings that are embedded in the served html files.  If this array
is changed, then the index position defined by the #defines such as 
ssiTASK_STATS_INDEX above must also be updated. */
static const char *pccSSITags[] = 
{
	"rtos_stats",
	"run_stats"
};

/* Semaphore used to guard the Tx buffer. */
static xSemaphoreHandle xTxBufferMutex = NULL;

/* The Tx buffer itself.  This is used to hold the text generated by the 
execution of command line commands, and (hopefully) the execution of 
server side include callbacks.  It is a shared buffer so protected by the
xTxBufferMutex mutex.  pcLwipAppsBlockingGetTxBuffer() and 
vLwipAppsReleaseTxBuffer() are provided to obtain and release the 
xTxBufferMutex respectively.  pcLwipAppsBlockingGetTxBuffer() must be used with
caution as it has the potential to block. */
static signed char cTxBuffer[ lwipappsTX_BUFFER_SIZE ];

/*-----------------------------------------------------------*/

void vStatusCallback( struct netif *pxNetIf )
{
char pcMessage[20];
struct in_addr* pxIPAddress;

	if( netif_is_up( pxNetIf ) != 0 )
	{
		strcpy( pcMessage, "IP=" );
		pxIPAddress = ( struct in_addr* ) &( pxNetIf->ip_addr );
		strcat( pcMessage, inet_ntoa( ( *pxIPAddress ) ) );
		xil_printf( pcMessage );
	}
	else
	{
		xil_printf( "Network is down" );
	}
}

/* Called from the TCP/IP thread. */
void lwIPAppsInit( void *pvArgument )
{
ip_addr_t xIPAddr, xNetMask, xGateway;
extern err_t xemacpsif_init( struct netif *netif );
extern void xemacif_input_thread( void *netif );
static struct netif xNetIf;

	( void ) pvArgument;

	/* Set up the network interface. */
	ip_addr_set_zero( &xGateway );
	ip_addr_set_zero( &xIPAddr );
	ip_addr_set_zero( &xNetMask );

	LWIP_PORT_INIT_GW(&xGateway);
	LWIP_PORT_INIT_IPADDR( &xIPAddr );
	LWIP_PORT_INIT_NETMASK(&xNetMask);

	/* Set mac address */
	xNetIf.hwaddr_len = 6;
	xNetIf.hwaddr[ 0 ] = configMAC_ADDR0;
	xNetIf.hwaddr[ 1 ] = configMAC_ADDR1;
	xNetIf.hwaddr[ 2 ] = configMAC_ADDR2;
	xNetIf.hwaddr[ 3 ] = configMAC_ADDR3;
	xNetIf.hwaddr[ 4 ] = configMAC_ADDR4;
	xNetIf.hwaddr[ 5 ] = configMAC_ADDR5;

	netif_set_default( netif_add( &xNetIf, &xIPAddr, &xNetMask, &xGateway, ( void * ) XPAR_XEMACPS_0_BASEADDR, xemacpsif_init, tcpip_input ) );
	netif_set_status_callback( &xNetIf, vStatusCallback );
	#if LWIP_DHCP
	{
		dhcp_start( &xNetIf );
	}
	#else
	{
		netif_set_up( &xNetIf );
	}
	#endif

	/* Install the server side include handler. */
	http_set_ssi_handler( uslwIPAppsSSIHandler, pccSSITags, sizeof( pccSSITags ) / sizeof( char * ) );

	/* Create the mutex used to ensure mutual exclusive access to the Tx 
	buffer. */
	xTxBufferMutex = xSemaphoreCreateMutex();
	configASSERT( xTxBufferMutex );

	/* Create the httpd server from the standard lwIP code.  This demonstrates
	use of the lwIP raw API. */
	httpd_init();

	sys_thread_new( "lwIP_In", xemacif_input_thread, &xNetIf, configMINIMAL_STACK_SIZE, configMAC_INPUT_TASK_PRIORITY );

	/* Create the FreeRTOS defined basic command server.  This demonstrates use
	of the lwIP sockets API. */
	xTaskCreate( vBasicSocketsCommandInterpreterTask, "CmdInt", configMINIMAL_STACK_SIZE * 5, NULL, configCLI_TASK_PRIORITY, NULL );
}
/*-----------------------------------------------------------*/

static unsigned short uslwIPAppsSSIHandler( int iIndex, char *pcBuffer, int iBufferLength )
{
static unsigned int uiUpdateCount = 0;
static char cUpdateString[ 200 ];
extern char *pcMainGetTaskStatusMessage( void );

	/* Unused parameter. */
	( void ) iBufferLength;

	/* The SSI handler function that generates text depending on the index of
	the SSI tag encountered. */
	
	switch( iIndex )
	{
		case ssiTASK_STATS_INDEX :
			vTaskList( pcBuffer );
			break;

		case ssiRUN_TIME_STATS_INDEX :
			vTaskGetRunTimeStats( pcBuffer );
			break;
	}

	/* Include a count of the number of times an SSI function has been executed
	in the returned string. */
	uiUpdateCount++;
	sprintf( cUpdateString, "\r\n\r\n%u\r\nStatus - %s", uiUpdateCount, pcMainGetTaskStatusMessage() );
	strcat( pcBuffer, cUpdateString );

	return strlen( pcBuffer );
}
/*-----------------------------------------------------------*/

signed char *pcLwipAppsBlockingGetTxBuffer( void )
{
signed char *pcReturn;

	/* Attempt to obtain the semaphore that guards the Tx buffer. */
	if( xSemaphoreTakeRecursive( xTxBufferMutex, lwipappsMAX_TIME_TO_WAIT_FOR_TX_BUFFER_MS ) == pdFAIL )
	{
		/* The semaphore could not be obtained before timing out. */
		pcReturn = NULL;
	}
	else
	{
		/* The semaphore was obtained successfully.  Return a pointer to the
		Tx buffer. */
		pcReturn = cTxBuffer;
	}

	return pcReturn;
}
/*-----------------------------------------------------------*/

void vLwipAppsReleaseTxBuffer( void )
{
	/* Finished with the Tx buffer.  Return the mutex. */
	xSemaphoreGiveRecursive( xTxBufferMutex );
}



