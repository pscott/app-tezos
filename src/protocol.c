#include "protocol.h"

#include "apdu.h"
#include "ui.h"
#include "to_string.h"

#include <stdint.h>
#include <string.h>

#include "os.h"

struct __attribute__((packed)) block {
    char magic_byte;
    uint32_t chain_id;
    level_t level;
    uint8_t proto;
    // ... beyond this we don't care
};

struct __attribute__((packed)) endorsement {
    uint8_t magic_byte;
    uint32_t chain_id;
    uint8_t branch[32];
    uint8_t tag;
    uint32_t level;
};

bool parse_baking_data(const void *data, size_t length, struct parsed_baking_data *out) {
    switch (get_magic_byte(data, length)) {
        case MAGIC_BYTE_BAKING_OP:
            if (length != sizeof(struct endorsement)) return false;
            const struct endorsement *endorsement = data;
            // TODO: Check chain ID
            out->is_endorsement = true;
            out->level = READ_UNALIGNED_BIG_ENDIAN(level_t, &endorsement->level);
            return true;
        case MAGIC_BYTE_BLOCK:
            if (length < sizeof(struct block)) return false;
            // TODO: Check chain ID
            out->is_endorsement = false;
            const struct block *block = data;
            out->level = READ_UNALIGNED_BIG_ENDIAN(level_t, &block->level);
            return true;
        case MAGIC_BYTE_INVALID:
        default:
            return false;
    }
}

struct operation_group_header {
    uint8_t magic_byte;
    uint8_t hash[32];
} __attribute__((packed));

struct contract {
    uint8_t originated;
    union {
        struct {
            uint8_t curve_code;
            uint8_t pkh[HASH_SIZE];
        } implicit;
        struct {
            uint8_t pkh[HASH_SIZE];
            uint8_t padding;
        } originated;
    } u;
} __attribute__((packed));

struct operation_header {
    uint8_t tag;
    struct contract contract;
} __attribute__((packed));

struct delegation_contents {
    uint8_t delegate_present;
    uint8_t curve_code;
    uint8_t hash[HASH_SIZE];
} __attribute__((packed));

// These macros assume:
// * Beginning of data: const void *data
// * Total length of data: size_t length
// * Current index of data: size_t ix
// Any function that uses these macros should have these as local variables
#define NEXT_TYPE(type) ({ \
    if (ix + sizeof(type) > length) return sizeof(type); \
    const type *val = data + ix; \
    ix += sizeof(type); \
    val; \
})

#define NEXT_BYTE() (*NEXT_TYPE(uint8_t))

#define PARSE_Z() ({ \
    uint64_t acc = 0; \
    uint64_t shift = 0; \
    while (true) { \
        if (ix >= length) return 23; \
        uint8_t next_byte = NEXT_BYTE(); \
        acc |= (next_byte & 0x7F) << shift; \
        shift += 7; \
        if (!(next_byte & 0x80)) { \
            break; \
        } \
    } \
    acc; \
})

static void compute_pkh(cx_curve_t curve, size_t path_length, uint32_t *bip32_path,
                        struct parsed_operation_group *out) {
    cx_ecfp_public_key_t public_key_init;
    cx_ecfp_private_key_t private_key;
    generate_key_pair(curve, path_length, bip32_path, &public_key_init, &private_key);
    os_memset(&private_key, 0, sizeof(private_key));

    public_key_hash(out->signing.hash, curve, &public_key_init, &out->public_key);
    out->signing.curve_code = curve_to_curve_code(curve);
    out->signing.originated = 0;
}

static uint32_t parse_implicit(struct parsed_contract *out, uint8_t curve_code,
                               const uint8_t hash[HASH_SIZE]) {
    out->originated = 0;
    out->curve_code = curve_code;
    memcpy(out->hash, hash, sizeof(out->hash));
    return 0;
}

static uint32_t parse_contract(struct parsed_contract *out, const struct contract *in) {
    out->originated = in->originated;
    if (out->originated == 0) { // implicit
        out->curve_code = in->u.implicit.curve_code;
        memcpy(out->hash, in->u.implicit.pkh, sizeof(out->hash));
    } else { // originated
        out->curve_code = TEZOS_NO_CURVE;
        memcpy(out->hash, in->u.originated.pkh, sizeof(out->hash));
    }
    return 0;
}

uint32_t parse_operations(const void *data, size_t length, cx_curve_t curve,
                          size_t path_length, uint32_t *bip32_path, struct parsed_operation_group *out) {
    check_null(data);
    check_null(bip32_path);
    check_null(out);
    memset(out, 0, sizeof(*out));
    for (size_t i = 0; i < MAX_OPERATIONS_PER_GROUP; i++) {
        out->operations[i].tag = OPERATION_TAG_NONE;
    }

    compute_pkh(curve, path_length, bip32_path, out); // sets up "signing" and "public_key" members

    size_t ix = 0;

    // Verify magic byte, ignore block hash
    const struct operation_group_header *ogh = NEXT_TYPE(struct operation_group_header);
    if (ogh->magic_byte != MAGIC_BYTE_UNSAFE_OP) return 15;

    size_t op_index = 0;

    while (ix < length) {
        if (op_index >= MAX_OPERATIONS_PER_GROUP) return 55;
        struct parsed_operation *cur = &out->operations[op_index];

        const struct operation_header *hdr = NEXT_TYPE(struct operation_header);
        uint32_t res = parse_contract(&cur->source, &hdr->contract);
        if (res != 0) return res;

        out->total_fee += PARSE_Z(); // fee
        PARSE_Z(); // counter
        PARSE_Z(); // gas limit
        PARSE_Z(); // storage limit
        cur->tag = hdr->tag;

        switch (cur->tag) {
            case OPERATION_TAG_REVEAL:
                // Public key up next! Ensure it matches signing key.
                // Ignore source :-)
                if (NEXT_BYTE() != out->signing.curve_code) return 64;
                size_t klen = out->public_key.W_len;
                if (ix + klen > length) return klen;
                if (memcmp(out->public_key.W, data + ix, klen) != 0) return 4;
                ix += klen;
                break;
            case OPERATION_TAG_DELEGATION:
                {
                    const struct delegation_contents *dlg = NEXT_TYPE(struct delegation_contents);
                    uint32_t res = parse_implicit(&cur->destination, dlg->curve_code, dlg->hash);
                    if (res != 0) return res;
                }
                break;
            case OPERATION_TAG_TRANSACTION:
                {
                    cur->amount = PARSE_Z();

                    const struct contract *destination = NEXT_TYPE(struct contract);
                    uint32_t res = parse_contract(&cur->destination, destination);
                    if (res != 0) return res;

                    uint8_t params = NEXT_BYTE();
                    if (params) return 101; // TODO: Support params
                }
                break;
            default:
                return 8;
        }

        op_index++;
    }

    return 0; // Success
}
