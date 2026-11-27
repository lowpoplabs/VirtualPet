/*
 * The exchange between two devices (see pet_link.h).
 *
 * The stream each way is a run of frames: a type (1 byte), a length (2
 * bytes, little-endian) and that many bytes. Both sides send the same
 * three, in this order, each as soon as it can:
 *
 *   HELLO   the exchange's number, the realm, whether it gives its pet,
 *           whether a pet could move in, its pet's id, hops and meals, and
 *           a random question
 *   PROOF   only when both hold the same pet, from the side with the newer
 *           copy: the answer to the other's question (pet_proof)
 *   PARCEL  a parcel (pet_export): the collection, and the pet if it gives
 *   RESULT  what pet_import() made of the other side's parcel, once the
 *           result is saved
 *
 * A side that gives its pet lets it go when the other's RESULT says it
 * arrived, and not before. BYE, with no bytes, is sent by a side that has
 * stopped for a reason the other cannot work out for itself.
 */
#include <string.h>

#include "pet_link.h"

#define LINK_PROTOCOL 1

enum { FRAME_HELLO = 1, FRAME_PARCEL, FRAME_RESULT, FRAME_BYE, FRAME_PROOF };

#define HELLO_BYTES 24

/* Moves what is still to be sent to the front of its buffer. */
static void compact(pet_link_t *l)
{
    if (l->out_at > 0) {
        memmove(l->out, l->out + l->out_at, l->out_len - l->out_at);
        l->out_len -= l->out_at;
        l->out_at = 0;
    }
}

static void queue(pet_link_t *l, uint8_t type, const uint8_t *data, size_t len)
{
    compact(l);
    if (len > UINT16_MAX || l->out_len + 3 + len > sizeof(l->out)) {
        l->state = PET_LINK_BAD;    /* cannot happen: the buffer holds all three frames */
        return;
    }
    uint8_t *p = l->out + l->out_len;
    p[0] = type;
    p[1] = (uint8_t)len;
    p[2] = (uint8_t)(len >> 8);
    if (len) {
        memmove(p + 3, data, len);
    }
    l->out_len += 3 + len;
}

static void stop(pet_link_t *l, pet_link_state_t why, bool say)
{
    if (l->state != PET_LINK_RUNNING) {
        return;
    }
    l->state = why;
    if (say) {
        queue(l, FRAME_BYE, NULL, 0);
    }
}

static bool can_take(const pet_t *pet)
{
    return pet->away || pet->stage == STAGE_EGG;
}

static void keep(pet_link_t *l)
{
    if (l->saved) {
        l->saved(l->ctx);
    }
}

static void finish_if_done(pet_link_t *l)
{
    if (l->state == PET_LINK_RUNNING && l->got_parcel && l->got_result) {
        l->state = PET_LINK_DONE;
    }
}

static void send_hello(pet_link_t *l)
{
    const pet_t *pet = l->pet;
    uint8_t h[HELLO_BYTES] = {LINK_PROTOCOL, (uint8_t)l->realm, l->i_give, can_take(pet)};
    uint32_t id = pet->away ? 0 : pet->id, hops = pet->away ? 0 : pet->hops;
    uint32_t meals = pet->away ? 0 : pet->meals;
    memcpy(h + 4, &id, 4);
    memcpy(h + 8, &hops, 4);
    memcpy(h + 12, &meals, 4);
    memcpy(h + 16, &l->nonce, 8);
    queue(l, FRAME_HELLO, h, sizeof(h));
}

static void on_hello(pet_link_t *l, const uint8_t *h, size_t len)
{
    if (l->got_hello || len < HELLO_BYTES) {
        stop(l, PET_LINK_BAD, false);
        return;
    }
    l->got_hello = true;
    if (h[0] != LINK_PROTOCOL) {
        stop(l, h[0] > LINK_PROTOCOL ? PET_LINK_NEWER : PET_LINK_OLDER, false);
        return;
    }
    if (h[1] != (uint8_t)l->realm) {
        stop(l, PET_LINK_WRONG_REALM, false);
        return;
    }
    bool peer_gives = h[2] != 0, peer_can_take = h[3] != 0;
    uint32_t id, hops, meals;
    memcpy(&id, h + 4, 4);
    memcpy(&hops, h + 8, 4);
    memcpy(&meals, h + 12, 4);
    uint64_t question;
    memcpy(&question, h + 16, 8);

    /*
     * Both say they hold the same pet: a move whose last word was lost.
     * The copy that has moved on further is the pet, and the other is to
     * be let go. Both sides work out which from the two hellos; but the
     * older copy is only let go when the proof arrives (on_proof), since
     * a hello is only a claim.
     */
    pet_t *pet = l->pet;
    if (!pet->away && id != 0 && id == pet->id) {
        bool mine_older = pet->hops != hops ? pet->hops < hops
                          : (pet->meals != meals ? pet->meals < meals : l->caller);
        if (mine_older) {
            l->doubted = true;
            l->peer_hops = hops;
            l->peer_meals = meals;
            l->i_give = false;      /* not a copy that may be the older one */
        } else {
            uint64_t proof = pet_proof(pet, question, pet->hops, pet->meals);
            queue(l, FRAME_PROOF, (const uint8_t *)&proof, sizeof(proof));
            peer_gives = false;
            peer_can_take = true;
        }
    }
    l->peer_gives = peer_gives;

    if (l->i_give && peer_gives) {
        stop(l, PET_LINK_BOTH_GIVE, false);
        return;
    }
    if ((l->i_give && !peer_can_take) || (peer_gives && !can_take(pet))) {
        stop(l, PET_LINK_OCCUPIED, false);
        return;
    }

    /* Written straight into the buffer it is sent from, after the three bytes that head a frame. */
    compact(l);
    uint8_t *p = l->out + l->out_len;
    size_t room = sizeof(l->out) - l->out_len;
    size_t n = room > 3 + 8 ? pet_export(pet, l->realm, l->i_give, p + 3, room - 3 - 8) : 0;
    if (n == 0 || n > UINT16_MAX) {
        stop(l, PET_LINK_BAD, true);
        return;
    }
    p[0] = FRAME_PARCEL;
    p[1] = (uint8_t)n;
    p[2] = (uint8_t)(n >> 8);
    l->out_len += 3 + n;
}

/* The other side answers the question: it does hold this pet, further on. */
static void on_proof(pet_link_t *l, const uint8_t *data, size_t len)
{
    uint64_t theirs = 0;
    if (len >= sizeof(theirs)) {
        memcpy(&theirs, data, sizeof(theirs));
    }
    if (!l->doubted || l->pet->away) {
        return;
    }
    l->doubted = false;
    uint64_t right = pet_proof(l->pet, l->nonce, l->peer_hops, l->peer_meals);
    if (right != 0 && theirs == right) {
        pet_leave(l->pet);
        keep(l);
        l->copy_dropped = true;
    }
}

static void on_parcel(pet_link_t *l, const uint8_t *data, size_t len)
{
    if (!l->got_hello || l->got_parcel) {
        stop(l, PET_LINK_BAD, false);
        return;
    }
    l->got_parcel = true;
    bool arrived = false;
    pet_import_t r = pet_import(l->pet, l->realm, data, len, l->pet->now, &arrived);
    if (r == PET_IMPORT_OK) {
        keep(l);    /* saved before the other side is told */
        l->joined = true;
        l->pet_received = arrived;
    }
    uint8_t code = (uint8_t)r;
    queue(l, FRAME_RESULT, &code, 1);
    finish_if_done(l);
}

static void on_result(pet_link_t *l, const uint8_t *data, size_t len)
{
    if (!l->got_hello || l->got_result || len < 1) {
        stop(l, PET_LINK_BAD, false);
        return;
    }
    l->got_result = true;
    if (data[0] == PET_IMPORT_OK) {
        if (l->i_give && !l->pet->away) {
            pet_leave(l->pet);      /* it is safe over there: now it has gone from here */
            keep(l);
            l->pet_sent = true;
        }
    } else {
        l->peer_reason = (pet_import_t)data[0];
        if (l->i_give) {
            stop(l, PET_LINK_REFUSED, false);
            return;
        }
    }
    finish_if_done(l);
}

void pet_link_start(pet_link_t *l, pet_t *pet, pet_realm_t realm, bool give_pet, bool caller,
                    uint64_t nonce, void (*saved)(void *ctx), void *ctx)
{
    memset(l, 0, sizeof(*l));
    l->nonce = nonce;
    l->pet = pet;
    l->realm = realm;
    l->caller = caller;
    l->saved = saved;
    l->ctx = ctx;
    l->state = PET_LINK_RUNNING;
    l->i_give = give_pet;
    if (give_pet && pet->away) {
        stop(l, PET_LINK_NO_PET, true);
        return;
    }
    send_hello(l);
}

void pet_link_in(pet_link_t *l, const uint8_t *data, size_t len)
{
    while (len > 0 && l->state == PET_LINK_RUNNING) {
        /* Gather the three bytes that head a frame, then the frame. */
        size_t want = l->in_len < 3 ? 3 : 3 + (size_t)(l->in[1] | l->in[2] << 8);
        size_t take = want - l->in_len < len ? want - l->in_len : len;
        memcpy(l->in + l->in_len, data, take);
        l->in_len += take;
        data += take;
        len -= take;
        if (l->in_len < 3) {
            continue;
        }
        want = 3 + (size_t)(l->in[1] | l->in[2] << 8);
        if (want > sizeof(l->in)) {
            stop(l, PET_LINK_BAD, false);   /* longer than any frame of ours */
            return;
        }
        if (l->in_len < want) {
            continue;
        }
        size_t n = l->in_len - 3;
        l->in_len = 0;
        switch (l->in[0]) {
        case FRAME_HELLO:  on_hello(l, l->in + 3, n); break;
        case FRAME_PARCEL: on_parcel(l, l->in + 3, n); break;
        case FRAME_RESULT: on_result(l, l->in + 3, n); break;
        case FRAME_PROOF:  on_proof(l, l->in + 3, n); break;
        case FRAME_BYE:    stop(l, PET_LINK_LOST, false); break;
        default:           stop(l, PET_LINK_BAD, false); break;
        }
    }
}

size_t pet_link_out(pet_link_t *l, uint8_t *buf, size_t cap)
{
    size_t n = l->out_len - l->out_at;
    n = n < cap ? n : cap;
    memcpy(buf, l->out + l->out_at, n);
    l->out_at += n;
    return n;
}

void pet_link_lost(pet_link_t *l)
{
    stop(l, PET_LINK_LOST, false);
}

pet_link_state_t pet_link_state(const pet_link_t *l)
{
    return l->state;
}
