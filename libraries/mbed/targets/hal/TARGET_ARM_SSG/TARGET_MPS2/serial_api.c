/* mbed Microcontroller Library
 * Copyright (c) 2006-2013 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
// math.h required for floating point operations for baud rate calculation
#include <math.h>
#include <string.h>
#include <stdlib.h>

#include "serial_api.h"
#include "cmsis.h"
#include "pinmap.h"
#include "error.h"
#include "gpio_api.h"

/******************************************************************************
 * INITIALIZATION
 ******************************************************************************/

static const PinMap PinMap_UART_TX[] = {
    {USBTX	 , UART_0, 0},
    {UART_TX1, UART_1, 0},
    {UART_TX2, UART_2, 0},
    {NC      , NC    , 0}
};

static const PinMap PinMap_UART_RX[] = {
    {USBRX	 , UART_0, 0},
    {UART_RX1, UART_1, 0},
    {UART_RX2, UART_2, 0},
    {NC , NC      , 0}
};
/*
static const PinMap PinMap_UART_RTS[] = {
    {P0_0,  UART_0, 0},
    {P0_2,  UART_1, 0},
    {P0_10, UART_2, 0},
    {NC   , NC    , 0}
};

static const PinMap PinMap_UART_CTS[] = {
    {P0_0,  UART_0, 0},
    {P0_2,  UART_1, 0},
    {P0_10, UART_2, 0},
    {NC   , NC    , 0}
};*/

#define UART_NUM    3

static uart_irq_handler irq_handler;

int stdio_uart_inited = 0;
serial_t stdio_uart;

struct serial_global_data_s {
    uint32_t serial_irq_id;
    gpio_t sw_rts, sw_cts;
    uint8_t count, rx_irq_set_flow, rx_irq_set_api;
};

static struct serial_global_data_s uart_data[UART_NUM];

void serial_init(serial_t *obj, PinName tx, PinName rx) {
    int is_stdio_uart = 0;
    
    // determine the UART to use
    UARTName uart_tx = (UARTName)pinmap_peripheral(tx, PinMap_UART_TX);
    UARTName uart_rx = (UARTName)pinmap_peripheral(rx, PinMap_UART_RX);
    UARTName uart = (UARTName)pinmap_merge(uart_tx, uart_rx);
    if ((int)uart == NC) {
        error("Serial pinout mapping failed");
    }
    
    obj->uart = (CMSDK_UART_TypeDef *)uart;
    //set baud rate and enable Uart in normarl mode (RX and TX enabled)
    switch (uart) {
        case UART_0: 	CMSDK_UART0->CTRL = 0;         /* Disable UART when changing configuration */
											//CMSDK_UART0->BAUDDIV = 2604;  // 9600 at 25MHz
											CMSDK_UART0->CTRL    = 0x3;  // Normal mode 
											break;
        case UART_1: 	CMSDK_UART1->CTRL = 0;         /* Disable UART when changing configuration */
											//CMSDK_UART1->BAUDDIV = 2604;  // 9600 at 25MHz
											CMSDK_UART1->CTRL    = 0x3;  // Normal mode 
											break;
        case UART_2: 	CMSDK_UART2->CTRL = 0;         /* Disable UART when changing configuration */
											//CMSDK_UART2->BAUDDIV = 2604;  // 9600 at 25MHz
											CMSDK_UART2->CTRL    = 0x3;  // Normal mode 
											break;
    }

    // set default baud rate and format
    serial_baud  (obj, 9600);
    //serial_format(obj, 8, ParityNone, 1);
    
    // pinout the chosen uart
    pinmap_pinout(tx, PinMap_UART_TX);
    pinmap_pinout(rx, PinMap_UART_RX);
    
    // set rx/tx pins in PullUp mode
    //pin_mode(tx, PullUp);
    //pin_mode(rx, PullUp);
    
    switch (uart) {
        case UART_0: obj->index = 0; break;
        case UART_1: obj->index = 1; break;
        case UART_2: obj->index = 2; break;
    }
    uart_data[obj->index].sw_rts.pin = NC;
    uart_data[obj->index].sw_cts.pin = NC;
    serial_set_flow_control(obj, FlowControlNone, NC, NC);
    
    is_stdio_uart = (uart == STDIO_UART) ? (1) : (0);
    
    if (is_stdio_uart) {
        stdio_uart_inited = 1;
        memcpy(&stdio_uart, obj, sizeof(serial_t));
    }
}

void serial_free(serial_t *obj) {
}

// serial_baud
// set the baud rate, taking in to account the current SystemFrequency
void serial_baud(serial_t *obj, int baudrate) {
    // The MPS2 has a simple divider to control the baud rate. The formula is:
    //
    // Baudrate = PCLK / BAUDDIV
		//
		// PCLK = 25 Mhz
		// so for a desired baud rate of 9600 
		//  25000000 / 9600 = 2604
    //
	//check to see if minimum baud value entered
	int baudrate_div = 0;
	baudrate_div = 25000000 / baudrate;
	if(baudrate >= 16){
    switch ((int)obj->uart) {
        case UART_0: CMSDK_UART0->BAUDDIV = baudrate_div; break;
        case UART_1: CMSDK_UART1->BAUDDIV = baudrate_div; break;
        case UART_2: CMSDK_UART2->BAUDDIV = baudrate_div; break;
        default: error("serial_baud"); break;
    }
	} else {
		error("serial_baud");
	}
    
}

void serial_format(serial_t *obj, int data_bits, SerialParity parity, int stop_bits) {
}

/******************************************************************************
 * INTERRUPTS HANDLING
 ******************************************************************************/
static inline void uart_irq(uint32_t intstatus, uint32_t index, CMSDK_UART_TypeDef *puart) {
    // [Chapter 14] LPC17xx UART0/2/3: UARTn Interrupt Handling
    SerialIrq irq_type;
    switch (intstatus) {
        case 1: irq_type = TxIrq; break;
        case 2: irq_type = RxIrq; break;
        default: return;
    }
    if ((RxIrq == irq_type) && (NC != uart_data[index].sw_rts.pin)) {
        gpio_write(&uart_data[index].sw_rts, 1);
        // Disable interrupt if it wasn't enabled by other part of the application
        if (!uart_data[index].rx_irq_set_api)
            puart->CTRL &= ~(1 << RxIrq);
    }
    if (uart_data[index].serial_irq_id != 0)
        if ((irq_type != RxIrq) || (uart_data[index].rx_irq_set_api))
            irq_handler(uart_data[index].serial_irq_id, irq_type);
}

void uart0_irq() {uart_irq(CMSDK_UART0->INTSTATUS & 0x3, 0, (CMSDK_UART_TypeDef*)CMSDK_UART0);}
void uart1_irq() {uart_irq(CMSDK_UART0->INTSTATUS & 0x3, 1, (CMSDK_UART_TypeDef*)CMSDK_UART1);}
void uart2_irq() {uart_irq(CMSDK_UART0->INTSTATUS & 0x3, 2, (CMSDK_UART_TypeDef*)CMSDK_UART2);}

void serial_irq_handler(serial_t *obj, uart_irq_handler handler, uint32_t id) {
    irq_handler = handler;
    uart_data[obj->index].serial_irq_id = id;
}

static void serial_irq_set_internal(serial_t *obj, SerialIrq irq, uint32_t enable) {
    IRQn_Type irq_n = (IRQn_Type)0;
    uint32_t vector = 0;
    switch ((int)obj->uart) {
			case UART_0: irq_n=((irq>> 2) ? UARTRX0_IRQn : UARTTX0_IRQn); vector = (uint32_t)&uart0_irq; break;
      case UART_1: irq_n=((irq>> 2) ? UARTRX1_IRQn : UARTTX1_IRQn); vector = (uint32_t)&uart1_irq; break;
      case UART_2: irq_n=((irq>> 2) ? UARTRX2_IRQn : UARTTX2_IRQn); vector = (uint32_t)&uart2_irq; break;
        //case UART_3: irq_n=UART3_IRQn; vector = (uint32_t)&uart3_irq; break;
    }
    
    if (enable) {
        obj->uart->CTRL |= 1 << irq;
        NVIC_SetVector(irq_n, vector);
        NVIC_EnableIRQ(irq_n);
    } else if ((TxIrq == irq) || (uart_data[obj->index].rx_irq_set_api + uart_data[obj->index].rx_irq_set_flow == 0)) { // disable
        int all_disabled = 0;
        SerialIrq other_irq = (irq == RxIrq) ? (TxIrq) : (RxIrq);
        obj->uart->CTRL &= ~(1 << irq);
        all_disabled = (obj->uart->CTRL & (1 << other_irq)) == 0;
        if (all_disabled)
            NVIC_DisableIRQ(irq_n);
    }
}

void serial_irq_set(serial_t *obj, SerialIrq irq, uint32_t enable) {
    if (RxIrq == irq)
        uart_data[obj->index].rx_irq_set_api = enable;
    serial_irq_set_internal(obj, irq, enable);
}

/*static void serial_flow_irq_set(serial_t *obj, uint32_t enable) {
    uart_data[obj->index].rx_irq_set_flow = enable;
    serial_irq_set_internal(obj, RxIrq, enable);
}*/

/******************************************************************************
 * READ/WRITE
 ******************************************************************************/
int serial_getc(serial_t *obj) {
    while (serial_readable(obj) == 0);
    int data = obj->uart->DATA;
    return data;
}

void serial_putc(serial_t *obj, int c) {
    while (serial_writable(obj));
    obj->uart->DATA = c;
}

int serial_readable(serial_t *obj) {
    return obj->uart->STATE & 2;
}

int serial_writable(serial_t *obj) {
		return obj->uart->STATE & 1;
}

void serial_clear(serial_t *obj) {
    obj->uart->DATA = 0x00;
}

void serial_pinout_tx(PinName tx) {
    pinmap_pinout(tx, PinMap_UART_TX);
}

void serial_break_set(serial_t *obj) {
}

void serial_break_clear(serial_t *obj) {
}
void serial_set_flow_control(serial_t *obj, FlowControl type, PinName rxflow, PinName txflow) {
}

