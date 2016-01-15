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

/**
 * \page  PDMIC Example
 *
 * \section Purpose
 *
 * The pdmic example will help new users get familiar with Atmel's
 * SAMA5D2X family of microcontrollers. This basic application shows the
 * usage of pdmic and can play the demonstration music with speaker.
 *
 * \section Requirements
 *
 * This package can be used with SAMA5D2-EK and SAMA5D2-XPLAINED.
 *
 * \section Description
 *
 * The demonstration program evaluates the pdmic parameter set. The process
 * of pdmic parameter set can be switched by the corresponding buttons.
 *
 *
 * \section Usage
 *
 * -# Build the program and download it inside the evaluation board. Please
 *    refer to the
 *    <a href="http://www.atmel.com/dyn/resources/prod_documents/6421B.pdf">
 *    SAM-BA User Guide</a>, the
 *    <a href="http://www.atmel.com/dyn/resources/prod_documents/doc6310.pdf">
 *    GNU-Based Software Development</a>
 *    application note or to the
 *    <a href="ftp://ftp.iar.se/WWWfiles/arm/Guides/EWARM_UserGuide.ENU.pdf">
 *    IAR EWARM User Guide</a>,
 *    depending on your chosen solution.
 * -# On the computer, open and configure a terminal application
 *    (e.g. HyperTerminal on Microsoft Windows) with these settings:
 *   - 57600 bauds
 *   - 8 bits of data
 *   - No parity
 *   - 1 stop bit
 *   - No flow control
 * -# Start the application.
 * -# In the terminal window, the following text should appear:
 *     \code
 *      -- Pdmic Example xxx --
 *      -- SAMxxxxx-xx
 *      -- Compiled: xxx xx xxxx xx:xx:xx --
 *     \endcode
 * -# Connect the audio xplained board to the A5D2 board first;
 * -# Press one of the keys listed in the menu to perform the corresponding action;
 * -# Press key '1' or key '2' to record the sound for a short time
 * -# press key '3' to display the sound information
 * -# press key '4' to playback the sound using CLASSD
 * -# press key '+' or key '-' to increase or decrease the recorded sound gain
 * \section References
 * - pdmic/main.c
 * - pdmic.h
 * - pdmic.c
 */

/** \file
 *
 *  This file contains all the specific code for the pdmic example.
 *
 */

/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/

#include "chip.h"
#include "board.h"
#include "compiler.h"
#include "trace.h"
#include "timer.h"
#include "wav.h"

#include "peripherals/aic.h"
#include "peripherals/pdmic.h"
#include "peripherals/classd.h"
#include "peripherals/pio.h"
#include "peripherals/pmc.h"
#include "peripherals/wdt.h"


#include "misc/console.h"

#include <string.h>
#include <stdio.h>
#include <stdbool.h>


/*----------------------------------------------------------------------------
 *         Local constants
 *----------------------------------------------------------------------------*/

#define SAMPLE_RATE (48000)

/* record 10 seconds */
#define SAMPLE_COUNT (10 * SAMPLE_RATE)

#define INITIAL_GAIN (10)

#define INITIAL_ATTENUATION (10)

static const struct _pin classd_pins[] = BOARD_CLASSD_PINS;

static const struct _pin pdmic_pins[] = PINS_PDMIC_IOS1;

static const struct {
	int8_t gain;
	uint16_t dgain;
	uint8_t scale;
} mic_gain_table[] = {
	{ -70, 5, 14},
	{ -60, 33, 15},
	{ -50, 13, 12},
	{ -40, 41, 12},
	{ -30, 1036, 15},
	{ -20, 3277, 15},
	{ -10, 5181, 14},
	{ 0, 1, 0},
	{10, 25905, 13},
	{20, 10, 0},
	{30, 16191, 9},
	{40, 100, 0},
	{50, 20239, 6},
	{60, 1000, 0},
	{70, 12649, 2},
};

/*----------------------------------------------------------------------------
 *        Local variables
 *----------------------------------------------------------------------------*/

static uint32_t _start_tick;

SECTION(".region_ddr")
static uint16_t _sound_buffer[SAMPLE_COUNT];

static bool _sound_recorded = false;

/** pdmic Configuration */
static struct _pdmic_desc pdmic_desc = {
	.sample_rate = SAMPLE_RATE,
	.channels = 1,
	.dsp_size = PDMIC_CONVERTED_DATA_SIZE_16,
	.dsp_osr = PDMIC_OVER_SAMPLING_RATIO_64,
	.dsp_hpfbyp = PDMIC_DSP_HIGH_PASS_FILTER_ON,
	.dsp_sinbyp = PDMIC_DSP_SINCC_PASS_FILTER_ON,
	/* while shift = 0 offset = 0
	 * dgain = 1 scale = 0, gain = 0(dB)
	 */
	.dsp_shift = 0,
	.dsp_offset = 0,
	.dsp_dgain = 1,
	.dsp_scale = 0,

};


/** ClassD Configuration */
static struct _classd_desc classd_desc = {
	.sample_rate = SAMPLE_RATE,
	.mode = BOARD_CLASSD_MODE,
	.non_ovr = CLASSD_NONOVR_10NS,
	.swap_channels = true,
	.mono = BOARD_CLASSD_MONO,
	.mono_mode = BOARD_CLASSD_MONO_MODE,
	.left_enable = true,
	.right_enable = true,
};

/*----------------------------------------------------------------------------
 *         Local functions
 *----------------------------------------------------------------------------*/

/**
 * \brief Display main menu.
 */
static void _display_menu(void)
{
	printf("\n\r");
	printf("Select an option:\n\r");
	printf("-----------------\n\r");
	printf("1 -> Record the sound with DMA\n\r");
	printf("2 -> Record the sound for polling\n\r");
	printf("3 -> Playback the record sound using CLASSD \n\r");
	printf("+ -> Increase the gain of record sound(increased 10dB)\n\r");
	printf("- -> Decrease the gain of record sound(reduced 10dB)\n\r");
	printf("=>");
}

/**
 * \brief Set record sound gain.
 */
static void _set_gain(int8_t gain)
{
	/* While offset and shift in DSP configuration Register are zero,
	 * the gaid(dB) = 20lg(dgain/2^scale). Lookup the gain table and
	 * set the value to corresponding bits
	 */
	uint8_t i;
	printf("Setting record sound gain to %ddB\r\n", gain);

	for (i = 0; i < ARRAY_SIZE(mic_gain_table); i++) {
		if (mic_gain_table[i].gain == gain) {
			pdmic_desc.dsp_dgain  = mic_gain_table[i].dgain;
			pdmic_desc.dsp_scale = mic_gain_table[i].scale;
			break;
		}
	}

	if (i == ARRAY_SIZE(mic_gain_table)) {
		printf("Lookup the gain value failed\n\r");
		return;
	}

	pdmic_set_gain(pdmic_desc.dsp_dgain, pdmic_desc.dsp_scale);
}


static void _record_start(void)
{
	printf("<Record Start>\r\n");
	_start_tick = timer_get_tick();
	pdmic_stream_convert(true);
	_sound_recorded = false;
}

static void _record_stop(void)
{
	uint32_t elapsed = timer_get_interval(_start_tick, timer_get_tick());
	printf("<Record Stop (%ums elapsed)>\r\n", (unsigned)elapsed);
	pdmic_stream_convert(false);
	_sound_recorded = true;
}

static void _play_start(void)
{
	printf("<Play Start>\r\n");
	_start_tick = timer_get_tick();
	classd_volume_unmute(true, true);

}

static void _play_stop(void)
{
	uint32_t elapsed = timer_get_interval(_start_tick, timer_get_tick());
	printf("<Play Stop (%ums elapsed)>\r\n", (unsigned)elapsed);
	classd_volume_mute(true, true);
}

/**
 *  \brief DMA callback
 */
static void _pdmic_dma_callback(struct _xdmad_channel *channel, void *arg)
{
	bool *done = arg;
	*done = true;
}

/**
 * \brief Record sound with DMA.
 */
static void _record_sound_with_dma(void)
{
	uint32_t  audio_length = SAMPLE_COUNT * 2;
	volatile bool done = false;

	_record_start();
	pdmic_dma_transfer((void *)_sound_buffer, audio_length, _pdmic_dma_callback, (void*)&done);
	while (!done);
	_record_stop();
}

/**
 * \brief Record sound without DMA.
 */
static void _record_sound_polling(void)
{
	uint16_t *sound_data = _sound_buffer;
	volatile uint32_t current_sample = 0;

	_record_start();
	while (current_sample < SAMPLE_COUNT) {
		if (pdmic_data_ready()) {
			/* start copy data from PDMIC_CDR to memory */
			*sound_data = PDMIC->PDMIC_CDR;
			sound_data++;
			current_sample++;
		}

	}
	_record_stop();
}

/**
 * \brief Play wav format sound using CLASSD.
 */
static void _playback_using_classd(void)
{
	/* our Classd support 16 bit sound only*/
	volatile uint16_t *audio = _sound_buffer;
	volatile uint32_t current_sample = 0;

	if (!_sound_recorded) {
	       printf("Please record the sound first\n\r");
	       return;
	}

	classd_configure(&classd_desc);
	classd_set_equalizer(CLASSD_EQCFG_FLAT);

	classd_set_left_attenuation(INITIAL_ATTENUATION);
	classd_set_right_attenuation(INITIAL_ATTENUATION);
	classd_volume_unmute(true, true);
	_play_start();

	while (current_sample < SAMPLE_COUNT) {
		if (CLASSD->CLASSD_ISR & CLASSD_ISR_DATRDY) {
			CLASSD->CLASSD_THR = *audio;
			audio++;
			current_sample++;
		}
	}

	_play_stop();
	classd_volume_mute(true, true);

}

/*----------------------------------------------------------------------------
 *         Global functions
 *----------------------------------------------------------------------------*/

/**
 *  \brief pdmic Application entry point
 *  \return Unused (ANSI-C compatibility)
 */
int main(void)
{
	uint8_t key = 0;
	int8_t gain = INITIAL_GAIN;

	/* disable watchdog */
	wdt_disable();

	/* enable console */
	board_cfg_console();

#ifndef VARIANT_DDRAM
	/* Enable DDRAM */
	board_cfg_ddram();
#endif

	/* output example information */
	printf("-- PDMIC Example " SOFTPACK_VERSION " --\n\r");
	printf("-- " BOARD_NAME "\n\r");
	printf("-- Compiled: " __DATE__ " " __TIME__ " --\n\r");

	/* configure PIO muxing for ClassD */
	pio_configure(classd_pins, ARRAY_SIZE(classd_pins));

	/* configure PIO muxing for pdmic */
	pio_configure(pdmic_pins, ARRAY_SIZE(pdmic_pins));

	if (pdmic_init(&pdmic_desc))
		printf("PDMIC configured\r\n");
	else
		printf("PDMIC configuration failed!\r\n");


	while (1) {
		_display_menu();
		key = console_get_char();
		printf("%c\r\n", key);

		if (key == '1')
			_record_sound_with_dma();
		else if (key == '2')
			_record_sound_polling();
		else if (key == '3')
			_playback_using_classd();
		else if (key == '+') {
			if (gain < 70) {
				gain += 10;
				_set_gain(gain);
			} else
				printf("Gain is already at max (70dB)\r\n");
		} else if (key == '-') {
			if (gain > -70) {
				gain -= 10;
				_set_gain(gain);
			} else
				printf("Gain is already at min (-70dB)\r\n");
		}
	}

}
