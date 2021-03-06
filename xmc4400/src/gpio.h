#ifndef GPIO_H
#define GPIO_H

#include <xmc_device.h>
#include <xmc_gpio.h>
#include <xmc_eth_mac.h>
#include <xmc_ccu8.h>
#include <xmc_uart.h>
#include <xmc_dma.h>
#include <xmc_scu.h>

/******************************************************************************
    Registers, so we can examine them in the debugger
******************************************************************************/
struct XMC_GPIO_PORT_padded:public XMC_GPIO_PORT
{
  __I  uint32_t  RESERVED5[34];
};

struct POSIF_PADDED_t:public POSIF_GLOBAL_TypeDef {
  __I  uint32_t  RESERVED6[4031];
};

struct VADC_complete_t:public VADC_GLOBAL_TypeDef 
{
    __I uint32_t RESERVED5[3];
    struct group:public VADC_G_TypeDef
    {
	__I uint32_t RESERVED[16];
    } G[4];
};

struct CCU4_complete_t:public CCU4_GLOBAL_TypeDef {
    __I uint32_t RESERVED2[31];
    struct cc4_t:public CCU4_CC4_TypeDef {
	__I uint32_t RESERVED2[19];
    } cc[4];
};

struct DSD_complete_t:public DSD_GLOBAL_TypeDef {
    __I uint32_t RESERVED6[6];
    struct dsd_ch_t:public DSD_CH_TypeDef {
	__I uint32_t RESERVED10[21];
    } ch[4];
};

extern XMC_GPIO_PORT_padded gpio_port[15];
extern XMC_DMA_t dma0;
extern XMC_USIC_CH_t u0c0;
extern XMC_USIC_CH_t u0c1;
extern XMC_USIC_CH_t u1c0;
extern XMC_USIC_CH_t u1c1;
extern POSIF_PADDED_t posif[2];
extern VADC_complete_t vadc;
extern CCU4_complete_t ccu40;
extern CCU4_complete_t ccu41;
extern CCU4_complete_t ccu42;
extern CCU4_complete_t ccu43;
extern DSD_complete_t dsd;
extern ETH_GLOBAL_TypeDef eth;

namespace iopin {

template <int port, int pin>
class pinBase
{
public:
    constexpr static int PORT=port;
    constexpr static int PIN=pin;
    pinBase() { static_assert(port>=0 && port<=15,
	"Illegal port, should be 0..15");
    }

    operator int(void) {
	return (gpio_port[port].IN>>pin)&1;
    }

    operator bool(void) {
	return (gpio_port[port].IN>>pin)&1;
    }

    void set(XMC_GPIO_MODE m) { 
	// Isn't this byte adressable?
	gpio_port[port].IOCR[pin/4]&=~(255<<8*(pin%4)); 
	gpio_port[port].IOCR[pin/4]|=m<<8*(pin%4);
    }
    void set(XMC_GPIO_OUTPUT_STRENGTH m) { 
	gpio_port[port].PDR[pin/8]&=~(15<<4*(pin%8));
	gpio_port[port].PDR[pin/8]|=m<<4*(pin%8);
    }
    void pdisc(int i) {
	if(i)
	    gpio_port[port].PDISC|=1<<pin;
	else
	    gpio_port[port].PDISC&=~(1<<pin);
    }
    void pps(int i) {
	if(i)
	    gpio_port[port].PPS|=1<<pin;
	else
	    gpio_port[port].PPS&=~(1<<pin);
    }
    void set(XMC_GPIO_HWCTRL i) {
	gpio_port[port].HWSEL&=~(3<<2*pin);
	gpio_port[port].HWSEL|=(i&3)<<2*pin;
    }

    // Ethernet inputs
    operator XMC_ETH_MAC_PORT_CTRL_RXD0() {
	static_assert(port==-1, "Cannot use this pin as RXD0 for ETH0");
    } 
    operator XMC_ETH_MAC_PORT_CTRL_RXD1() {
	static_assert(port==-1, "Cannot use this pin as RXD1 for ETH0");
    } 
    operator XMC_ETH_MAC_PORT_CTRL_CLK_RMII() {
	static_assert(port==-1, "Cannot use this pin as CLK_RMII for ETH0");
    } 
    operator XMC_ETH_MAC_PORT_CTRL_CRS_DV() {
	static_assert(port==-1, "Cannot use this pin as CRS_DV for ETH0");
    } 
    operator XMC_ETH_MAC_PORT_CTRL_RXER() {
	static_assert(port==-1, "Cannot use this pin as RXER for ETH0");
    } 
    operator XMC_ETH_MAC_PORT_CTRL_MDIO() {
	static_assert(port==-1, "Cannot use this pin as MDIO for ETH0");
    } 
};

template <int port, int pin>
class output:public pinBase<port,pin>
{
public:
    output(void) {
	static_assert(port<14,
	    "Ports 14 and 15 are input only"
	);
	pinBase<port,pin>::set(XMC_GPIO_HWCTRL_DISABLED);
	pinBase<port,pin>::set(XMC_GPIO_MODE_OUTPUT_PUSH_PULL);
    }
    output(int i) {
	output();
	*this=i;
    }
    int operator=(int i) { gpio_port[port].OMR=(i? 1:0x10000)<<pin; return i; }
    void operator^=(int i) { if(i) gpio_port[port].OMR=0x10001<<pin; }
};

template <int port, int pin>
class input:public pinBase<port,pin>
{
public:
    input(void) { 
	pinBase<port,pin>::set(XMC_GPIO_MODE_INPUT_TRISTATE); 
	pinBase<port,pin>::set(XMC_GPIO_HWCTRL_DISABLED);
	if(port>=14)
	    pinBase<port,pin>::pdisc(0);
    }
    operator int(void) { return (gpio_port[port].IN>>pin)&1; }
    operator bool(void) { return (gpio_port[port].IN>>pin)&1; }
};

// ETH0 Ports //////////////////////////////////////////////////////////////////
#define type_conversion(type,pin,port) \
template <> \
inline iopin::pinBase<pin,port>::operator type() \
{ \
    set(XMC_GPIO_MODE_INPUT_TRISTATE); \
    return type##_P##pin##_##port; \
}


type_conversion(XMC_ETH_MAC_PORT_CTRL_RXD0,2,2);
type_conversion(XMC_ETH_MAC_PORT_CTRL_RXD0,0,2);
type_conversion(XMC_ETH_MAC_PORT_CTRL_RXD0,14,8);
type_conversion(XMC_ETH_MAC_PORT_CTRL_RXD0,5,0);

type_conversion(XMC_ETH_MAC_PORT_CTRL_RXD1,2,3); 
type_conversion(XMC_ETH_MAC_PORT_CTRL_RXD1,0,3); 
type_conversion(XMC_ETH_MAC_PORT_CTRL_RXD1,14,9);
type_conversion(XMC_ETH_MAC_PORT_CTRL_RXD1,5,1);

type_conversion(XMC_ETH_MAC_PORT_CTRL_CLK_RMII,2,1);
type_conversion(XMC_ETH_MAC_PORT_CTRL_CLK_RMII,0,0);
type_conversion(XMC_ETH_MAC_PORT_CTRL_CLK_RMII,15,8);
type_conversion(XMC_ETH_MAC_PORT_CTRL_CLK_RMII,6,5);

type_conversion(XMC_ETH_MAC_PORT_CTRL_CRS_DV,2,5);
type_conversion(XMC_ETH_MAC_PORT_CTRL_CRS_DV,0,1);
type_conversion(XMC_ETH_MAC_PORT_CTRL_CRS_DV,15,9);
type_conversion(XMC_ETH_MAC_PORT_CTRL_CRS_DV,5,2);

type_conversion(XMC_ETH_MAC_PORT_CTRL_RXER,2,4);
type_conversion(XMC_ETH_MAC_PORT_CTRL_RXER,0,11);
type_conversion(XMC_ETH_MAC_PORT_CTRL_RXER,5,3);

type_conversion(XMC_ETH_MAC_PORT_CTRL_MDIO,0,9);
type_conversion(XMC_ETH_MAC_PORT_CTRL_MDIO,2,0);
type_conversion(XMC_ETH_MAC_PORT_CTRL_MDIO,1,11);

#undef type_conversion

#include "gpio_output_conversions"
}

#include "input_conversions.h"
#endif
