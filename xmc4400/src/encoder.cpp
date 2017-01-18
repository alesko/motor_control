#include "encoder.h"
#include "hardware.h"

#include "xmc_scu.h"
#include "xmc_ccu4.h"

#include <atomic>
#include <math.h>

#include "bitfields.h"

constexpr auto PI=acos(-1);

/*******************************************************************************
    Dummy encoder, encoder is initially of this type and it does nothing
    and is never valid.
*******************************************************************************/
class dummy_encoder_t:public encoder_t {
public:
    dummy_encoder_t(void);

    virtual uint32_t position(void) { return 0;}
    virtual float angle(void) { return 0.0;}
    virtual bool valid(void) { return false;}

    virtual void trigger(void) {}
    virtual void half_duplex(void) {}
    virtual void full_duplex(void) {}
};

dummy_encoder_t::dummy_encoder_t(void)
{
    // Turn encoder power off
    ENC_5V=0;
    ENC_12V=0;
    ENC_DIR=0;

    // disable all encoder peripherals and interrupts
    // FIXME
    XMC_SCU_RESET_AssertPeripheralReset(XMC_SCU_PERIPHERAL_RESET_USIC0);
    XMC_SCU_RESET_AssertPeripheralReset(XMC_SCU_PERIPHERAL_RESET_USIC1);
    XMC_SCU_CLOCK_GatePeripheralClock(XMC_SCU_PERIPHERAL_CLOCK_USIC0);
    XMC_SCU_CLOCK_GatePeripheralClock(XMC_SCU_PERIPHERAL_CLOCK_USIC1);
    NVIC_DisableIRQ(USIC1_0_IRQn);
    NVIC_DisableIRQ(USIC0_1_IRQn);
}

std::unique_ptr<encoder_t> encoder(new dummy_encoder_t);

/*******************************************************************************
    Mitsubishi base class
*******************************************************************************/
class mitsubishi_encoder_t:public encoder_t {
protected:
    int putp;
    uint8_t rx_buffer[16];
public:
    virtual uint32_t position(void) { return 0; }
    virtual float angle(void) { return 0.0F; }
    virtual bool valid(void) { return false; }

    virtual void trigger(void) { }
    virtual void half_duplex(void);
    virtual void full_duplex(void);

    static int detect(void);
private:
    void serial_tx(uint8_t);
};

void mitsubishi_encoder_t::half_duplex(void)
{
    ENC_DIR=0;
    XMC_USIC_CH_t *usic=ENC_TXD;
    uint32_t reason=usic->PSR;
    if(reason & USIC_CH_PSR_ASCMode_RFF_Msk)
	rx_buffer[putp++]=usic->RBUF;
    usic->PSCR=reason;
}

void mitsubishi_encoder_t::full_duplex(void)
{
    // static_assert(uart_number(ENC_RXD2)==1, "FIXME");
    u1c1.PSCR=u1c1.PSR;
    rx_buffer[putp++]=u1c1.RBUF;
}

void mitsubishi_encoder_t::serial_tx(uint8_t data)
{
    using namespace std::chrono;
    ENC_DIR=1;
    putp=0;
    ENC_TXD=data;
    sleep(1ms);
}

/* HC-MFS13-S13 motor *********************************************************/
class mitsubishi_MFS13_t:public mitsubishi_encoder_t
{
    constexpr static int poles=4;
    constexpr static int increments_per_revolution=(1<<20);
    constexpr static float conv=2.0*PI*poles/increments_per_revolution;
public:
    virtual uint32_t position(void);
    virtual float angle(void);
    virtual bool valid(void);

    virtual void trigger(void);
};

float mitsubishi_MFS13_t::angle(void)
{
    uint32_t encoder=rx_buffer[2]+(1<<8)*rx_buffer[3]+(1<<16)*rx_buffer[4];
    return conv*float(encoder);
}

uint32_t mitsubishi_MFS13_t::position(void)
{
    return rx_buffer[2]+(1<<8)*rx_buffer[3]+(1<<16)*rx_buffer[4]
	+(1<<20)*rx_buffer[5]+(1<<28)*(rx_buffer[6]*0x0f);
}

bool mitsubishi_MFS13_t::valid(void)
{
    if(putp!=9)
	return false;
    uint8_t crc=0;
    for(int i=0;i<putp;i++)
	crc^=rx_buffer[i];
    return crc==0;
}

void mitsubishi_MFS13_t::trigger(void)
{
    ENC_DIR=1;
    putp=0;
    ENC_TXD=0x1a;
}

/* HC-PQ[24]3 motor ***********************************************************/
class mitsubishi_PQ_t:public mitsubishi_encoder_t
{
    constexpr static int poles=4;
    constexpr static int increments_per_revolution=(1<<12);
    constexpr static float conv=2.0*PI*poles/increments_per_revolution;
public:
    virtual uint32_t position(void);
    virtual float angle(void);
    virtual bool valid(void);

    virtual void trigger(void);
};

float mitsubishi_PQ_t::angle(void)
{
    uint32_t encoder=rx_buffer[2]+(1<<8)*rx_buffer[3];
    return conv*float(encoder);
}

uint32_t mitsubishi_PQ_t::position(void)
{
    return rx_buffer[2]+(1<<8)*rx_buffer[3]
	+(1<<12)*rx_buffer[5]+(1<<20)*rx_buffer[6]+(1<<28)*rx_buffer[7];
}

bool mitsubishi_PQ_t::valid(void)
{
    if(putp!=9)
	return false;
    uint8_t crc=0;
    for(int i=0;i<putp;i++)
	crc^=rx_buffer[i];
    return crc==0;
}

void mitsubishi_PQ_t::trigger(void)
{
    ENC_DIR=1;
    putp=0;
    ENC_TXD=0x1a;
}

/******************************************************************************/
int mitsubishi_encoder_t::detect(void)
{
    using namespace std::chrono;
    using namespace usic_ch_ns;
    mitsubishi_encoder_t *p=new mitsubishi_encoder_t;
    encoder=std::unique_ptr<mitsubishi_encoder_t>(p);

    XMC_UART_CH_CONFIG_t uart_config = {
	.baudrate=2500000,
	.data_bits=8U,
	.frame_length=0,
	.stop_bits=1,
	.oversampling=0,
	.parity_mode=XMC_USIC_CH_PARITY_MODE_NONE
    };

    constexpr XMC_USIC_CH_t *channel=xmc_channel(ENC_TXD);
    XMC_UART_CH_Init(channel, &uart_config);
    XMC_UART_CH_SetInputSource(channel,
	XMC_UART_CH_INPUT_RXD, dx0(ENC_RXD));
    XMC_UART_CH_EnableEvent(channel, XMC_UART_CH_EVENT_FRAME_FINISHED );
    XMC_UART_CH_SelectInterruptNodePointer(channel,
	XMC_UART_CH_INTERRUPT_NODE_POINTER_PROTOCOL, hd_irq);
    XMC_UART_CH_Start(channel);
    // Protocol interrupt
    NVIC_SetPriority(irq<hd_irq>(ENC_TXD),  0);
    NVIC_ClearPendingIRQ(irq<hd_irq>(ENC_TXD));
    NVIC_EnableIRQ(irq<hd_irq>(ENC_TXD));

    // Powerup and wait
    p->serial_tx(0x1a);
    ENC_5V=1;
    sleep(100ms);

    // First try to use half-duplex mode
    p->serial_tx(0x1a);
    if(p->putp==9) {
	encoder=std::unique_ptr<mitsubishi_MFS13_t>(new mitsubishi_MFS13_t);
	return 1;
    }

    // We don't need to disable the transmitter in full-duplex
    XMC_UART_CH_DisableEvent(ENC_TXD, XMC_UART_CH_EVENT_FRAME_FINISHED);
    NVIC_DisableIRQ(irq<hd_irq>(ENC_TXD));

    // FIXME, hardware fix re-route everything to a single uart
    constexpr XMC_USIC_CH_t *rx_channel=xmc_channel(ENC_RXD2);
    XMC_UART_CH_Init(rx_channel, &uart_config);
    XMC_UART_CH_SetInputSource(rx_channel,XMC_UART_CH_INPUT_RXD, dx0(ENC_RXD2));
    XMC_UART_CH_EnableInputInversion(rx_channel,XMC_UART_CH_INPUT_RXD);
    XMC_UART_CH_EnableEvent(rx_channel, XMC_UART_CH_EVENT_STANDARD_RECEIVE);
    XMC_UART_CH_SelectInterruptNodePointer(rx_channel,
	XMC_UART_CH_INTERRUPT_NODE_POINTER_RECEIVE, fd_irq);
    XMC_UART_CH_Start(rx_channel);
    // Receive interrupt
    NVIC_SetPriority(irq<fd_irq>(ENC_RXD2),  0);
    NVIC_ClearPendingIRQ(irq<fd_irq>(ENC_RXD2));
    NVIC_EnableIRQ(irq<fd_irq>(ENC_RXD2));

    p->serial_tx(0x1a);
    if(p->putp!=9) {
	encoder=std::unique_ptr<dummy_encoder_t>(new dummy_encoder_t);
	return 0;
    }

    encoder=std::unique_ptr<mitsubishi_PQ_t>(new mitsubishi_PQ_t);
    return 1;
}

/*******************************************************************************
    Hiperface encoder
*******************************************************************************/
class hiperface_t:public encoder_t {
    // FIXME, these settings are for the DS56S
    constexpr static int poles=3;
    constexpr static int increments_per_revolution=(1<<12);
    constexpr static float conv=2.0*PI*poles/increments_per_revolution;
    constexpr static float offset=PI/3;

    uint8_t rx_buffer[8];
    uint8_t tx_buffer[8];
    volatile int rx_put, tx_get, tx_len;
public:
    virtual uint32_t position(void) { return CCU40_CC40->TIMER; }
    virtual float angle(void) { return conv*ccu40.cc[0].TIMER+offset; }
    virtual bool valid(void) { return true; }

    virtual void trigger(void) { }
    virtual void half_duplex(void);
    virtual void full_duplex(void) {}

    static int detect(void);
private:
    void transmit(std::initializer_list<uint8_t> msg)
    {
	using namespace std::chrono;
	ENC_DIR=1;
	uint8_t crc=0;
	tx_len=0;
	for(auto x: msg) {
	    crc^=x;
	    tx_buffer[tx_len++]=x;
	}
	tx_buffer[tx_len++]=crc;
	tx_get=0;
	rx_put=0;
	ENC_TXD=tx_buffer[tx_get++];
	sleep(20ms);
    }
};

void hiperface_t::half_duplex(void)
{
    LED2=0;
    XMC_USIC_CH_t *usic=ENC_TXD;
    uint32_t reason=usic->PSR;
    if(reason & USIC_CH_PSR_ASCMode_TFF_Msk) {
	if(tx_get<tx_len)
	    ENC_TXD=tx_buffer[tx_get++];
	else
	    ENC_DIR=0;
    } else if(reason & USIC_CH_PSR_ASCMode_RFF_Msk) {
	rx_buffer[rx_put++]=usic->RBUF;
    }
    usic->PSCR=reason;
    LED2=1;
}

#include "xmc_posif.h"

void posif_init(uint32_t position)
{
    using namespace posif_ns;

    static_assert(unit(ENC_COS)==unit(ENC_SIN),
	"ENC_COS and ENC_SIN must be part of the same POSIF unit");
    auto p=&posif[unit(ENC_COS)];

    XMC_POSIF_CONFIG_t PCONF({{{
	.mode=XMC_POSIF_MODE_QD,
	.input0=pinA(ENC_SIN),
	.input1=pinB(ENC_COS),
	.input2=0,
	.filter=7
    }}});
    XMC_POSIF_Init(p, &PCONF);

    XMC_POSIF_QD_CONFIG_t qd;
    qd.phase_a=0;
    qd.phase_b=0;       /**< Phase-B active level configuration */
    qd.phase_leader=0;  /**< Which of the two phase signals[Phase A or Phase B]
			    leads the other? */
    qd.index=XMC_POSIF_QD_INDEX_GENERATION_NEVER;
    XMC_POSIF_QD_Init(p, &qd);

    XMC_POSIF_Start(p);

    /* Initialise counter FIXME, allocate unit in some way */
    XMC_CCU4_SetModuleClock(CCU40, XMC_CCU4_CLOCK_SCU);
    XMC_CCU4_EnableModule(CCU40);
    XMC_CCU4_Init(CCU40, XMC_CCU4_SLICE_MCMS_ACTION_TRANSFER_PR_CR);
    ccu40.GIDLC=1;		// slice[0] out of idle

    ccu40.cc[0].INS=ccu4_cc4_ns::ins_t({{
	.ev0is=CCU40_IN0_POSIF0_OUT0,	// q_clock
	.ev1is=CCU40_IN1_POSIF0_OUT1,	// q_dir
	.ev2is=CCU40_IN2_POSIF0_OUT2,	// FIXME map this to CCU80 SR1
	.ev0em=XMC_CCU4_SLICE_EVENT_EDGE_SENSITIVITY_RISING_EDGE,
	.ev1em=XMC_CCU4_SLICE_EVENT_EDGE_SENSITIVITY_RISING_EDGE,
	.ev2em=XMC_CCU4_SLICE_EVENT_EDGE_SENSITIVITY_NONE,
	.ev0lm=XMC_CCU4_SLICE_EVENT_LEVEL_SENSITIVITY_ACTIVE_HIGH,
	.ev1lm=XMC_CCU4_SLICE_EVENT_LEVEL_SENSITIVITY_COUNT_UP_ON_LOW,
	.ev2lm=XMC_CCU4_SLICE_EVENT_LEVEL_SENSITIVITY_ACTIVE_HIGH,
	.lpf0m=XMC_CCU4_SLICE_EVENT_FILTER_DISABLED,
	.lpf1m=XMC_CCU4_SLICE_EVENT_FILTER_DISABLED,
	.lpf2m=XMC_CCU4_SLICE_EVENT_FILTER_DISABLED
    }}).raw;
    ccu40.cc[0].CMC=ccu4_cc4_ns::cmc_t({{
	.strts=0,	// no externa start
	.ends=0,	// no external end
	.cap0s=0,	// Should be mapped to CCU80 SR1
	.cap1s=0,	// not external capture 1
	.gates=0,	// no external gating
	.uds=2,		// up/down
	.lds=0,		// no external load
	.cnts=1,	// count
	.ofs=0,
	.ts=0,
	.mos=0,
	.tce=0 
    }}).raw; 
    ccu40.cc[0].PRS=0x0fff;	// 12-bit period (overflow somewhere)
    ccu40.GCSS=1;		// transfer enable (to load period)
    ccu40.cc[0].TIMER=(position/8)&0xffff; 
    ccu40.cc[0].TCSET=1;	// timer ON
}

int hiperface_t::detect(void)
{
    using namespace std::chrono;
    using namespace usic_ch_ns;

    hiperface_t *p=new hiperface_t;
    encoder=std::unique_ptr<hiperface_t>(p);

    XMC_UART_CH_CONFIG_t uart_config = {
	.baudrate=9600,
	.data_bits=8U,
	.frame_length=0,
	.stop_bits=1,
	.oversampling=0,
	.parity_mode=XMC_USIC_CH_PARITY_MODE_EVEN
    };

    constexpr XMC_USIC_CH_t *channel=xmc_channel(ENC_TXD);
    XMC_UART_CH_Init(channel, &uart_config);
    XMC_UART_CH_SetInputSource(channel, XMC_UART_CH_INPUT_RXD, dx0(ENC_RXD));
    XMC_UART_CH_EnableEvent(channel, XMC_UART_CH_EVENT_FRAME_FINISHED );
    XMC_UART_CH_SelectInterruptNodePointer(channel,
	XMC_UART_CH_INTERRUPT_NODE_POINTER_PROTOCOL, hd_irq);
    XMC_UART_CH_Start(channel);
    // Protocol interrupt
    NVIC_SetPriority(irq<hd_irq>(ENC_TXD),  0);
    NVIC_ClearPendingIRQ(irq<hd_irq>(ENC_TXD));
    NVIC_EnableIRQ(irq<hd_irq>(ENC_TXD));

    // Powerup and wait
    ENC_12V=1;
    sleep(200ms);
    p->transmit({0xff, 0x53}); // reset
    do {
	p->transmit({0xff, 0x50});	// ask status
    } while(p->rx_put!=4 || p->rx_buffer[2]);
    p->transmit({0xff,0x42});

    if(p->rx_put!=7) {
	encoder=std::unique_ptr<dummy_encoder_t>(new dummy_encoder_t);
	return 0;
    }

    uint32_t position=p->rx_buffer[5]
	+0x100L*p->rx_buffer[4]
	+0x10000L*p->rx_buffer[3]
	+0x1000000L*p->rx_buffer[2];
    posif_init(position);

    return 1;
}


void init_encoder(void)
{
    if(mitsubishi_encoder_t::detect())
	return;
    if(hiperface_t::detect())
	return;
}
