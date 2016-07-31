/*******************************************************************************
*   Ledger Blue
*   (c) 2016 Ledger
*
*  Licensed under the Apache License, Version 2.0 (the "License");
*  you may not use this file except in compliance with the License.
*  You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
*  Unless required by applicable law or agreed to in writing, software
*  distributed under the License is distributed on an "AS IS" BASIS,
*  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*  See the License for the specific language governing permissions and
*  limitations under the License.
********************************************************************************/

#include "os.h"
#include "cx.h"
#include <stdbool.h>

#include "os_io_seproxyhal.h"
#include "string.h"

unsigned int io_seproxyhal_touch_exit(const bagl_element_t *e);
unsigned int io_seproxyhal_touch_sign_ok(const bagl_element_t *e);
unsigned int io_seproxyhal_touch_sign_cancel(const bagl_element_t *e);
unsigned int io_seproxyhal_touch_address_ok(const bagl_element_t *e);
unsigned int io_seproxyhal_touch_address_cancel(const bagl_element_t *e);

#define MAX_BIP32_PATH 10

#define CLA 0x80
#define INS_GET_PUBLIC_KEY 0x02
#define INS_SIGN_BLOB 0x04
#define P1_FIRST 0x00
#define P1_NEXT 0x01
#define P1_LAST_MARKER 0x80

#define OFFSET_CLA 0
#define OFFSET_INS 1
#define OFFSET_P1 2
#define OFFSET_P2 3
#define OFFSET_LC 4
#define OFFSET_CDATA 5

unsigned char G_io_seproxyhal_spi_buffer[IO_SEPROXYHAL_BUFFER_SIZE_B];

ux_state_t ux;


typedef struct operationContext_t {
    uint8_t pathLength;
    uint32_t bip32Path[MAX_BIP32_PATH];
    cx_sha256_t hash;
    cx_ecfp_public_key_t publicKey;
} operationContext_t;

char keyPath[200];
operationContext_t operationContext;


bagl_element_t const ui_address_blue[] = {
    {{BAGL_RECTANGLE, 0x00, 0, 60, 320, 420, 0, 0, BAGL_FILL, 0xf9f9f9,
      0xf9f9f9, 0, 0},
     NULL,
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},

    // type                                 id    x    y    w    h    s  r  fill
    // fg        bg        font icon   text, out, over, touch
    {{BAGL_RECTANGLE, 0x00, 0, 0, 320, 60, 0, 0, BAGL_FILL, 0x1d2028, 0x1d2028,
      0, 0},
     NULL,
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},
    {{BAGL_LABEL, 0x00, 20, 0, 320, 60, 0, 0, BAGL_FILL, 0xFFFFFF, 0x1d2028,
      BAGL_FONT_OPEN_SANS_LIGHT_14px | BAGL_FONT_ALIGNMENT_MIDDLE, 0},
     "SSH Agent",
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},

    {{BAGL_BUTTON | BAGL_FLAG_TOUCHABLE, 0x00, 35, 385, 120, 40, 0, 6,
      BAGL_FILL, 0xcccccc, 0xF9F9F9,
      BAGL_FONT_OPEN_SANS_LIGHT_14px | BAGL_FONT_ALIGNMENT_CENTER |
          BAGL_FONT_ALIGNMENT_MIDDLE,
      0},
     "CANCEL",
     0,
     0x37ae99,
     0xF9F9F9,
     io_seproxyhal_touch_address_cancel,
     NULL,
     NULL},
    {{BAGL_BUTTON | BAGL_FLAG_TOUCHABLE, 0x00, 165, 385, 120, 40, 0, 6,
      BAGL_FILL, 0x41ccb4, 0xF9F9F9,
      BAGL_FONT_OPEN_SANS_LIGHT_14px | BAGL_FONT_ALIGNMENT_CENTER |
          BAGL_FONT_ALIGNMENT_MIDDLE,
      0},
     "CONFIRM",
     0,
     0x37ae99,
     0xF9F9F9,
     io_seproxyhal_touch_address_ok,
     NULL,
     NULL},

    {{BAGL_LABEL, 0x00, 0, 147, 320, 32, 0, 0, 0, 0x000000, 0xF9F9F9,
      BAGL_FONT_OPEN_SANS_LIGHT_14px | BAGL_FONT_ALIGNMENT_CENTER, 0},
     "Get public key for path",
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},
    {{BAGL_LABEL, 0x00, 0, 280, 320, 33, 0, 0, 0, 0x000000, 0xF9F9F9,
      BAGL_FONT_OPEN_SANS_LIGHT_16px | BAGL_FONT_ALIGNMENT_CENTER, 0},
     (const char *)keyPath,
     0,
     0,
     0,
     NULL,
     NULL,
     NULL}};

unsigned int ui_address_blue_button(unsigned int button_mask, unsigned int button_mask_counter) {
	return 0;
}

// UI to approve or deny the signature proposal
static const bagl_element_t const ui_approval_blue[] = {
    {{BAGL_RECTANGLE, 0x00, 0, 60, 320, 420, 0, 0, BAGL_FILL, 0xf9f9f9,
      0xf9f9f9, 0, 0},
     NULL,
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},

    // type                                 id    x    y    w    h    s  r  fill
    // fg        bg        font icon   text, out, over, touch
    {{BAGL_RECTANGLE, 0x00, 0, 0, 320, 60, 0, 0, BAGL_FILL, 0x1d2028, 0x1d2028,
      0, 0},
     NULL,
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},
    {{BAGL_LABEL, 0x00, 20, 0, 320, 60, 0, 0, BAGL_FILL, 0xFFFFFF, 0x1d2028,
      BAGL_FONT_OPEN_SANS_LIGHT_14px | BAGL_FONT_ALIGNMENT_MIDDLE, 0},
     "SSH Agent",
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},

    {{BAGL_BUTTON | BAGL_FLAG_TOUCHABLE, 0x00, 35, 385, 120, 40, 0, 6,
      BAGL_FILL, 0xcccccc, 0xF9F9F9,
      BAGL_FONT_OPEN_SANS_LIGHT_14px | BAGL_FONT_ALIGNMENT_CENTER |
          BAGL_FONT_ALIGNMENT_MIDDLE,
      0},
     "CANCEL",
     0,
     0x37ae99,
     0xF9F9F9,
     io_seproxyhal_touch_sign_cancel,
     NULL,
     NULL},
    {{BAGL_BUTTON | BAGL_FLAG_TOUCHABLE, 0x00, 165, 385, 120, 40, 0, 6,
      BAGL_FILL, 0x41ccb4, 0xF9F9F9,
      BAGL_FONT_OPEN_SANS_LIGHT_14px | BAGL_FONT_ALIGNMENT_CENTER |
          BAGL_FONT_ALIGNMENT_MIDDLE,
      0},
     "CONFIRM",
     0,
     0x37ae99,
     0xF9F9F9,
     io_seproxyhal_touch_sign_ok,
     NULL,
     NULL},

    {{BAGL_LABEL, 0x00, 0, 87, 320, 32, 0, 0, 0, 0x000000, 0xF9F9F9,
      BAGL_FONT_OPEN_SANS_LIGHT_14px | BAGL_FONT_ALIGNMENT_CENTER, 0},
     "Confirm SSH authentication with key",
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},
    {{BAGL_LABEL, 0x00, 0, 125, 320, 33, 0, 0, 0, 0x000000, 0xF9F9F9,
      BAGL_FONT_OPEN_SANS_LIGHT_16px | BAGL_FONT_ALIGNMENT_CENTER, 0},
     (const char *)keyPath,
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},
};

unsigned int ui_approval_blue_button(unsigned int button_mask, unsigned int button_mask_counter) {
	return 0;
}

// UI displayed when no signature proposal has been received
static const bagl_element_t const ui_idle_blue[] = {
    {{BAGL_RECTANGLE, 0x00, 0, 60, 320, 420, 0, 0, BAGL_FILL, 0xf9f9f9,
      0xf9f9f9, 0, 0},
     NULL,
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},

    {{BAGL_RECTANGLE, 0x00, 0, 0, 320, 60, 0, 0, BAGL_FILL, 0x1d2028, 0x1d2028,
      0, 0},
     NULL,
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},

    {{BAGL_LABEL, 0x00, 20, 0, 320, 60, 0, 0, BAGL_FILL, 0xFFFFFF, 0x1d2028,
      BAGL_FONT_OPEN_SANS_LIGHT_14px | BAGL_FONT_ALIGNMENT_MIDDLE, 0},
     "SSH Agent",
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},

    {{BAGL_BUTTON | BAGL_FLAG_TOUCHABLE, 0x00, 190, 215, 120, 40, 0, 6,
      BAGL_FILL, 0x41ccb4, 0xF9F9F9,
      BAGL_FONT_OPEN_SANS_LIGHT_14px | BAGL_FONT_ALIGNMENT_CENTER |
          BAGL_FONT_ALIGNMENT_MIDDLE,
      0},
     "Exit",
     0,
     0x37ae99,
     0xF9F9F9,
     io_seproxyhal_touch_exit,
     NULL,
     NULL}

};

unsigned int ui_idle_blue_button(unsigned int button_mask, unsigned int button_mask_counter) {
	return 0;
}


const bagl_element_t ui_idle_nanos[] = {
  // type                               userid    x    y   w    h  str rad fill      fg        bg      fid iid  txt   touchparams...       ]
  {{BAGL_RECTANGLE                      , 0x00,   0,   0, 128,  32, 0, 0, BAGL_FILL, 0x000000, 0xFFFFFF, 0, 0}, NULL, 0, 0, 0, NULL, NULL, NULL},
  
  {{BAGL_LABELINE                       , 0x00,   0,  12, 128,  32, 0, 0, 0        , 0xFFFFFF, 0x000000, BAGL_FONT_OPEN_SANS_EXTRABOLD_11px|BAGL_FONT_ALIGNMENT_CENTER, 0  }, "SSH Agent", 0, 0, 0, NULL, NULL, NULL },
  //{{BAGL_LABELINE                       , 0x02,   0,  26, 128,  32, 0, 0, 0        , 0xFFFFFF, 0x000000, BAGL_FONT_OPEN_SANS_REGULAR_11px|BAGL_FONT_ALIGNMENT_CENTER, 0  }, "Waiting for requests...", 0, 0, 0, NULL, NULL, NULL },

  {{BAGL_ICON                           , 0x00,   3,  12,   7,   7, 0, 0, 0        , 0xFFFFFF, 0x000000, 0, BAGL_GLYPH_ICON_CROSS  }, NULL, 0, 0, 0, NULL, NULL, NULL },
};
unsigned int ui_idle_nanos_button(unsigned int button_mask, unsigned int button_mask_counter);

const bagl_element_t ui_address_nanos[] = {
  // type                               userid    x    y   w    h  str rad fill      fg        bg      fid iid  txt   touchparams...       ]
  {{BAGL_RECTANGLE                      , 0x00,   0,   0, 128,  32, 0, 0, BAGL_FILL, 0x000000, 0xFFFFFF, 0, 0}, NULL, 0, 0, 0, NULL, NULL, NULL},
  
  {{BAGL_LABELINE                       , 0x01,   0,  12, 128,  32, 0, 0, 0        , 0xFFFFFF, 0x000000, BAGL_FONT_OPEN_SANS_EXTRABOLD_11px|BAGL_FONT_ALIGNMENT_CENTER, 0  }, "SSH Agent", 0, 0, 0, NULL, NULL, NULL },
  {{BAGL_LABELINE                       , 0x02,   0,  26, 128,  32, 0, 0, 0        , 0xFFFFFF, 0x000000, BAGL_FONT_OPEN_SANS_REGULAR_11px|BAGL_FONT_ALIGNMENT_CENTER, 0  }, "Provide public key?", 0, 0, 0, NULL, NULL, NULL },

  {{BAGL_ICON                           , 0x00,   3,  12,   7,   7, 0, 0, 0        , 0xFFFFFF, 0x000000, 0, BAGL_GLYPH_ICON_CROSS  }, NULL, 0, 0, 0, NULL, NULL, NULL },
  {{BAGL_ICON                           , 0x00, 117,  13,   8,   6, 0, 0, 0        , 0xFFFFFF, 0x000000, 0, BAGL_GLYPH_ICON_CHECK  }, NULL, 0, 0, 0, NULL, NULL, NULL },
};
unsigned int ui_address_nanos_button(unsigned int button_mask, unsigned int button_mask_counter);

const bagl_element_t ui_approval_nanos[] = {
  // type                               userid    x    y   w    h  str rad fill      fg        bg      fid iid  txt   touchparams...       ]
  {{BAGL_RECTANGLE                      , 0x00,   0,   0, 128,  32, 0, 0, BAGL_FILL, 0x000000, 0xFFFFFF, 0, 0}, NULL, 0, 0, 0, NULL, NULL, NULL},
  
  {{BAGL_LABELINE                       , 0x01,   0,  12, 128,  32, 0, 0, 0        , 0xFFFFFF, 0x000000, BAGL_FONT_OPEN_SANS_EXTRABOLD_11px|BAGL_FONT_ALIGNMENT_CENTER, 0  }, "SSH Agent", 0, 0, 0, NULL, NULL, NULL },
  {{BAGL_LABELINE                       , 0x02,   0,  26, 128,  32, 0, 0, 0        , 0xFFFFFF, 0x000000, BAGL_FONT_OPEN_SANS_REGULAR_11px|BAGL_FONT_ALIGNMENT_CENTER, 0  }, "Authenticate?", 0, 0, 0, NULL, NULL, NULL },

  {{BAGL_ICON                           , 0x00,   3,  12,   7,   7, 0, 0, 0        , 0xFFFFFF, 0x000000, 0, BAGL_GLYPH_ICON_CROSS  }, NULL, 0, 0, 0, NULL, NULL, NULL },
  {{BAGL_ICON                           , 0x00, 117,  13,   8,   6, 0, 0, 0        , 0xFFFFFF, 0x000000, 0, BAGL_GLYPH_ICON_CHECK  }, NULL, 0, 0, 0, NULL, NULL, NULL },
};
unsigned int ui_approval_nanos_button(unsigned int button_mask, unsigned int button_mask_counter);

void ui_idle(void) {
    if (os_seph_features() & SEPROXYHAL_TAG_SESSION_START_EVENT_FEATURE_SCREEN_BIG) {
        UX_DISPLAY(ui_idle_blue, NULL);
    }
    else {
        UX_DISPLAY(ui_idle_nanos, NULL);   
    }
}

uint32_t path_item_to_string(char *dest, uint32_t number) {
    uint32_t offset = 0;
    uint32_t startOffset = 0, destOffset = 0;
    uint8_t i;
    uint8_t tmp[11];
    bool hardened = ((number & 0x80000000) != 0);
    number &= 0x7FFFFFFF;
    uint32_t divIndex = 0x3b9aca00;
    while (divIndex != 0) {
        tmp[offset++] = '0' + ((number / divIndex) % 10);
        divIndex /= 10;
    }
    tmp[offset] = '\0';
    while ((tmp[startOffset] == '0') && (startOffset < offset)) {
        startOffset++;
    }
    if (startOffset == offset) {
        dest[destOffset++] = '0';
    } else {
        for (i = startOffset; i < offset; i++) {
            dest[destOffset++] = tmp[i];
        }
    }
    if (hardened) {
        dest[destOffset++] = '\'';
    }
    dest[destOffset++] = '\0';
    return destOffset;
}

uint32_t path_to_string(char *dest) {
    uint8_t i;
    uint32_t offset = 0;
    for (i = 0; i < operationContext.pathLength; i++) {
        uint32_t length =
            path_item_to_string(dest + offset, operationContext.bip32Path[i]);
        offset += length;
        offset--;
        if (i != operationContext.pathLength - 1) {
            dest[offset++] = '/';
        }
    }
    dest[offset++] = '\0';
    return offset;
}

// TODO : replace with 1.2 SDK
int app_cx_ecfp_init_private_key(cx_curve_t curve, unsigned char WIDE *rawkey,
                                 int key_len, cx_ecfp_private_key_t *key) {
    key->curve = curve;
    key->d_len = key_len;
    if (rawkey != NULL) {
        os_memmove(key->d, rawkey, key_len);
    }
    return key_len;
}
unsigned int io_seproxyhal_touch_exit(const bagl_element_t *e) {
    // Go back to the dashboard
    os_sched_exit(0);
    return 0; // do not redraw the widget
}

unsigned int ui_idle_nanos_button(unsigned int button_mask, unsigned int button_mask_counter) {
    switch(button_mask) {
        case BUTTON_EVT_RELEASED|BUTTON_LEFT: // EXIT
		    io_seproxyhal_touch_exit(NULL);
            break;
    }
    return 0;
}

unsigned int io_seproxyhal_touch_sign_ok(const bagl_element_t *e) {
    uint8_t privateKeyData[32];
    uint8_t hash[32];
    cx_ecfp_private_key_t privateKey;
    uint32_t tx = 0;
    cx_hash(&operationContext.hash.header, CX_LAST, hash, 0, hash);
    os_perso_derive_seed_bip32(operationContext.bip32Path,
                               operationContext.pathLength, privateKeyData,
                               NULL);
    app_cx_ecfp_init_private_key(CX_CURVE_256R1, privateKeyData, 32,
                                 &privateKey);
    os_memset(privateKeyData, 0, sizeof(privateKeyData));
    // TODO 1.2 : support deterministic signature also with prime256v1
    tx = cx_ecdsa_sign(&privateKey, CX_RND_TRNG | CX_LAST, CX_NONE, hash,
                       sizeof(hash), G_io_apdu_buffer);
    os_memset(&privateKey, 0, sizeof(privateKey));
    G_io_apdu_buffer[tx++] = 0x90;
    G_io_apdu_buffer[tx++] = 0x00;
    // Send back the response, do not restart the event loop
    io_exchange(CHANNEL_APDU | IO_RETURN_AFTER_TX, tx);
    // Display back the original UX
    ui_idle();
    return 0; // do not redraw the widget
}

unsigned int io_seproxyhal_touch_sign_cancel(const bagl_element_t *e) {
    G_io_apdu_buffer[0] = 0x69;
    G_io_apdu_buffer[1] = 0x85;
    // Send back the response, do not restart the event loop
    io_exchange(CHANNEL_APDU | IO_RETURN_AFTER_TX, 2);
    // Display back the original UX
    ui_idle();
    return 0; // do not redraw the widget
}


unsigned int ui_approval_nanos_button(unsigned int button_mask, unsigned int button_mask_counter) {
    switch(button_mask) {
        case BUTTON_EVT_RELEASED|BUTTON_LEFT: // CANCEL
			io_seproxyhal_touch_sign_cancel(NULL);
            break;

        case BUTTON_EVT_RELEASED|BUTTON_RIGHT: { // OK
			io_seproxyhal_touch_sign_ok(NULL);
            break;
        }
    }    
    return 0;
}

unsigned int io_seproxyhal_touch_address_ok(const bagl_element_t *e) {
    uint32_t tx = 0;
    G_io_apdu_buffer[tx++] = 65;
    os_memmove(G_io_apdu_buffer + tx, operationContext.publicKey.W, 65);
    tx += 65;
    G_io_apdu_buffer[tx++] = 0x90;
    G_io_apdu_buffer[tx++] = 0x00;
    // Send back the response, do not restart the event loop
    io_exchange(CHANNEL_APDU | IO_RETURN_AFTER_TX, tx);
    // Display back the original UX
    ui_idle();
    return 0; // do not redraw the widget
}

unsigned int io_seproxyhal_touch_address_cancel(const bagl_element_t *e) {
    G_io_apdu_buffer[0] = 0x69;
    G_io_apdu_buffer[1] = 0x85;
    // Send back the response, do not restart the event loop
    io_exchange(CHANNEL_APDU | IO_RETURN_AFTER_TX, 2);
    // Display back the original UX
    ui_idle();
    return 0; // do not redraw the widget
}


unsigned int ui_address_nanos_button(unsigned int button_mask, unsigned int button_mask_counter) {
    switch(button_mask) {
        case BUTTON_EVT_RELEASED|BUTTON_LEFT: // CANCEL
			io_seproxyhal_touch_address_cancel(NULL);
            break;

        case BUTTON_EVT_RELEASED|BUTTON_RIGHT: { // OK
            io_seproxyhal_touch_address_ok(NULL);
            break;
        }
    }
    return 0;
}

unsigned short io_exchange_al(unsigned char channel, unsigned short tx_len) {
    switch (channel & ~(IO_FLAGS)) {
    case CHANNEL_KEYBOARD:
        break;

    // multiplexed io exchange over a SPI channel and TLV encapsulated protocol
    case CHANNEL_SPI:
        if (tx_len) {
            io_seproxyhal_spi_send(G_io_apdu_buffer, tx_len);

            if (channel & IO_RESET_AFTER_REPLIED) {
                reset();
            }
            return 0; // nothing received from the master so far (it's a tx
                      // transaction)
        } else {
            return io_seproxyhal_spi_recv(G_io_apdu_buffer,
                                          sizeof(G_io_apdu_buffer), 0);
        }

    default:
        THROW(INVALID_PARAMETER);
    }
    return 0;
}

void sample_main(void) {
    volatile unsigned int rx = 0;
    volatile unsigned int tx = 0;
    volatile unsigned int flags = 0;

    // DESIGN NOTE: the bootloader ignores the way APDU are fetched. The only
    // goal is to retrieve APDU.
    // When APDU are to be fetched from multiple IOs, like NFC+USB+BLE, make
    // sure the io_event is called with a
    // switch event, before the apdu is replied to the bootloader. This avoid
    // APDU injection faults.
    for (;;) {
        volatile unsigned short sw = 0;

        BEGIN_TRY {
            TRY {
                rx = tx;
                tx = 0; // ensure no race in catch_other if io_exchange throws
                        // an error
                rx = io_exchange(CHANNEL_APDU | flags, rx);
                flags = 0;

                // no apdu received, well, reset the session, and reset the
                // bootloader configuration
                if (rx == 0) {
                    THROW(0x6982);
                }

                if (G_io_apdu_buffer[0] != CLA) {
                    THROW(0x6E00);
                }

                switch (G_io_apdu_buffer[1]) {
                case INS_GET_PUBLIC_KEY: {
                    uint8_t privateKeyData[32];
                    uint32_t i;
                    uint8_t *dataBuffer = G_io_apdu_buffer + OFFSET_CDATA + 1;
                    cx_ecfp_private_key_t privateKey;

                    operationContext.pathLength =
                        G_io_apdu_buffer[OFFSET_CDATA];
                    if ((operationContext.pathLength < 0x01) ||
                        (operationContext.pathLength > MAX_BIP32_PATH)) {
                        screen_printf("Invalid path\n");
                        THROW(0x6a80);
                    }

                    if ((G_io_apdu_buffer[OFFSET_P1] != 0) ||
                        (G_io_apdu_buffer[OFFSET_P2] != 0)) {
                        THROW(0x6B00);
                    }
                    for (i = 0; i < operationContext.pathLength; i++) {
                        operationContext.bip32Path[i] =
                            (dataBuffer[0] << 24) | (dataBuffer[1] << 16) |
                            (dataBuffer[2] << 8) | (dataBuffer[3]);
                        dataBuffer += 4;
                    }
                    os_perso_derive_seed_bip32(operationContext.bip32Path,
                                               operationContext.pathLength,
                                               privateKeyData, NULL);
                    app_cx_ecfp_init_private_key(CX_CURVE_256R1, privateKeyData,
                                                 32, &privateKey);
                    cx_ecfp_generate_pair(CX_CURVE_256R1,
                                          &operationContext.publicKey,
                                          &privateKey, 1);
                    os_memset(&privateKey, 0, sizeof(privateKey));
                    os_memset(privateKeyData, 0, sizeof(privateKeyData));
                    path_to_string(keyPath);
				    if (os_seph_features() & SEPROXYHAL_TAG_SESSION_START_EVENT_FEATURE_SCREEN_BIG) {
				        UX_DISPLAY(ui_address_blue, NULL);
				    }
				    else {
				        UX_DISPLAY(ui_address_nanos, NULL);   
				    }
                    flags |= IO_ASYNCH_REPLY;
                }

                break;

                case INS_SIGN_BLOB: {
                    uint8_t p1 = G_io_apdu_buffer[OFFSET_P1];
                    uint8_t *dataBuffer = G_io_apdu_buffer + OFFSET_CDATA;
                    uint32_t dataLength = G_io_apdu_buffer[OFFSET_LC];
                    bool last = (p1 & P1_LAST_MARKER);
                    p1 &= (uint8_t)~P1_LAST_MARKER;

                    if (p1 == P1_FIRST) {
                        uint32_t i;
                        operationContext.pathLength = *dataBuffer;
                        dataBuffer++;
                        dataLength--;
                        if ((operationContext.pathLength < 0x01) ||
                            (operationContext.pathLength > MAX_BIP32_PATH)) {
                            screen_printf("Invalid path\n");
                            THROW(0x6a80);
                        }
                        for (i = 0; i < operationContext.pathLength; i++) {
                            operationContext.bip32Path[i] =
                                (dataBuffer[0] << 24) | (dataBuffer[1] << 16) |
                                (dataBuffer[2] << 8) | (dataBuffer[3]);
                            dataBuffer += 4;
                            dataLength -= 4;
                        }
                        cx_sha256_init(&operationContext.hash);
                    } else if (p1 != P1_NEXT) {
                        THROW(0x6B00);
                    }
                    if (G_io_apdu_buffer[OFFSET_P2] != 0) {
                        THROW(0x6B00);
                    }
                    cx_hash(&operationContext.hash.header, 0, dataBuffer,
                            dataLength, NULL);

                    if (!last) {
                        THROW(0x9000);
                    }

                    path_to_string(keyPath);
				    if (os_seph_features() & SEPROXYHAL_TAG_SESSION_START_EVENT_FEATURE_SCREEN_BIG) {
				        UX_DISPLAY(ui_approval_blue, NULL);
				    }
				    else {
				        UX_DISPLAY(ui_approval_nanos, NULL);   
				    }
                    flags |= IO_ASYNCH_REPLY;
                }

                break;

                case 0xFF: // return to dashboard
                    os_sched_exit(0);

                default:
                    THROW(0x6D00);
                    break;
                }
            }
            CATCH_OTHER(e) {
                switch (e & 0xF000) {
                case 0x6000:
                case 0x9000:
                    sw = e;
                    break;
                default:
                    sw = 0x6800 | (e & 0x7FF);
                    break;
                }
                // Unexpected exception => report
                G_io_apdu_buffer[tx] = sw >> 8;
                G_io_apdu_buffer[tx + 1] = sw;
                tx += 2;
            }
            FINALLY {
            }
        }
        END_TRY;
    }
}

void io_seproxyhal_display(const bagl_element_t *element) {
    return io_seproxyhal_display_default((bagl_element_t *)element);
}

unsigned char io_event(unsigned char channel) {
    // nothing done with the event, throw an error on the transport layer if
    // needed

    // can't have more than one tag in the reply, not supported yet.
    switch (G_io_seproxyhal_spi_buffer[0]) {
    case SEPROXYHAL_TAG_FINGER_EVENT:
		UX_FINGER_EVENT(G_io_seproxyhal_spi_buffer);
		break;
	
    case SEPROXYHAL_TAG_BUTTON_PUSH_EVENT:
        UX_BUTTON_PUSH_EVENT(G_io_seproxyhal_spi_buffer);
        break;

#ifdef HAVE_BLE
    // Make automatically discoverable again when disconnected

    case SEPROXYHAL_TAG_BLE_CONNECTION_EVENT:
        if (G_io_seproxyhal_spi_buffer[3] == 0) {
            // TODO : cleaner reset sequence
            // first disable BLE before turning it off
            G_io_seproxyhal_spi_buffer[0] = SEPROXYHAL_TAG_BLE_RADIO_POWER;
            G_io_seproxyhal_spi_buffer[1] = 0;
            G_io_seproxyhal_spi_buffer[2] = 1;
            G_io_seproxyhal_spi_buffer[3] = 0;
            io_seproxyhal_spi_send(G_io_seproxyhal_spi_buffer, 4);
            // send BLE power on (default parameters)
            G_io_seproxyhal_spi_buffer[0] = SEPROXYHAL_TAG_BLE_RADIO_POWER;
            G_io_seproxyhal_spi_buffer[1] = 0;
            G_io_seproxyhal_spi_buffer[2] = 1;
            G_io_seproxyhal_spi_buffer[3] = 3; // ble on & advertise
            io_seproxyhal_spi_send(G_io_seproxyhal_spi_buffer, 5);
        }
        break;
#endif

    case SEPROXYHAL_TAG_DISPLAY_PROCESSED_EVENT:
        if (UX_DISPLAYED()) {
            // TODO perform actions after all screen elements have been displayed
        }
        else {
            UX_DISPLAY_PROCESSED_EVENT();
        }
        break;

    // unknown events are acknowledged
    default:
        break;
    }

    // close the event if not done previously (by a display or whatever)
    if (!io_seproxyhal_spi_is_status_sent()) {
        io_seproxyhal_general_status();
    }
    // command has been processed, DO NOT reset the current APDU transport
    return 1;
}

void app_exit(void) {

    BEGIN_TRY_L(exit) {
        TRY_L(exit) {
            os_sched_exit(-1);
        }
        FINALLY_L(exit) {

        }
    }
    END_TRY_L(exit);
}

__attribute__((section(".boot"))) int main(void) {
    // exit critical section
    __asm volatile("cpsie i");

    UX_INIT();

    // ensure exception will work as planned
    os_boot();

    BEGIN_TRY {
        TRY {
            io_seproxyhal_init();

            USB_power(1);

            ui_idle();

            sample_main();
        }
        CATCH_OTHER(e) {
        }
        FINALLY {
        }
    }
    END_TRY;

    app_exit();
}