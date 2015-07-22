/* ----------------------------------------------------------------------------
 *         SAM Software Package License
 * ----------------------------------------------------------------------------
 * Copyright (c) 2015, Atmel Corporation
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

/*------------------------------------------------------------------------------
 *        Header
 *----------------------------------------------------------------------------*/

#include "board.h"
#include "chip.h"

#include "peripherals/twi.h"
#include "peripherals/twid.h"
#include "memories/at24.h"

#include "test_at24mac402.h"
#include "crc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*------------------------------------------------------------------------------
 *        Types
 *----------------------------------------------------------------------------*/

static struct _board_info DEFAULT_BOARD_INFO =
{16*2, "ATMEL ", "RFO", 15, 01, "AAA", 0x00, 0x0000, "SAMA5D2-XULT", 'C', 0x00};


/*------------------------------------------------------------------------------
 *
 *----------------------------------------------------------------------------*/


uint8_t at24mac402_display_serial_number (struct _at24* pAt24)
{
	uint8_t Len, status = TWID_SUCCESS;

	printf(" -I- Serial Number:");
	status = at24_get_serial_number(pAt24);
	if(status != TWID_SUCCESS) return status;
	for (Len=0; Len<AT24_SN_SIZE; Len++) {
		printf("%02X.", pAt24->serial_number[Len]);
	}
	printf("\n\r");
	return status;
}

uint8_t at24mac402_display_mac_adress (struct _at24* pAt24)
{
	uint8_t Len, status = TWID_SUCCESS;

	printf(" -I- Mac add EUI48");
	status = at24_get_mac_address(pAt24);
	if(status != TWID_SUCCESS) return status;
	for (Len=0; Len<AT24_EUI48_SIZE; Len++) {
		printf(":%02X", pAt24->mac_addr_48[Len]);
	}
	printf("\n\r");
	return status;
}

uint8_t at24mac402_get_info_board (struct _at24* pAt24, struct _board_info* pInfo)
{
	return at24_read_eep(pAt24, AT24_INFO_ADDR, (uint8_t*)pInfo, AT24_INFO_SIZE);
}

uint8_t at24mac402_set_info_board (struct _at24* pAt24, struct _board_info* pInfo)
{
	uint8_t status = TWID_SUCCESS;

	status = at24_get_serial_number(pAt24);
	pInfo->crc_sn = compute_crc8 ((uint8_t*)pAt24->serial_number, AT24_SN_SIZE);
	pInfo->crc = compute_crc8 ((uint8_t*)pInfo, AT24_INFO_SIZE-1);
	status = at24_write_eep (pAt24, AT24_INFO_ADDR, (uint8_t*)pInfo, AT24_INFO_SIZE);
	return status;
}

uint8_t at24mac402_display_info_board (struct _at24* pAt24)
{
	uint8_t crc, status = TWID_SUCCESS;
	struct _board_info sInfo = {0};
	struct _board_info* pInfo = &sInfo;
	uint8_t buf[32] = {0};

	status = at24mac402_display_serial_number(pAt24);
	status |= at24mac402_display_mac_adress(pAt24);
	status |= at24mac402_get_info_board (pAt24, pInfo);
	if(status != TWID_SUCCESS) return status;

	// Check Info board
	crc = compute_crc8 ((uint8_t*)pInfo, AT24_INFO_SIZE-1);
	if (crc!=pInfo->crc) {
		memcpy (pInfo, &DEFAULT_BOARD_INFO, AT24_INFO_SIZE);
		// write default info
		status = at24mac402_set_info_board (pAt24, pInfo);
		// Read back
		status |= at24mac402_get_info_board (pAt24, pInfo);
		if(status != TWID_SUCCESS) return status;
	}
	printf(" -I- Manufacturing info\n\r");
	printf("\tInfo Size   : %02d\n\r", pInfo->page_size);
	memset(buf, 0x00, sizeof(buf));
	memcpy(buf, (char*)pInfo->manufacturer, SIZE_SUBC);
	printf("\tManufacturer: %s\n\r", buf);
	memset(buf, 0x00, sizeof(buf));
	memcpy(buf, (char*)pInfo->country, SIZE_MANUF_COUNTRY);
	printf("\tMade in     : %s\n\r", buf);
	printf("\tManuf year  : %d\n\r", pInfo->manuf_year+2000);
	printf("\tWeek        : %02d\n\r", pInfo->manuf_week);
	memset(buf, 0x00, sizeof(buf));
	memcpy(buf, (char*)pInfo->rev_code, SIZE_HW_REVISION);
	printf("\tRevision    : %s\n\r", buf);
	printf("\tCrcSN       : Ox%02X\n\r", pInfo->crc_sn);
	printf("\tExtended add: 0x%04X\n\r", pInfo->addr_ext_config);
	memset(buf, 0x00, sizeof(buf));
	memcpy(buf, (char*)pInfo->board_ident, SIZE_BOARD_IDENT);
	printf("\tBoard       : %s\n\r", buf);
	printf("\tMapping     : %c\n\r", pInfo->rev_mapping);
	printf("\tCrc         : 0x%02X\n\r", pInfo->crc);
	return status;
}
