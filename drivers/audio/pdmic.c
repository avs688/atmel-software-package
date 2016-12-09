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

/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "audio/pdmic.h"
#include "chip.h"
#include "dma/dma.h"
#include "errno.h"
#include "mm/cache.h"
#include "peripherals/pmc.h"
#include "trace.h"

/*----------------------------------------------------------------------------
 *        Exported functions
 *----------------------------------------------------------------------------*/

void pdmic_enable(struct _pdmic_desc* desc)
{
	uint32_t id = get_pdmic_id_from_addr(desc->addr);

	pmc_enable_gck(id);
	pmc_enable_peripheral(id);
	/* Enable the overrun error interrupt */
	desc->addr->PDMIC_IER = PDMIC_IER_OVRE;
}

void pdmic_disable(struct _pdmic_desc* desc)
{
	uint32_t id = get_pdmic_id_from_addr(desc->addr);

	/* Disable the overrun error interrupt */
	desc->addr->PDMIC_IDR = PDMIC_IDR_OVRE;
	pmc_disable_gck(id);
	pmc_disable_peripheral(id);
}

int pdmic_configure(struct _pdmic_desc *desc)
{
	uint32_t mr_val;
	uint32_t dspr0_val, dspr1_val;
	uint32_t pclk_rate, gclk_rate;
	uint32_t pclk_prescal, gclk_prescal;
	uint32_t f_pdmic;
	uint32_t id = get_pdmic_id_from_addr(desc->addr);

	desc->addr->PDMIC_CR = (desc->addr->PDMIC_CR & ~PDMIC_CR_ENPDM) | PDMIC_CR_SWRST;

	if (desc->channels != 1) {
		trace_error("only supports one channel\n");
		return -EINVAL;
	}

	switch (desc->dsp_size) {
	case PDMIC_CONVERTED_DATA_SIZE_16:
		dspr0_val = PDMIC_DSPR0_SIZE_16;
		break;

	case PDMIC_CONVERTED_DATA_SIZE_32:
		dspr0_val = PDMIC_DSPR0_SIZE_32;
		break;

	default:
		return -EINVAL;
	}

	switch (desc->dsp_osr) {
	case PDMIC_OVER_SAMPLING_RATIO_64:
		dspr0_val |= PDMIC_DSPR0_OSR(1);
		break;

	case PDMIC_OVER_SAMPLING_RATIO_128:
		dspr0_val |= PDMIC_DSPR0_OSR(0);
		break;

	default:
		return -EINVAL;
	}

	switch (desc->dsp_hpfbyp) {
	case PDMIC_DSP_HIGH_PASS_FILTER_ON:
		dspr0_val &= ~PDMIC_DSPR0_HPFBYP;
		break;

	case PDMIC_DSP_HIGH_PASS_FILTER_OFF:
		dspr0_val |= PDMIC_DSPR0_HPFBYP;
		break;

	default:
		return -EINVAL;
	}

	switch (desc->dsp_sinbyp) {
	case PDMIC_DSP_SINCC_PASS_FILTER_ON:
		dspr0_val &= ~PDMIC_DSPR0_SINBYP;
		break;

	case PDMIC_DSP_SINCC_PASS_FILTER_OFF:
		dspr0_val |= PDMIC_DSPR0_SINBYP;
		break;

	default:
		return -EINVAL;
	}

	if (desc->dsp_shift < PDMIC_DSPR_SHIFT_MAX_VAL)
		dspr0_val |= PDMIC_DSPR0_SHIFT(desc->dsp_shift);
	else
		return -EINVAL;

	if (desc->dsp_scale < PDMIC_DSPR_SCALE_MAX_VAL)
		dspr0_val |= PDMIC_DSPR0_SCALE(desc->dsp_scale);
	else
		return false;

	dspr1_val = PDMIC_DSPR1_OFFSET(desc->dsp_offset);

	if (desc->dsp_dgain < PDMIC_DSPR_DGAIN_MAX_VAL)
		dspr1_val |= PDMIC_DSPR1_DGAIN(desc->dsp_dgain);
	else
		return -EINVAL;

	desc->addr->PDMIC_DSPR0 = dspr0_val;
	desc->addr->PDMIC_DSPR1 = dspr1_val;

	f_pdmic = (desc->sample_rate * desc->dsp_osr);

	pclk_rate = pmc_get_peripheral_clock(id);
	gclk_rate = pmc_get_gck_clock(id);

	/* PRESCAL = SELCK/(2*f_pdmic) - 1*/
	pclk_prescal = (uint32_t)(pclk_rate / (f_pdmic << 1)) - 1;
	gclk_prescal = (uint32_t)(gclk_rate / (f_pdmic << 1)) - 1;

	if (pclk_prescal < PDMIC_MR_PRESCAL_MAX_VAL) {
		mr_val = PDMIC_MR_PRESCAL(pclk_prescal) | PDMIC_MR_CLKS_PCLK;
	} else if (gclk_prescal < PDMIC_MR_PRESCAL_MAX_VAL) {
		mr_val = PDMIC_MR_PRESCAL(gclk_prescal) | PDMIC_MR_CLKS_GCLK;
	} else {
		trace_error("PDMIC Prescal configure error");
		return -EINVAL;
	}

	desc->dma.rx.channel = dma_allocate_channel(id, DMA_PERIPH_MEMORY);
	assert(desc->dma.rx.channel);

	/* write configuration */
	desc->addr->PDMIC_MR = mr_val;

	return 0;
}

int pdmic_set_gain(struct _pdmic_desc* desc, uint16_t dgain, uint8_t scale)
{
	uint32_t dspr0_scale, dspr1_dgain;

	if (dgain < PDMIC_DSPR_DGAIN_MAX_VAL &&
			scale < PDMIC_DSPR_SCALE_MAX_VAL) {
		dspr0_scale = PDMIC_DSPR0_SCALE(scale);
		dspr1_dgain = PDMIC_DSPR1_DGAIN(dgain);

		desc->addr->PDMIC_DSPR0 &= ~PDMIC_DSPR0_SCALE_Msk;
		desc->addr->PDMIC_DSPR1 &= ~PDMIC_DSPR1_DGAIN_Msk;

		desc->addr->PDMIC_DSPR0 |= dspr0_scale;
		desc->addr->PDMIC_DSPR1 |= dspr1_dgain;

		return 0;
	}

	return -EINVAL;
}

void pdmic_stream_convert(struct _pdmic_desc* desc, bool flag)
{
	if (flag)
		desc->addr->PDMIC_CR |= PDMIC_CR_ENPDM;
	else
		desc->addr->PDMIC_CR &= ~PDMIC_CR_ENPDM;
}

int pdmic_init(struct _pdmic_desc *desc)
{
	uint32_t id = get_pdmic_id_from_addr(desc->addr);

#if (TRACE_LEVEL >= TRACE_LEVEL_DEBUG)
	uint32_t pclk, gclk;
	pclk = pmc_get_peripheral_clock(id);
	trace_debug("-- PDMIC PCLK: %uMHz --\n\r", (unsigned)(pclk / 1000000));
#endif

	/* The gclk clock frequency must always be three times
	 * lower than the pclk clock frequency
	 */
	struct _pmc_periph_cfg cfg = {
		.gck = {
			.css = PMC_PCR_GCKCSS_PLLA_CLK,
			.div = 18,
		},
	};
	pmc_configure_peripheral(id, &cfg, false);

#if (TRACE_LEVEL >= TRACE_LEVEL_DEBUG)
	gclk = pmc_get_gck_clock(id);
	trace_debug("-- PDMIC GCLK: %uMHz --\n\r", (unsigned)(gclk / 1000000));
#endif
	pdmic_enable(desc);

	return pdmic_configure(desc);
}

bool pdmic_data_ready(struct _pdmic_desc* desc)
{
	return (desc->addr->PDMIC_ISR & PDMIC_ISR_DRDY) == PDMIC_ISR_DRDY;
}

static void _pdmic_dma_read_callback(struct dma_channel* channel, void* args)
{
  	struct _pdmic_desc* desc = (struct _pdmic_desc *)args;

	cache_invalidate_region(desc->dma.rx.cfg.da, desc->dma.rx.cfg.len);

	dma_free_channel(channel);

	if (desc->rx.callback)
		desc->rx.callback(desc, desc->rx.cb_args);

	mutex_unlock(&desc->rx.mutex);
}

static void _pdmic_dma_transfer(struct _pdmic_desc* desc, struct _buffer* buffer)
{
	memset(&desc->dma.rx.cfg, 0, sizeof(desc->dma.rx.cfg));

	desc->dma.rx.cfg.sa = (void*)&desc->addr->PDMIC_CDR;
	desc->dma.rx.cfg.da = buffer->data;
	desc->dma.rx.cfg.upd_sa_per_data = 0;
	desc->dma.rx.cfg.upd_da_per_data = 1;
	desc->dma.rx.cfg.chunk_size = DMA_CHUNK_SIZE_1;

	if (desc->dsp_size == PDMIC_CONVERTED_DATA_SIZE_32) {
		desc->dma.rx.cfg.len = buffer->size / 4;
		desc->dma.rx.cfg.data_width = DMA_DATA_WIDTH_WORD;
	} else {
		desc->dma.rx.cfg.len = buffer->size / 2;
		desc->dma.rx.cfg.data_width = DMA_DATA_WIDTH_HALF_WORD;
	}
	dma_configure_transfer(desc->dma.rx.channel, &desc->dma.rx.cfg);
	dma_set_callback(desc->dma.rx.channel, _pdmic_dma_read_callback, (void*)desc);
	dma_start_transfer(desc->dma.rx.channel);
}

static void _pdmic_polling_transfer(struct _pdmic_desc* desc, struct _buffer* buffer)
{
	uint16_t* data = (uint16_t*)buffer->data;
	uint32_t  length = buffer->size / sizeof(uint16_t);
	volatile uint32_t current = 0;


	while (current < length) {
		if (pdmic_data_ready(desc)) {
			/* start copy data from PDMIC_CDR to memory */
			*data = desc->addr->PDMIC_CDR;
			data++;
			current++;
		}
	}

	if (desc->rx.callback)
		desc->rx.callback(desc, desc->rx.cb_args);

	mutex_unlock(&desc->rx.mutex);
}

int pdmic_transfer(struct _pdmic_desc* desc, struct _buffer* buf, pdmic_callback_t cb, void* user_args)
{
	uint8_t tmode;

	tmode = desc->transfer_mode;

	if ((buf == NULL) || (buf->size == 0))
		return -EINVAL;

	if (buf->attr & PDMIC_BUF_ATTR_READ) {
		mutex_lock(&desc->rx.mutex);

		desc->rx.transferred = 0;
		desc->rx.buffer.data = buf->data;
		desc->rx.buffer.size = buf->size;
		desc->rx.buffer.attr = buf->attr;
		desc->rx.callback = cb;
		desc->rx.cb_args = user_args;

		if(tmode == PDMIC_MODE_DMA)
			_pdmic_dma_transfer(desc, buf);
		else if (tmode == PDMIC_MODE_POLLING)
			_pdmic_polling_transfer(desc, buf);
	}

	return 0;
}

bool pdmic_transfer_is_done(struct _pdmic_desc* desc)
{
	return (!mutex_is_locked(&desc->rx.mutex));
}

void pdmic_dma_set_callback(struct _pdmic_desc* desc, pdmic_callback_t cb, void* arg)
{
	desc->rx.callback = cb;
	desc->rx.cb_args = arg;
}

void pdmic_dma_stop(struct _pdmic_desc* desc)
{
	if (desc->transfer_mode == PDMIC_MODE_DMA) {
		if (desc->dma.rx.channel){
			dma_stop_transfer(desc->dma.rx.channel);
			mutex_unlock(&desc->rx.mutex);
		}
	}
}