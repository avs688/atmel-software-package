/* ----------------------------------------------------------------------------
 *         SAM Software Package License
 * ----------------------------------------------------------------------------
 * Copyright (c) 2016, Atmel Corporation
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the disclaimer below.
 *
 * Atmel's name may not be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * DISCLAIMER: THIS SOFTWARE IS PROVIDED BY ATMEL "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT ARE
 * DISCLAIMED. IN NO EVENT SHALL ATMEL BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * ----------------------------------------------------------------------------
 */

/** \addtogroup xmad_module
 *
 * \section dma Dma Configuration Usage
 *
 * To configure a DMA channel, the user has to follow these few steps :
 * <ul>
 * <li> Initialize a DMA driver instance by dmacd_initialize().</li>
 * <li> choose an available (disabled) channel using dmacd_allocate_channel().</li>
 * <li> After the DMAC selected channel has been programmed, dmacd_prepare_channel() is to enable
 * clock and dma peripheral of the DMA, and set Configuration register to set up the transfer type
 * (memory or non-memory peripheral for source and destination) and flow control device.</li>
 * <li> Invoke dmacd_start_transfer() to start DMA transfer  or dmacd_stop_transfer() to force stop DMA transfer.</li>
  * <li> Once the buffer of data is transferred, dmacd_is_transfer_done() checks if DMA transfer is finished.</li>
 * <li> dmacd_handler() handles DMA interrupt, and invoking dmacd_set_callback() if provided.</li>
 * </ul>
 *
 * Related files:\n
 * \ref dmacd.h\n
 * \ref dmacd.c\n
 */

/** \file */

/** \addtogroup dmacd_functions
  @{*/

/*----------------------------------------------------------------------------
 *        Includes
 *----------------------------------------------------------------------------*/

#include "irq/irq.h"
#include "peripherals/pmc.h"
#include "dma/dmacd.h"
#include "dma/dma.h"
#include <assert.h>
#include "compiler.h"

/*----------------------------------------------------------------------------
 *        Local constants
 *----------------------------------------------------------------------------*/

static Dmac* controllers[] = {
#ifdef DMAC0
	DMAC0,
#endif
#ifdef DMAC1
	DMAC1,
#endif
};

/*----------------------------------------------------------------------------
 *        Local definitions
 *----------------------------------------------------------------------------*/

#ifdef CONFIG_SOC_SAM9XX5
#define DMAC_CFG_SRC_PER_MSB_Msk 0
#define DMAC_CFG_DST_PER_MSB_Msk 0
#define DMAC_CFG_SRC_PER_MSB(x) 0
#define DMAC_CFG_DST_PER_MSB(x) 0
#endif

#define DMAC_CONTROLLERS ARRAY_SIZE(controllers)
#define DMACD_CHANNELS (DMAC_CONTROLLERS * DMAC_CHANNELS)

/** DMA state for channel */
enum {
	DMACD_STATE_FREE = 0,  /**< Free channel */
	DMACD_STATE_ALLOCATED, /**< Allocated to some peripheral */
	DMACD_STATE_STARTED,   /**< DMA started */
	DMACD_STATE_DONE,      /**< DMA transfer done */
	DMACD_STATE_SUSPENDED, /**< DMA suspended */
};

/** DMA driver channel */
struct _dmacd_channel
{
	Dmac             *dmac;      /**< DMAC instance */
	uint32_t         id;         /**< Channel ID */
	dmacd_callback_t  callback;  /**< Callback */
	void             *user_arg;  /**< Callback argument */
	uint8_t          src_txif;   /**< Source TX Interface ID */
	uint8_t          src_rxif;   /**< Source RX Interface ID */
	uint8_t          dest_txif;  /**< Destination TX Interface ID */
	uint8_t          dest_rxif;  /**< Destination RX Interface ID */
	volatile uint32_t rep_count; /**< repeat count in auto mode */
	volatile uint8_t state;      /**< Channel State */
	char             dummy[4];   /** Aligned with dma_channel */
};

/** DMA driver instance */
struct _dmacd {
	struct _dmacd_channel channels[DMACD_CHANNELS];
	bool                  polling;
	uint8_t               polling_timeout;
};

static struct _dmacd _dmacd;

/*----------------------------------------------------------------------------
 *        Local functions
 *----------------------------------------------------------------------------*/
/**
 * \brief Enable clock of the DMA peripheral, Enable the peripheral,
 * setup configuration register for transfer.
 * \param channel Channel pointer
 */
static uint32_t dmacd_prepare_channel(struct _dmacd_channel *channel);

static struct _dmacd_channel* _dmacd_channel(Dmac* dmac, uint32_t channel)
{
	int i;
	struct _dmacd_channel* ch = NULL;
	for (i = 0; i < ARRAY_SIZE(controllers); i++) {
		if (controllers[i] == dmac)
			ch = &_dmacd.channels[i * DMAC_CHANNELS + channel];
	}
	assert(ch != NULL);
	return ch;
}

/**
 * \brief DMA interrupt handler
 * \param source Peripheral ID of DMA controller
 */
static void dmacd_handler(uint32_t source, void* user_arg)
{
	uint32_t chan, gis;
	Dmac* dmac = (Dmac*)user_arg;


	gis = dmac_get_global_isr(dmac);
	if ((gis & 0xFFFFFFFF) == 0)
		return;;

	for (chan = 0; chan < DMAC_CHANNELS; chan++) {
		struct _dmacd_channel *channel;
		bool exec = false;
		if (!(gis & ((DMAC_EBCISR_BTC0 | DMAC_EBCISR_CBTC0 | DMAC_EBCISR_ERR0) << chan)))
			continue;
		channel = _dmacd_channel(dmac, chan);
		if (channel->state == DMACD_STATE_FREE)
			continue;
		if (gis & (DMAC_EBCISR_CBTC0 << chan)) {
			if (channel->rep_count) {
				if (channel->rep_count == 1) {
					dmac_auto_clear(dmac, chan);
				}
				dmac_resume_channel(dmac, chan);
				channel->rep_count--;

			} else {
				channel->state = DMACD_STATE_DONE;
				exec = 1;
			}
		}
		/* Execute callback */
		if (exec && channel->callback) {
			channel->callback(channel, channel->user_arg);
			dma_free_item((struct dma_channel *)channel);
		}
	}
}

/*----------------------------------------------------------------------------
 *        Exported functions
 *----------------------------------------------------------------------------*/

void dmacd_initialize(bool polling)
{
	uint32_t cont, chan;

	_dmacd.polling = polling;
	for (cont = 0; cont < DMAC_CONTROLLERS; cont++) {
		Dmac* dmac = controllers[cont];
		dmac_get_channel_status(dmac);
		for (chan = 0; chan < DMAC_CHANNELS; chan++) {
			struct _dmacd_channel *channel = _dmacd_channel(dmac, chan);
			channel->dmac = dmac;
			channel->id = chan;
			channel->callback = 0;
			channel->user_arg = 0;
			channel->src_txif = 0;
			channel->src_rxif = 0;
			channel->dest_txif = 0;
			channel->dest_rxif = 0;
			channel->rep_count = 0;
			channel->state = DMACD_STATE_FREE;
		}

		if (!polling) {
			uint32_t pid = get_dmac_id_from_addr(dmac);
			/* enable interrupts */
			irq_add_handler(pid, dmacd_handler, dmac);
			irq_enable(pid);
		}
	}
}

void dmacd_poll(void)
{
	if (_dmacd.polling) {
		int i;
		for (i = 0; i < DMAC_CONTROLLERS; i++)
			dmacd_handler(get_dmac_id_from_addr(controllers[i]), controllers[i]);
	}
}

struct _dmacd_channel *dmacd_allocate_channel(uint8_t src, uint8_t dest)
{
	uint32_t i;

	/* Reject peripheral to peripheral transfers */
	if (src != DMACD_PERIPH_MEMORY && dest != DMACD_PERIPH_MEMORY) {
		return NULL;
	}

	for (i = 0; i < DMACD_CHANNELS; i++) {
		struct _dmacd_channel *channel = &_dmacd.channels[i];
		Dmac *dmac = channel->dmac;

		if (channel->state == DMACD_STATE_FREE) {
			/* Check if source peripheral matches this channel controller */
			if (src != DMACD_PERIPH_MEMORY)
				if (!is_peripheral_on_dma_controller(src, dmac))
					continue;

			/* Check if destination peripheral matches this channel controller */
			if (dest != DMACD_PERIPH_MEMORY)
				if (!is_peripheral_on_dma_controller(dest, dmac))
					continue;

			/* Allocate the channel */
			channel->state = DMACD_STATE_ALLOCATED;
			channel->src_txif = get_peripheral_dma_channel(src, dmac, true);
			channel->src_rxif = get_peripheral_dma_channel(src, dmac, false);
			channel->dest_txif = get_peripheral_dma_channel(dest, dmac, true);
			channel->dest_rxif = get_peripheral_dma_channel(dest, dmac, false);
			dmacd_prepare_channel(channel);

			return channel;
		}
	}
	return NULL;
}

uint32_t dmacd_free_channel(struct _dmacd_channel *channel)
{
	switch (channel->state) {
	case DMACD_STATE_STARTED:
		return DMACD_BUSY;
	case DMACD_STATE_ALLOCATED:
	case DMACD_STATE_DONE:
		channel->state = DMACD_STATE_FREE;
		break;
	}
	return DMACD_OK;
}

uint32_t dmacd_set_callback(struct _dmacd_channel *channel,
		dmacd_callback_t callback, void *user_arg)
{
	if (channel->state == DMACD_STATE_FREE)
		return DMACD_ERROR;
	else if (channel->state == DMACD_STATE_STARTED)
		return DMACD_BUSY;

	channel->callback = callback;
	channel->user_arg = user_arg;

	return DMACD_OK;
}

static uint32_t dmacd_prepare_channel(struct _dmacd_channel *channel)
{
	Dmac *dmac = channel->dmac;

	if (channel->state == DMACD_STATE_FREE)
		return DMACD_ERROR;
	else if (channel->state == DMACD_STATE_STARTED)
		return DMACD_BUSY;

	/* Clear status */
	dmac_get_global_isr(dmac);

	/* Enable clock of the DMA peripheral */	
	pmc_configure_peripheral(get_dmac_id_from_addr(dmac), NULL, true);

	/* Clear status */
	dmac_get_channel_status(dmac);

	/* Disables DMAC interrupt for the given channel */
	dmac_disable_global_it(dmac, -1);

	dmac_enable(dmac);
	/* Disable the given dma channel */
	dmac_disable_channel(dmac, channel->id);
	dmac_set_src_addr(dmac, channel->id, 0);
	dmac_set_dest_addr(dmac, channel->id, 0);

	dmac_set_channel_config(dmac, channel->id, 0);
	dmac_set_descriptor_addr(dmac, channel->id, 0, 0);

	return DMACD_OK;
}

bool dmacd_is_transfer_done(struct _dmacd_channel *channel)
{
	return ((channel->state != DMACD_STATE_STARTED) 
			&& (channel->state != DMACD_STATE_SUSPENDED));
}

uint32_t dmacd_configure_transfer(struct _dmacd_channel *channel,
		struct _dmacd_cfg *cfg,
		void *desc_addr)
{
	void * sa;
	void * da;
	uint32_t ctrla;
	uint32_t ctrlb;
	if (channel->state == DMACD_STATE_FREE)
		return DMACD_ERROR;
	else if (channel->state == DMACD_STATE_STARTED)
		return DMACD_BUSY;
	Dmac *dmac = channel->dmac;

	sa = ((struct _dma_desc *)desc_addr)->sa;
	da = ((struct _dma_desc *)desc_addr)->da;
	ctrla = ((struct _dma_desc *)desc_addr)->ctrla;
	ctrlb = ((struct _dma_desc *)desc_addr)->ctrlb;

	cfg->cfg &= ~(DMAC_CFG_SRC_PER_Msk | DMAC_CFG_SRC_PER_MSB_Msk
				| DMAC_CFG_DST_PER_Msk | DMAC_CFG_DST_PER_MSB_Msk) ;
	if ((ctrlb & DMAC_CTRLB_FC_Msk) == DMAC_CTRLB_FC_PER2MEM_DMA_FC) {
		cfg->cfg |= DMAC_CFG_SRC_PER(channel->src_rxif);
		cfg->cfg |= DMAC_CFG_SRC_PER_MSB(channel->src_rxif >> 4);
	}
	if ((ctrlb & DMAC_CTRLB_FC_Msk) == DMAC_CTRLB_FC_MEM2PER_DMA_FC) {
		cfg->cfg |= DMAC_CFG_DST_PER(channel->dest_txif);
		cfg->cfg |= DMAC_CFG_DST_PER_MSB(channel->dest_txif >> 4);
	}

	dmac_get_global_isr(dmac);
	dmac_get_channel_status(dmac);

	/* if DMAC_CTRLBx.AUTO bit is enabled, and source or destination is not fetched
		from linker list, set the channel with AUTO mode, in this mode, the hardware
		sets the Buffer Transfer Completed Interrupt when the buffer transfer has
		completed, It then stalls until STALx bit of DMAC_CHSR is cleared by writing
		in the KEEPx bit of DMAC_CHER */
	if (cfg->trans_auto) {
		if ((cfg->s_decr_fetch) && (cfg->d_decr_fetch)){
			ctrlb |= DMAC_CTRLB_AUTO_ENABLE;
			channel->rep_count = cfg->blocks;
		}
	}
	dmac_set_descriptor_addr(dmac, channel->id, 0, 0);
	if (cfg->s_decr_fetch)
		/* *Buffer Descriptor fetch operation is disabled for the source */
		dmac_set_src_addr(dmac, channel->id, sa);
	else
		/* Source address is updated when the descriptor is fetched from the memory */
		dmac_set_descriptor_addr(dmac, channel->id, desc_addr, 0);

	if (cfg->d_decr_fetch)
		/* *Buffer Descriptor fetch operation is disabled for the destination */
		dmac_set_dest_addr(dmac, channel->id, da);
	else
		/* destination address is updated when the descriptor is fetched from the memory */
		dmac_set_descriptor_addr(dmac, channel->id, desc_addr, 0);
	dmac_set_control_a(dmac, channel->id, ctrla);
	dmac_set_control_b(dmac, channel->id, ctrlb);
	dmac_set_channel_config(dmac, channel->id, cfg->cfg);
	return DMACD_OK;
}

uint32_t dmacd_start_transfer(struct _dmacd_channel *channel)
{
	if (channel->state == DMACD_STATE_FREE)
		return DMACD_ERROR;
	else if (channel->state == DMACD_STATE_STARTED)
		return DMACD_BUSY;

	/* Change state to 'started' */
	channel->state = DMACD_STATE_STARTED;

	/* Start DMA transfer */
	if (!_dmacd.polling) {
		dmac_enable_global_it(channel->dmac, ((DMAC_EBCIDR_CBTC0 | DMAC_EBCIER_BTC0 | DMAC_EBCIER_ERR0) << channel->id));
	}
	dmac_enable_channel(channel->dmac, channel->id);
	return DMACD_OK;
}

uint32_t dmacd_stop_transfer(struct _dmacd_channel *channel)
{
	Dmac *dmac = channel->dmac;

	/* Disable channel */
	dmac_disable_channel(dmac, channel->id);

	/* Clear pending status */
	dmac_get_global_isr(dmac);

	/* Change state to 'allocated' */
	channel->state = DMACD_STATE_ALLOCATED;

	return DMACD_OK;
}

uint32_t dmacd_suspend_transfer(struct _dmacd_channel *channel)
{
	Dmac *dmac = channel->dmac;

	/* Suspend channel */
	dmac_suspend_channel(dmac, channel->id);

	/* Change state to 'suspended' */
	channel->state = DMACD_STATE_SUSPENDED;

	return DMACD_OK;
}

uint32_t dmacd_resume_transfer(struct _dmacd_channel *channel)
{
	Dmac *dmac = channel->dmac;

	/* Resume channel */
	dmac_resume_channel(dmac, channel->id);

	/* Change state to 'started */
	channel->state = DMACD_STATE_STARTED;

	return DMACD_OK;
}

void dmacd_fifo_flush(struct _dmacd_channel *channel)
{
	Dmac *dmac = channel->dmac;

	dmac_fifo_flush(dmac, channel->id);
}

uint32_t dmacd_get_transferred_data_len(struct _dmacd_channel *channel)
{
	Dmac *dmac = channel->dmac;

	return dmac_get_btsize(dmac, channel->id);
}

uint32_t dmacd_get_desc_addr(struct _dmacd_channel *channel)
{
	Dmac *dmac = channel->dmac;

	return dmac_get_descriptor_addr(dmac, channel->id);
}
/**@}*/
