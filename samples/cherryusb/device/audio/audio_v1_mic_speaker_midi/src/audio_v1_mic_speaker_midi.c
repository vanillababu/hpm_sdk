/*
 * Copyright (c) 2023 HPMicro
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include "usbd_core.h"
#include "usbd_audio.h"
#include "usb_audio.h"
#include "usb_midi.h"
#include "hpm_i2s_drv.h"
#include "hpm_clock_drv.h"
#ifdef HPMSOC_HAS_HPMSDK_DMAV2
#include "hpm_dmav2_drv.h"
#else
#include "hpm_dma_drv.h"
#endif
#include "hpm_dmamux_drv.h"
#include "hpm_pdm_drv.h"
#include "hpm_dao_drv.h"
#include "hpm_gpio_drv.h"
#include "board.h"

#if defined(BOARD_BUTTON_PRESSED_VALUE)
#define APP_BUTTON_PRESSED_VALUE BOARD_BUTTON_PRESSED_VALUE
#else
#define APP_BUTTON_PRESSED_VALUE 0
#endif

#ifdef CONFIG_USB_HS
#define EP_INTERVAL 0x04
#define MIDI_EP_MPS 512
#else
#define EP_INTERVAL 0x01
#define MIDI_EP_MPS 64
#endif

#define AUDIO_VERSION 0x0100

#define AUDIO_OUT_EP 0x01
#define AUDIO_IN_EP  0x81
#define MIDI_OUT_EP 0x02
#define MIDI_IN_EP  0x82

#define AUDIO_IN_FU_ID  0x02
#define AUDIO_OUT_FU_ID 0x05

/* AUDIO Class Config */
#define AUDIO_SPEAKER_FREQ            16000U
#define AUDIO_SPEAKER_FRAME_SIZE_BYTE 2u
#define AUDIO_SPEAKER_RESOLUTION_BIT  16u
#define AUDIO_MIC_FREQ                16000U
#define AUDIO_MIC_FRAME_SIZE_BYTE     2u
#define AUDIO_MIC_RESOLUTION_BIT      16u

#define AUDIO_SAMPLE_FREQ(frq) (uint8_t)(frq), (uint8_t)((frq >> 8)), (uint8_t)((frq >> 16))

/* AudioFreq * DataSize (2 bytes) * NumChannels (Stereo: 2) */
#define AUDIO_OUT_PACKET ((uint32_t)((AUDIO_SPEAKER_FREQ * AUDIO_SPEAKER_FRAME_SIZE_BYTE * 2) / 1000))
/* 16bit(2 Bytes) Two channels(Mono:2) */
#define AUDIO_IN_PACKET ((uint32_t)((AUDIO_MIC_FREQ * AUDIO_MIC_FRAME_SIZE_BYTE * 2) / 1000))

#define AUDIO_MS_SIZ (7 + MIDI_SIZEOF_JACK_DESC + 9 + 5 + 9 + 5)

#define USB_AUDIO_CONFIG_DESC_SIZ (unsigned long)(9 +                                       \
                                                  AUDIO_AC_DESCRIPTOR_INIT_LEN(3) +         \
                                                  AUDIO_SIZEOF_AC_INPUT_TERMINAL_DESC +     \
                                                  AUDIO_SIZEOF_AC_FEATURE_UNIT_DESC(2, 1) + \
                                                  AUDIO_SIZEOF_AC_OUTPUT_TERMINAL_DESC +    \
                                                  AUDIO_SIZEOF_AC_INPUT_TERMINAL_DESC +     \
                                                  AUDIO_SIZEOF_AC_FEATURE_UNIT_DESC(2, 1) + \
                                                  AUDIO_SIZEOF_AC_OUTPUT_TERMINAL_DESC +    \
                                                  AUDIO_AS_DESCRIPTOR_INIT_LEN(1) +         \
                                                  AUDIO_AS_DESCRIPTOR_INIT_LEN(1) +         \
                                                  AUDIO_MS_STANDARD_DESCRIPTOR_INIT_LEN +   \
                                                  AUDIO_MS_SIZ)

#define AUDIO_AC_SIZ (AUDIO_SIZEOF_AC_HEADER_DESC(3) +          \
                      AUDIO_SIZEOF_AC_INPUT_TERMINAL_DESC +     \
                      AUDIO_SIZEOF_AC_FEATURE_UNIT_DESC(2, 1) + \
                      AUDIO_SIZEOF_AC_OUTPUT_TERMINAL_DESC +    \
                      AUDIO_SIZEOF_AC_INPUT_TERMINAL_DESC +     \
                      AUDIO_SIZEOF_AC_FEATURE_UNIT_DESC(2, 1) + \
                      AUDIO_SIZEOF_AC_OUTPUT_TERMINAL_DESC)

const uint8_t audio_v1_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0xef, 0x02, 0x01, USBD_VID, USBD_PID, 0x0001, 0x01),
    USB_CONFIG_DESCRIPTOR_INIT(USB_AUDIO_CONFIG_DESC_SIZ, 0x04, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    AUDIO_AC_DESCRIPTOR_INIT(0x00, 0x04, AUDIO_AC_SIZ, 0x00, 0x01, 0x02, 0x03),
    AUDIO_AC_INPUT_TERMINAL_DESCRIPTOR_INIT(0x01, AUDIO_INTERM_MIC, 0x02, 0x0003),
    AUDIO_AC_FEATURE_UNIT_DESCRIPTOR_INIT(AUDIO_IN_FU_ID, 0x01, 0x01, 0x03, 0x00, 0x00),
    AUDIO_AC_OUTPUT_TERMINAL_DESCRIPTOR_INIT(0x03, AUDIO_TERMINAL_STREAMING, AUDIO_IN_FU_ID),
    AUDIO_AC_INPUT_TERMINAL_DESCRIPTOR_INIT(0x04, AUDIO_TERMINAL_STREAMING, 0x02, 0x0003),
    AUDIO_AC_FEATURE_UNIT_DESCRIPTOR_INIT(AUDIO_OUT_FU_ID, 0x04, 0x01, 0x03, 0x00, 0x00),
    AUDIO_AC_OUTPUT_TERMINAL_DESCRIPTOR_INIT(0x06, AUDIO_OUTTERM_SPEAKER, AUDIO_OUT_FU_ID),
    AUDIO_AS_DESCRIPTOR_INIT(0x01, 0x04, 0x02, AUDIO_SPEAKER_FRAME_SIZE_BYTE, AUDIO_SPEAKER_RESOLUTION_BIT, AUDIO_OUT_EP, 0x09, AUDIO_OUT_PACKET,
                             EP_INTERVAL, AUDIO_SAMPLE_FREQ_3B(AUDIO_SPEAKER_FREQ)),
    AUDIO_AS_DESCRIPTOR_INIT(0x02, 0x03, 0x02, AUDIO_MIC_FRAME_SIZE_BYTE, AUDIO_MIC_RESOLUTION_BIT, AUDIO_IN_EP, 0x05, AUDIO_IN_PACKET,
                             EP_INTERVAL, AUDIO_SAMPLE_FREQ_3B(AUDIO_MIC_FREQ)),
    /*
     * Add Midi descriptor
     */
    AUDIO_MS_STANDARD_DESCRIPTOR_INIT(0x03, 0x02),
    MIDI_CS_HEADER_DESCRIPTOR_INIT(AUDIO_MS_SIZ),
    MIDI_JACK_DESCRIPTOR_INIT(0x01),
    /* OUT endpoint descriptor */
    0x09, 0x05, MIDI_OUT_EP, 0x02, WBVAL(MIDI_EP_MPS), 0x00, 0x00, 0x00,
    0x05, 0x25, 0x01, 0x01, 0x01,
    /* IN endpoint descriptor */
    0x09, 0x05, MIDI_IN_EP, 0x02, WBVAL(MIDI_EP_MPS), 0x00, 0x00, 0x00,
    0x05, 0x25, 0x01, 0x01, 0x03,
    /*
     * string0 descriptor
     */
    USB_LANGID_INIT(USBD_LANGID_STRING),
    /*
     * string1 descriptor
     */
    0x14,                       /* bLength */
    USB_DESCRIPTOR_TYPE_STRING, /* bDescriptorType */
    'C', 0x00,                  /* wcChar0 */
    'h', 0x00,                  /* wcChar1 */
    'e', 0x00,                  /* wcChar2 */
    'r', 0x00,                  /* wcChar3 */
    'r', 0x00,                  /* wcChar4 */
    'y', 0x00,                  /* wcChar5 */
    'U', 0x00,                  /* wcChar6 */
    'S', 0x00,                  /* wcChar7 */
    'B', 0x00,                  /* wcChar8 */
    /*
     * string2 descriptor
     */
    0x26,                       /* bLength */
    USB_DESCRIPTOR_TYPE_STRING, /* bDescriptorType */
    'C', 0x00,                  /* wcChar0 */
    'h', 0x00,                  /* wcChar1 */
    'e', 0x00,                  /* wcChar2 */
    'r', 0x00,                  /* wcChar3 */
    'r', 0x00,                  /* wcChar4 */
    'y', 0x00,                  /* wcChar5 */
    'U', 0x00,                  /* wcChar6 */
    'S', 0x00,                  /* wcChar7 */
    'B', 0x00,                  /* wcChar8 */
    ' ', 0x00,                  /* wcChar9 */
    'U', 0x00,                  /* wcChar10 */
    'A', 0x00,                  /* wcChar11 */
    'C', 0x00,                  /* wcChar12 */
    ' ', 0x00,                  /* wcChar13 */
    'M', 0x00,                  /* wcChar14 */
    'I', 0x00,                  /* wcChar15 */
    'D', 0x00,                  /* wcChar16 */
    'I', 0x00,                  /* wcChar17 */
    /*
     * string3 descriptor
     */
    0x16,                       /* bLength */
    USB_DESCRIPTOR_TYPE_STRING, /* bDescriptorType */
    '2', 0x00,                  /* wcChar0 */
    '0', 0x00,                  /* wcChar1 */
    '2', 0x00,                  /* wcChar2 */
    '3', 0x00,                  /* wcChar3 */
    '0', 0x00,                  /* wcChar4 */
    '8', 0x00,                  /* wcChar5 */
    '1', 0x00,                  /* wcChar6 */
    '0', 0x00,                  /* wcChar7 */
    '0', 0x00,                  /* wcChar8 */
    '1', 0x00,                  /* wcChar9 */
#ifdef CONFIG_USB_HS
    /*
     * device qualifier descriptor
     */
    0x0a,
    USB_DESCRIPTOR_TYPE_DEVICE_QUALIFIER,
    0x00,
    0x02,
    0x00,
    0x00,
    0x00,
    0x40,
    0x01,
    0x00,
#endif
    0x00
};

/* static variable definition */
#define SPEAKER_DMA_CHANNEL 1U
#define MIC_DMA_CHANNEL     2U
#define SPEAKER_DMAMUX_CHANNEL    DMA_SOC_CHN_TO_DMAMUX_CHN(BOARD_APP_HDMA, SPEAKER_DMA_CHANNEL)
#define MIC_DMAMUX_CHANNEL        DMA_SOC_CHN_TO_DMAMUX_CHN(BOARD_APP_HDMA, MIC_DMA_CHANNEL)

#define MIC_I2S               HPM_I2S0
#define MIC_I2S_CLK_NAME      clock_i2s0
#define MIC_I2S_DATA_LINE     I2S_DATA_LINE_0
#define MIC_I2S_RX_DMAMUX_SRC HPM_DMA_SRC_I2S0_RX

#define SPEAKER_I2S               HPM_I2S1
#define SPEAKER_I2S_CLK_NAME      clock_i2s1
#define SPEAKER_I2S_DATA_LINE     I2S_DATA_LINE_0
#define SPEAKER_I2S_TX_DMAMUX_SRC HPM_DMA_SRC_I2S1_TX

#define AUDIO_BUFFER_COUNT 32
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t s_speaker_out_buffer[AUDIO_BUFFER_COUNT][AUDIO_OUT_PACKET];
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t s_mic_in_buffer[AUDIO_BUFFER_COUNT][AUDIO_IN_PACKET];
static uint32_t s_speaker_out_buffer_size[AUDIO_BUFFER_COUNT];
static volatile bool s_speaker_rx_flag;
static volatile uint8_t s_speaker_out_buffer_front;
static volatile uint8_t s_speaker_out_buffer_rear;
static volatile bool s_speaker_dma_transfer_req;
static volatile bool s_speaker_dma_transfer_done;
static volatile uint32_t s_speaker_sample_rate;
static volatile int32_t s_speaker_volume_percent;
static volatile bool s_speaker_mute;
static volatile bool s_mic_tx_flag;
static volatile bool s_mic_ep_tx_busy_flag;
static volatile uint8_t s_mic_in_buffer_front;
static volatile uint8_t s_mic_in_buffer_rear;
static volatile bool s_mic_dma_transfer_done;
static volatile uint32_t s_mic_sample_rate;
static volatile int32_t s_mic_volume_percent;
static volatile bool s_mic_mute;

static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t s_midi_in_buffer[4];
static volatile bool s_midi_in_busy;

static struct usbd_interface intf0;
static struct usbd_interface intf1;
static struct usbd_interface intf2;
static struct usbd_interface intf3;

static void usbd_audio_in_callback(uint8_t ep, uint32_t nbytes);
static struct usbd_endpoint audio_in_ep = {
    .ep_cb = usbd_audio_in_callback,
    .ep_addr = AUDIO_IN_EP
};
static void usbd_audio_out_callback(uint8_t ep, uint32_t nbytes);
static struct usbd_endpoint audio_out_ep = {
    .ep_cb = usbd_audio_out_callback,
    .ep_addr = AUDIO_OUT_EP
};
static void usbd_midi_bulk_out_callback(uint8_t ep, uint32_t nbytes);
struct usbd_endpoint midi_out_ep = {
    .ep_cb = usbd_midi_bulk_out_callback,
    .ep_addr = MIDI_OUT_EP
};
static void usbd_midi_bulk_in_callback(uint8_t ep, uint32_t nbytes);
struct usbd_endpoint midi_in_ep = {
    .ep_cb = usbd_midi_bulk_in_callback,
    .ep_addr = MIDI_IN_EP
};

static struct audio_entity_info audio_entity_table[] = {
    {
        .bEntityId = AUDIO_IN_FU_ID,
        .bDescriptorSubtype = AUDIO_CONTROL_FEATURE_UNIT,
        .ep = AUDIO_IN_EP
    },
    {
        .bEntityId = AUDIO_OUT_FU_ID,
        .bDescriptorSubtype = AUDIO_CONTROL_FEATURE_UNIT,
        .ep = AUDIO_OUT_EP
    },
};

/* static function declaration */
static void speaker_i2s_dma_start_transfer(uint32_t addr, uint32_t size);
static void mic_i2s_dma_start_transfer(uint32_t addr, uint32_t size);
static bool speaker_out_buff_is_empty(void);
static bool mic_in_buff_is_empty(void);

/* extern function definition */
void usbd_event_handler(uint8_t event)
{
    switch (event) {
    case USBD_EVENT_RESET:
        break;
    case USBD_EVENT_CONNECTED:
        break;
    case USBD_EVENT_DISCONNECTED:
        break;
    case USBD_EVENT_RESUME:
        break;
    case USBD_EVENT_SUSPEND:
        break;
    case USBD_EVENT_CONFIGURED:
        break;
    case USBD_EVENT_SET_REMOTE_WAKEUP:
        break;
    case USBD_EVENT_CLR_REMOTE_WAKEUP:
        break;

    default:
        break;
    }
}

void usbd_audio_set_volume(uint8_t ep, uint8_t ch, int volume)
{
    (void)ch;
    if (ep == AUDIO_OUT_EP) {
        s_speaker_volume_percent = volume;
        /* Do Nothing */
    } else if (ep == AUDIO_IN_EP) {
        s_mic_volume_percent = volume;
        /* Do Nothing */
    } else {
        ;
    }
}

int usbd_audio_get_volume(uint8_t ep, uint8_t ch)
{
    (void)ch;
    int volume = 0;

    if (ep == AUDIO_OUT_EP) {
        volume = s_speaker_volume_percent;
    } else if (ep == AUDIO_IN_EP) {
        volume = s_mic_volume_percent;
    } else {
        ;
    }

    return volume;
}

void usbd_audio_set_mute(uint8_t ep, uint8_t ch, bool mute)
{
    (void)ch;
    if (ep == AUDIO_OUT_EP) {
        s_speaker_mute = mute;
        if (s_speaker_mute) {
            dao_stop(HPM_DAO);
        } else {
            dao_start(HPM_DAO);
        }
    } else if (ep == AUDIO_IN_EP) {
        s_mic_mute = mute;
        if (s_mic_mute) {
            pdm_stop(HPM_PDM);
        } else {
            pdm_start(HPM_PDM);
        }
    } else {
        ;
    }
}

bool usbd_audio_get_mute(uint8_t ep, uint8_t ch)
{
    (void)ch;
    bool mute = false;

    if (ep == AUDIO_OUT_EP) {
        mute = s_speaker_mute;
    } else if (ep == AUDIO_IN_EP) {
        mute = s_mic_mute;
    } else {
        ;
    }

    return mute;
}

void usbd_audio_set_sampling_freq(uint8_t ep, uint32_t sampling_freq)
{
    if (ep == AUDIO_OUT_EP) {
        s_speaker_sample_rate = sampling_freq;
    } else if (ep == AUDIO_IN_EP) {
        s_mic_sample_rate = sampling_freq;
    } else {
        ;
    }
}

uint32_t usbd_audio_get_sampling_freq(uint8_t ep)
{
    uint32_t freq = 0;

    if (ep == AUDIO_OUT_EP) {
        freq = s_speaker_sample_rate;
    } else if (ep == AUDIO_IN_EP) {
        freq = s_mic_sample_rate;
    } else {
        ;
    }

    return freq;
}

void usbd_audio_open(uint8_t intf)
{
    if (intf == 1) {
        s_speaker_rx_flag = 1;
        s_speaker_out_buffer_front = 0;
        s_speaker_out_buffer_rear = 0;
        s_speaker_dma_transfer_req = true;
        /* setup first out ep read transfer */
        usbd_ep_start_read(AUDIO_OUT_EP, (uint8_t *)&s_speaker_out_buffer[s_speaker_out_buffer_rear][0], AUDIO_OUT_PACKET);
        dao_start(HPM_DAO);
        printf("OPEN SPEAKER\r\n");
    } else {
        s_mic_tx_flag = 1;
        s_mic_ep_tx_busy_flag = false;
        s_mic_in_buffer_front = 0;
        s_mic_in_buffer_rear = 0;
        s_mic_dma_transfer_done = false;
        pdm_start(HPM_PDM);
        mic_i2s_dma_start_transfer((uint32_t)&s_mic_in_buffer[s_mic_in_buffer_rear][0], AUDIO_IN_PACKET);
        printf("OPEN MIC\r\n");
    }
}

void usbd_audio_close(uint8_t intf)
{
    if (intf == 1) {
        s_speaker_rx_flag = 0;
        dao_stop(HPM_DAO);
        printf("CLOSE SPEAKER\r\n");
    } else {
        s_mic_tx_flag = 0;
        pdm_stop(HPM_PDM);
        printf("CLOSE MIC\r\n");
    }
}

void audio_init(void)
{
    usbd_desc_register(audio_v1_descriptor);
    usbd_add_interface(usbd_audio_init_intf(&intf0, AUDIO_VERSION, audio_entity_table, 2));
    usbd_add_interface(usbd_audio_init_intf(&intf1, AUDIO_VERSION, audio_entity_table, 2));
    usbd_add_interface(usbd_audio_init_intf(&intf2, AUDIO_VERSION, audio_entity_table, 2));
    usbd_add_interface(&intf3);
    usbd_add_endpoint(&audio_in_ep);
    usbd_add_endpoint(&audio_out_ep);
    usbd_add_endpoint(&midi_out_ep);
    usbd_add_endpoint(&midi_in_ep);

    usbd_initialize();
}

void audio_task(void)
{
    if (s_speaker_rx_flag) {
        if (!speaker_out_buff_is_empty()) {
            if (s_speaker_dma_transfer_req) {
                s_speaker_dma_transfer_req = false;
                speaker_i2s_dma_start_transfer((uint32_t)&s_speaker_out_buffer[s_speaker_out_buffer_front][0],
                                                s_speaker_out_buffer_size[s_speaker_out_buffer_front]);
                s_speaker_out_buffer_front++;
                if (s_speaker_out_buffer_front >= AUDIO_BUFFER_COUNT) {
                    s_speaker_out_buffer_front = 0;
                }
            } else if (s_speaker_dma_transfer_done) {
                s_speaker_dma_transfer_done = false;
                speaker_i2s_dma_start_transfer((uint32_t)&s_speaker_out_buffer[s_speaker_out_buffer_front][0],
                                                s_speaker_out_buffer_size[s_speaker_out_buffer_front]);
                s_speaker_out_buffer_front++;
                if (s_speaker_out_buffer_front >= AUDIO_BUFFER_COUNT) {
                    s_speaker_out_buffer_front = 0;
                }
            } else {
                ;
            }
        }
    }

    if (s_mic_tx_flag) {
        if (s_mic_dma_transfer_done) {
            s_mic_dma_transfer_done = false;
            s_mic_in_buffer_rear++;
            if (s_mic_in_buffer_rear >= AUDIO_BUFFER_COUNT) {
                s_mic_in_buffer_rear = 0;
            }
            mic_i2s_dma_start_transfer((uint32_t)&s_mic_in_buffer[s_mic_in_buffer_rear][0], AUDIO_IN_PACKET);
        }

        if (!mic_in_buff_is_empty()) {
            if (!s_mic_ep_tx_busy_flag) {
                s_mic_ep_tx_busy_flag = true;
                usbd_ep_start_write(AUDIO_IN_EP, &s_mic_in_buffer[s_mic_in_buffer_front][0], AUDIO_IN_PACKET);
                s_mic_in_buffer_front++;
                if (s_mic_in_buffer_front >= AUDIO_BUFFER_COUNT) {
                    s_mic_in_buffer_front = 0;
                }
            }
        }
    }
}

void midi_task(void)
{
    static uint8_t s_midi_state;
    static uint8_t s_note_pos;
    static const uint8_t s_note_sequence[] = {
        74, 78, 81, 86, 90, 93, 98, 102, 57, 61, 66, 69, 73, 78, 81, 85, 88, 92, 97, 100, 97, 92, 88, 85, 81, 78,
        74, 69, 66, 62, 57, 62, 66, 69, 74, 78, 81, 86, 90, 93, 97, 102, 97, 93, 90, 85, 81, 78, 73, 68, 64, 61,
        56, 61, 64, 68, 74, 78, 81, 86, 90, 93, 98, 102
    };

    const uint8_t cable_num = 0; /* MIDI jack associated with USB endpoint */
    const uint8_t channel = 0;   /* 0 for channel 1 */
    int ret;

    switch (s_midi_state) {
    case 1:
        if (s_midi_in_busy == false) {
            if (gpio_read_pin(BOARD_APP_GPIO_CTRL, BOARD_APP_GPIO_INDEX, BOARD_APP_GPIO_PIN) != APP_BUTTON_PRESSED_VALUE) {
                s_midi_in_buffer[0] = (cable_num << 4) | MIDI_CIN_NOTE_OFF;
                s_midi_in_buffer[1] = NoteOff | channel;
                s_midi_in_buffer[2] = s_note_sequence[s_note_pos];
                s_midi_in_buffer[3] = 0;  /* velocity */
                s_midi_in_busy = true;
                ret = usbd_ep_start_write(MIDI_IN_EP, s_midi_in_buffer, 4);
                if (ret < 0) {
                    s_midi_in_busy = false;
                    printf("midi ep write error1\n");
                } else {
                    s_note_pos++;
                    if (s_note_pos >= sizeof(s_note_sequence)) {
                        s_note_pos = 0;
                    }
                    s_midi_state = 0;
                }
            }
        }
        break;

    case 0:
    default:
        if (s_midi_in_busy == false) {
            if (gpio_read_pin(BOARD_APP_GPIO_CTRL, BOARD_APP_GPIO_INDEX, BOARD_APP_GPIO_PIN) == APP_BUTTON_PRESSED_VALUE) {
                s_midi_in_buffer[0] = (cable_num << 4) | MIDI_CIN_NOTE_ON;
                s_midi_in_buffer[1] = NoteOn | channel;
                s_midi_in_buffer[2] = s_note_sequence[s_note_pos];
                s_midi_in_buffer[3] = 100;  /* velocity */
                s_midi_in_busy = true;
                ret = usbd_ep_start_write(MIDI_IN_EP, s_midi_in_buffer, 4);
                if (ret < 0) {
                    s_midi_in_busy = false;
                    printf("midi ep write error0\n");
                } else {
                    s_midi_state = 1;
                }
            }
            break;
        }
    }
}

void i2s_enable_dma_irq_with_priority(int32_t priority)
{
    i2s_enable_tx_dma_request(SPEAKER_I2S);
    i2s_enable_rx_dma_request(MIC_I2S);
    dmamux_config(BOARD_APP_DMAMUX, SPEAKER_DMAMUX_CHANNEL, SPEAKER_I2S_TX_DMAMUX_SRC, true);
    dmamux_config(BOARD_APP_DMAMUX, MIC_DMAMUX_CHANNEL, MIC_I2S_RX_DMAMUX_SRC, true);

    intc_m_enable_irq_with_priority(BOARD_APP_HDMA_IRQ, priority);
}

void mic_init_i2s_pdm(void)
{
    i2s_config_t i2s_config;
    i2s_transfer_config_t transfer;
    pdm_config_t pdm_config;
    uint32_t i2s_mclk_hz;

    i2s_get_default_config(MIC_I2S, &i2s_config);
    i2s_init(MIC_I2S, &i2s_config);

    i2s_get_default_transfer_config_for_pdm(&transfer);
    transfer.data_line = MIC_I2S_DATA_LINE;
    transfer.sample_rate = AUDIO_MIC_FREQ;
    transfer.channel_slot_mask = BOARD_PDM_DUAL_CHANNEL_MASK; /* 2 channels */

    s_mic_sample_rate = transfer.sample_rate;

    i2s_mclk_hz = clock_get_frequency(MIC_I2S_CLK_NAME);

    if (status_success != i2s_config_rx(MIC_I2S, i2s_mclk_hz, &transfer)) {
        printf("MIC I2S1 config failed\n");
        while (1) {
            ;
        }
    }
    i2s_start(MIC_I2S);

    pdm_get_default_config(HPM_PDM, &pdm_config);
    pdm_init(HPM_PDM, &pdm_config);
}

void speaker_init_i2s_dao(void)
{
    i2s_config_t i2s_config;
    i2s_transfer_config_t transfer;
    dao_config_t dao_config;
    uint32_t i2s_mclk_hz;

    i2s_get_default_config(SPEAKER_I2S, &i2s_config);
    i2s_init(SPEAKER_I2S, &i2s_config);

    i2s_get_default_transfer_config_for_dao(&transfer);
    transfer.data_line = SPEAKER_I2S_DATA_LINE;
    transfer.sample_rate = AUDIO_SPEAKER_FREQ;
    transfer.audio_depth = AUDIO_SPEAKER_RESOLUTION_BIT;
    transfer.channel_num_per_frame = 2; /* non TDM mode, channel num fix to 2. */
    transfer.channel_slot_mask = 0x3;   /* data from hpm_wav_decode API is 2 channels */

    s_speaker_sample_rate = transfer.sample_rate;

    i2s_mclk_hz = clock_get_frequency(SPEAKER_I2S_CLK_NAME);

    if (status_success != i2s_config_tx(SPEAKER_I2S, i2s_mclk_hz, &transfer)) {
        printf("SPEAKER I2S config failed\n");
        while (1) {
            ;
        }
    }
    i2s_start(SPEAKER_I2S);

    dao_get_default_config(HPM_DAO, &dao_config);
    dao_config.enable_mono_output = true;
    dao_init(HPM_DAO, &dao_config);
}

void isr_dma(void)
{
    volatile uint32_t speaker_status;
    volatile uint32_t mic_status;

    speaker_status = dma_check_transfer_status(BOARD_APP_HDMA, SPEAKER_DMA_CHANNEL);
    mic_status = dma_check_transfer_status(BOARD_APP_HDMA, MIC_DMA_CHANNEL);

    if (0 != (speaker_status & DMA_CHANNEL_STATUS_TC)) {
        s_speaker_dma_transfer_done = true;
    }

    if (0 != (mic_status & DMA_CHANNEL_STATUS_TC)) {
        s_mic_dma_transfer_done = true;
    }
}
SDK_DECLARE_EXT_ISR_M(BOARD_APP_HDMA_IRQ, isr_dma)

/* static function definition */
static void usbd_audio_out_callback(uint8_t ep, uint32_t nbytes)
{
    if (s_speaker_rx_flag) {
        s_speaker_out_buffer_size[s_speaker_out_buffer_rear] = nbytes;
        s_speaker_out_buffer_rear++;
        if (s_speaker_out_buffer_rear >= AUDIO_BUFFER_COUNT) {
            s_speaker_out_buffer_rear = 0;
        }
        usbd_ep_start_read(ep, &s_speaker_out_buffer[s_speaker_out_buffer_rear][0], AUDIO_OUT_PACKET);
    }
}

static void usbd_audio_in_callback(uint8_t ep, uint32_t nbytes)
{
    (void)ep;
    (void)nbytes;
    s_mic_ep_tx_busy_flag = false;
}

static void usbd_midi_bulk_out_callback(uint8_t ep, uint32_t nbytes)
{
    (void)ep;
    (void)nbytes;
}

static void usbd_midi_bulk_in_callback(uint8_t ep, uint32_t nbytes)
{
    (void)ep;
    (void)nbytes;
    s_midi_in_busy = false;
}

static void speaker_i2s_dma_start_transfer(uint32_t addr, uint32_t size)
{
    dma_channel_config_t ch_config = { 0 };

    dma_default_channel_config(BOARD_APP_HDMA, &ch_config);
    ch_config.src_addr = core_local_mem_to_sys_address(HPM_CORE0, addr);
    ch_config.dst_addr = (uint32_t)(&SPEAKER_I2S->TXD[SPEAKER_I2S_DATA_LINE]) + 2u;
    ch_config.src_width = DMA_TRANSFER_WIDTH_HALF_WORD;
    ch_config.dst_width = DMA_TRANSFER_WIDTH_HALF_WORD;
    ch_config.src_addr_ctrl = DMA_ADDRESS_CONTROL_INCREMENT;
    ch_config.dst_addr_ctrl = DMA_ADDRESS_CONTROL_FIXED;
    ch_config.size_in_byte = DMA_ALIGN_HALF_WORD(size);
    ch_config.src_mode = DMA_HANDSHAKE_MODE_NORMAL;
    ch_config.dst_mode = DMA_HANDSHAKE_MODE_HANDSHAKE;
    ch_config.src_burst_size = DMA_NUM_TRANSFER_PER_BURST_1T;

    if (status_success != dma_setup_channel(BOARD_APP_HDMA, SPEAKER_DMA_CHANNEL, &ch_config, true)) {
        printf(" dma setup channel failed\n");
    }
}

static void mic_i2s_dma_start_transfer(uint32_t addr, uint32_t size)
{
    dma_channel_config_t ch_config = { 0 };

    dma_default_channel_config(BOARD_APP_HDMA, &ch_config);
    ch_config.src_addr = (uint32_t)(&MIC_I2S->RXD[MIC_I2S_DATA_LINE]) + 2u;
    ch_config.dst_addr = core_local_mem_to_sys_address(HPM_CORE0, addr);
    ch_config.src_width = DMA_TRANSFER_WIDTH_HALF_WORD;
    ch_config.dst_width = DMA_TRANSFER_WIDTH_HALF_WORD;
    ch_config.src_addr_ctrl = DMA_ADDRESS_CONTROL_FIXED;
    ch_config.dst_addr_ctrl = DMA_ADDRESS_CONTROL_INCREMENT;
    ch_config.size_in_byte = DMA_ALIGN_HALF_WORD(size);
    ch_config.src_mode = DMA_HANDSHAKE_MODE_HANDSHAKE;
    ch_config.dst_mode = DMA_HANDSHAKE_MODE_NORMAL;
    ch_config.src_burst_size = DMA_NUM_TRANSFER_PER_BURST_1T;

    if (status_success != dma_setup_channel(BOARD_APP_HDMA, MIC_DMA_CHANNEL, &ch_config, true)) {
        printf(" pdm dma setup channel failed\n");
    }
}

static bool speaker_out_buff_is_empty(void)
{
    bool empty = false;

    if (s_speaker_out_buffer_front == s_speaker_out_buffer_rear) {
        empty = true;
    }

    return empty;
}

static bool mic_in_buff_is_empty(void)
{
    bool empty = false;

    if (s_mic_in_buffer_front == s_mic_in_buffer_rear) {
        empty = true;
    }

    return empty;
}
