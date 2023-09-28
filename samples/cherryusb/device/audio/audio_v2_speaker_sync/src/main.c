/*
 * Copyright (c) 2022 HPMicro
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include <stdio.h>
#include "board.h"
#include "pinmux.h"
#include "audio_v2_speaker_sync.h"
#include "usb_config.h"


int main(void)
{
    board_init();
    board_init_usb_pins();

#if defined(USING_CODEC) && USING_CODEC
    board_init_i2c(CODEC_I2C);
    init_i2s_pins(TARGET_I2S);
    board_init_i2s_clock(TARGET_I2S);
#elif defined(USING_DAO) && USING_DAO
    init_dao_pins();
    board_init_dao_clock();
#else
    #error define USING_CODEC or USING_DAO
#endif

    intc_set_irq_priority(CONFIG_HPM_USBD_IRQn, 2);
    i2s_enable_dma_irq_with_priority(1);

    printf("cherry usb audio v2 speaker sample.\n");

    cherryusb_audio_init();
    speaker_init_i2s_dao_codec();

    while (1) {
        cherryusb_audio_main_task();
    }
    return 0;
}
