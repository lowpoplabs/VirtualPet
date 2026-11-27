/*
 * The exchange between two devices that have been brought together: the
 * collections are joined both ways, and a pet moves if one side gives it.
 *
 * It knows nothing of Bluetooth. A host gives it the bytes that arrive
 * (pet_link_in) and sends the bytes it asks to have sent (pet_link_out), in
 * order, over any link that keeps them in order. The watch, a phone and
 * the simulator run the same one.
 *
 * The pet is never lost. The side that gives it keeps it until the other
 * has said it holds the whole of it, saved. If the link drops before that
 * word arrives, both may hold a copy for a while; that is the lesser harm,
 * and when the two next meet the older copy is noticed and dropped: but
 * only once the other side has proved it holds that pet, with the secret
 * that travels inside the pet and is never said aloud. A stranger who
 * merely names the pet cannot make a device let it go.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pet_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The Bluetooth service a waiting device offers, only while its owner has
 * the exchange screen open. Made at random for this project; both ends
 * read them from here.
 *
 * The side that calls writes its bytes to TO_WAITER, and is notified of
 * the other's on FROM_WAITER. Each write or notification is one piece of
 * the stream, of whatever size the link allows.
 */
#define PET_LINK_SERVICE_UUID     "9a12f1ff-962b-4b4e-8d2d-95a00aa282a8"
#define PET_LINK_TO_WAITER_UUID   "9d983c30-98a5-4053-8cea-1360584b291a"
#define PET_LINK_FROM_WAITER_UUID "83dc5b32-2ef7-42e5-bd7c-213b6fa6299e"

typedef enum {
    PET_LINK_RUNNING,
    PET_LINK_DONE,          /* see pet_sent, pet_received, joined and copy_dropped */
    PET_LINK_LOST,          /* the link dropped, or the other side gave up, before the end */
    PET_LINK_BAD,           /* the other side is not speaking this exchange */
    PET_LINK_NEWER,         /* the other side is a newer build: update this device */
    PET_LINK_OLDER,         /* the other side is an older build: update that one */
    PET_LINK_WRONG_REALM,   /* a test build and a release build */
    PET_LINK_NO_PET,        /* asked to give a pet, and none lives here */
    PET_LINK_BOTH_GIVE,     /* both sides offered a pet: one at a time */
    PET_LINK_OCCUPIED,      /* a hatched pet lives where the pet was to go */
    PET_LINK_REFUSED,       /* the other side would not take the parcel: see peer_reason */
} pet_link_state_t;

typedef struct pet_link {
    /* What happened; read once the state is no longer PET_LINK_RUNNING. */
    bool pet_sent;          /* this device's pet has gone to the other */
    bool pet_received;      /* a pet has arrived here */
    bool joined;            /* the other side's collection was joined into this one */
    bool copy_dropped;      /* this device held an older copy of a pet the other has, and let it go */
    pet_import_t peer_reason;

    /* The rest is the exchange's own. */
    pet_t *pet;
    pet_realm_t realm;
    bool caller;
    void (*saved)(void *ctx);
    void *ctx;
    pet_link_state_t state;
    bool i_give, peer_gives, got_hello, got_parcel, got_result;
    bool doubted;           /* the other side says it holds a newer copy of this pet, and has yet to prove it */
    uint64_t nonce;         /* this side's question */
    uint32_t peer_hops, peer_meals;
    size_t out_len, out_at, in_len;
    uint8_t out[PET_PARCEL_MAX + 64];
    uint8_t in[PET_PARCEL_MAX + 8];
} pet_link_t;

/*
 * Begins an exchange on a link that has just come up. give_pet: the owner
 * chose to send this device's pet. caller: this side made the call (it
 * only settles a tie). nonce: a random number, new each time, that the
 * other side cannot guess. saved is called each time *pet has been changed and
 * must be kept: save it before returning, since the other side is told
 * the pet is safe as soon as it does. pet must have been ticked to now.
 *
 * The struct is some 12 KB: make it only for the length of an exchange.
 */
void pet_link_start(pet_link_t *link, pet_t *pet, pet_realm_t realm, bool give_pet, bool caller,
                    uint64_t nonce, void (*saved)(void *ctx), void *ctx);

/* Bytes that arrived, in order, in pieces of any size. */
void pet_link_in(pet_link_t *link, const uint8_t *data, size_t len);

/* The next bytes to send, up to cap. Returns how many; 0 when there are none for now. */
size_t pet_link_out(pet_link_t *link, uint8_t *buf, size_t cap);

/* The link has dropped. */
void pet_link_lost(pet_link_t *link);

/*
 * Where it stands. Once it is not PET_LINK_RUNNING, send what
 * pet_link_out() still holds (the other side is waiting for it) and then
 * close the link.
 */
pet_link_state_t pet_link_state(const pet_link_t *link);

#ifdef __cplusplus
}
#endif
