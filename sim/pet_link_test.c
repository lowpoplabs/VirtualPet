/* Desktop checks for the exchange between two devices, run one against the other. Exit code 0 = pass. */
#include <stdio.h>
#include <string.h>

#include "pet_link.h"

static int failures;

#define CHECK(cond, what)                                   \
    do {                                                    \
        if (!(cond)) {                                      \
            printf("FAIL: %s (line %d)\n", what, __LINE__); \
            failures++;                                     \
        }                                                   \
    } while (0)

#define DAY  86400u
#define HOUR 3600u
#define T0   (100 * DAY + 10 * HOUR)

static pet_link_t s_la, s_lb;
static int s_saves;

static void saved(void *ctx)
{
    (void)ctx;
    s_saves++;
}

static void meal(pet_t *p, food_type_t type, float value)
{
    pet_feed(p, type, value);
    pet_tick(p, p->now + 61, NULL);
}

/* A device with a hatched pet, and one resident at its farm. */
static void device(pet_t *p, uint32_t seed, const char *name, pet_form_t retired_as)
{
    pet_init(p, NULL, T0, seed);
    p->stage = STAGE_ADULT;
    p->form = retired_as;
    p->retire_ready = true;
    pet_retire(p, seed + 1000);
    for (int i = 0; i < 8; i++) {
        meal(p, i % 2 ? FOOD_WIFI_SECURED : FOOD_BLE_PHONE, 2);
    }
    snprintf(p->name, sizeof(p->name), "%s", name);
}

static bool same_farm(const pet_t *a, const pet_t *b)
{
    if (a->farm.count != b->farm.count || a->badges != b->badges || a->farm.forms != b->farm.forms) {
        return false;
    }
    for (int i = 0; i < a->farm.count; i++) {
        if (a->farm.resident[i].id != b->farm.resident[i].id) {
            return false;
        }
    }
    return true;
}

/*
 * Two devices meet. Bytes cross in pieces, a turn each way, until neither
 * has more to send; after `budget` bytes in all the link drops. Returns
 * the bytes that crossed.
 */
static size_t meet(pet_t *a, bool a_gives, pet_realm_t realm_a, pet_t *b, bool b_gives, pet_realm_t realm_b,
                   size_t piece, size_t budget)
{
    uint8_t buf[512];
    size_t crossed = 0;
    static uint64_t question = 0x1234567890abcdefull;
    question = question * 6364136223846793005ull + 1442695040888963407ull;
    pet_link_start(&s_la, a, realm_a, a_gives, true, question, saved, NULL);
    pet_link_start(&s_lb, b, realm_b, b_gives, false, ~question * 31, saved, NULL);
    for (;;) {
        size_t moved = 0;
        for (int dir = 0; dir < 2; dir++) {
            pet_link_t *from = dir ? &s_lb : &s_la, *to = dir ? &s_la : &s_lb;
            size_t want = piece < budget - crossed ? piece : budget - crossed;
            size_t n = pet_link_out(from, buf, want);
            pet_link_in(to, buf, n);
            crossed += n;
            moved += n;
        }
        if (crossed >= budget) {
            pet_link_lost(&s_la);
            pet_link_lost(&s_lb);
            return crossed;
        }
        if (moved == 0) {
            /* Nothing more to say: whoever is still waiting has lost the other. */
            pet_link_lost(&s_la);
            pet_link_lost(&s_lb);
            return crossed;
        }
    }
}

#define ALL ((size_t)-1)
#define TEST PET_REALM_TEST

static void test_give(void)
{
    static pet_t a, b;
    device(&a, 11, "Mossy", FORM_ROAMER);
    pet_init(&b, NULL, T0 + 3 * HOUR, 12);  /* an egg, and another clock */
    uint32_t id = a.id, meals = a.meals;
    s_saves = 0;
    size_t n = meet(&a, true, TEST, &b, false, TEST, 20, ALL);
    printf("an exchange with a pet is %u bytes\n", (unsigned)n);
    CHECK(pet_link_state(&s_la) == PET_LINK_DONE && pet_link_state(&s_lb) == PET_LINK_DONE, "both sides finish");
    CHECK(s_la.pet_sent && !s_la.pet_received && s_lb.pet_received && !s_lb.pet_sent, "one sent, one received");
    CHECK(a.away && !b.away && b.id == id && b.hops == 1 && b.meals == meals && strcmp(b.name, "Mossy") == 0,
          "the pet moved, whole");
    CHECK(s_la.joined && s_lb.joined && same_farm(&a, &b) && a.farm.count == 1, "and the farms are level");
    CHECK(s_saves >= 3, "each change was saved");

    /* In pieces of one byte, and of many. */
    for (size_t piece = 1; piece <= 512; piece = piece * 7 + 1) {
        device(&a, 11, "Mossy", FORM_ROAMER);
        pet_init(&b, NULL, T0, 12);
        meet(&a, true, TEST, &b, false, TEST, piece, ALL);
        CHECK(pet_link_state(&s_la) == PET_LINK_DONE && pet_link_state(&s_lb) == PET_LINK_DONE && a.away && !b.away,
              "whatever the size of the pieces");
    }

    /* The side that waits can be the one that gives. */
    device(&a, 13, "Fern", FORM_SINGER);
    pet_init(&b, NULL, T0, 14);
    pet_leave(&b);
    meet(&b, false, TEST, &a, true, TEST, 50, ALL);
    CHECK(pet_link_state(&s_la) == PET_LINK_DONE && a.away && !b.away && strcmp(b.name, "Fern") == 0,
          "the waiting side can give, to an empty pasture");
}

static void test_visit(void)
{
    static pet_t a, b;
    device(&a, 21, "Ash", FORM_HOMEBODY);
    device(&b, 22, "Birch", FORM_RACER);
    uint32_t ida = a.id, idb = b.id;
    meet(&a, false, TEST, &b, false, TEST, 20, ALL);
    CHECK(pet_link_state(&s_la) == PET_LINK_DONE && pet_link_state(&s_lb) == PET_LINK_DONE, "a visit finishes");
    CHECK(!s_la.pet_sent && !s_lb.pet_sent && a.id == ida && b.id == idb && !a.away && !b.away, "no pet moves");
    CHECK(same_farm(&a, &b) && a.farm.count == 2, "both farms hold both residents");
    meet(&a, false, TEST, &b, false, TEST, 20, ALL);
    CHECK(same_farm(&a, &b) && a.farm.count == 2, "a second visit changes nothing");
}

static void test_refusals(void)
{
    static pet_t a, b, a0, b0;
    device(&a, 31, "Bramble", FORM_GLOWBUG);
    device(&b, 32, "Thistle", FORM_PRISM);
    a0 = a;
    b0 = b;

    meet(&a, true, TEST, &b, false, TEST, 20, ALL);
    CHECK(pet_link_state(&s_la) == PET_LINK_OCCUPIED && pet_link_state(&s_lb) == PET_LINK_OCCUPIED,
          "a hatched pet does not give way, and both sides know it");
    CHECK(a.id == a0.id && !a.away && b.id == b0.id && a.farm.count == 1 && b.farm.count == 1, "nothing changed");

    meet(&a, true, TEST, &b, true, TEST, 20, ALL);
    CHECK(pet_link_state(&s_la) == PET_LINK_BOTH_GIVE && pet_link_state(&s_lb) == PET_LINK_BOTH_GIVE,
          "two pets cannot cross at once");

    meet(&a, false, TEST, &b, false, PET_REALM_RELEASE, 20, ALL);
    CHECK(pet_link_state(&s_la) == PET_LINK_WRONG_REALM && pet_link_state(&s_lb) == PET_LINK_WRONG_REALM,
          "a test build and a release build do not trade");
    CHECK(a.farm.count == 1 && b.farm.count == 1, "not even farms");

    pet_leave(&a);
    meet(&a, true, TEST, &b, false, TEST, 20, ALL);
    CHECK(pet_link_state(&s_la) == PET_LINK_NO_PET && pet_link_state(&s_lb) == PET_LINK_LOST,
          "an empty pasture has no pet to give, and the other side is told it is over");

    /* Not our exchange at all. */
    static pet_link_t l;
    pet_link_start(&l, &b, TEST, false, false, 77, saved, NULL);
    pet_link_in(&l, (const uint8_t *)"\x09\x02\x00hi", 5);
    CHECK(pet_link_state(&l) == PET_LINK_BAD, "a frame of an unknown kind ends it");
    pet_link_start(&l, &b, TEST, false, false, 77, saved, NULL);
    pet_link_in(&l, (const uint8_t *)"\x02\xff\xff", 3);
    CHECK(pet_link_state(&l) == PET_LINK_BAD, "and so does one too long to be a parcel");
    uint8_t hello[27] = {1, 24, 0, 7 /* a later exchange */, PET_REALM_TEST};
    pet_link_start(&l, &b, TEST, false, false, 77, saved, NULL);
    pet_link_in(&l, hello, sizeof(hello));
    CHECK(pet_link_state(&l) == PET_LINK_NEWER, "a later exchange is told as newer");
    hello[3] = 0;
    pet_link_start(&l, &b, TEST, false, false, 77, saved, NULL);
    pet_link_in(&l, hello, sizeof(hello));
    CHECK(pet_link_state(&l) == PET_LINK_OLDER, "and an earlier one as older");
    CHECK(b.id == b0.id && !b.away && b.farm.count == 1, "none of which changed anything");
}

/*
 * The link drops after every possible number of bytes. The pet is never
 * lost; and when the two meet again there is one of it, where it was
 * going unless nothing of it had arrived.
 */
static void test_dropped_links(void)
{
    static pet_t a, b;
    device(&a, 41, "Clover", FORM_SCAVENGER);
    device(&b, 42, "", FORM_HUNTER);
    b.stage = STAGE_EGG;
    size_t whole = meet(&a, true, TEST, &b, false, TEST, 20, ALL);
    CHECK(a.away && !b.away, "the whole exchange moves the pet");
    int both = 0, stayed = 0, moved = 0;

    for (size_t cut = 0; cut <= whole; cut++) {
        device(&a, 41, "Clover", FORM_SCAVENGER);
        device(&b, 42, "", FORM_HUNTER);
        b.stage = STAGE_EGG;    /* an egg, with a resident at its farm */
        uint32_t id = a.id;

        meet(&a, true, TEST, &b, false, TEST, 20, cut);
        bool at_a = !a.away && a.id == id, at_b = !b.away && b.id == id;
        CHECK(at_a || at_b, "the pet is never lost");
        CHECK(cut == whole || pet_link_state(&s_la) != PET_LINK_DONE || pet_link_state(&s_lb) != PET_LINK_DONE || !at_a,
              "a side only calls it done when it is");
        both += at_a && at_b;
        stayed += at_a && !at_b;
        moved += at_b && !at_a;
        if (at_a && !at_b) {
            CHECK(!s_la.pet_sent, "the giver does not think it sent a pet it still has");
        }

        /* They meet again, and the owner tries again if the pet still seems to be here. */
        meet(&a, at_a, TEST, &b, false, TEST, 20, ALL);
        bool then_a = !a.away && a.id == id, then_b = !b.away && b.id == id;
        CHECK(then_b && !then_a, "after they meet again the pet is on the other side, once");
        CHECK(!(at_a && at_b) || s_la.copy_dropped, "and a second copy was noticed and dropped");
        CHECK(same_farm(&a, &b) && a.farm.count == 2, "with the farms level");
        CHECK(strcmp(b.name, "Clover") == 0 && b.hops == 1, "and it is itself, one hop on");
    }
    printf("link dropped at each of %u bytes: pet stayed %d times, moved %d, was on both for a while %d\n",
           (unsigned)whole + 1, stayed, moved, both);
    CHECK(both > 0 && stayed > 0 && moved > 0, "all three outcomes were tried");
}

/* A copy that was fed after the drop is still the older one. */
static void test_stale_copy_fed(void)
{
    static pet_t a, b;
    uint32_t id = 0;
    /* Find a drop that leaves it on both: it arrived, and the word that it had was lost. */
    for (size_t cut = 0; cut < 4000; cut++) {
        device(&a, 51, "Rowan", FORM_ROAMER);
        pet_init(&b, NULL, T0, 52);
        id = a.id;
        meet(&a, true, TEST, &b, false, TEST, 20, cut);
        if (!b.away && b.id == id) {
            break;
        }
    }
    CHECK(!a.away && !b.away && a.id == id && b.id == id, "both hold it");
    for (int i = 0; i < 5; i++) {
        meal(&a, FOOD_BLE_TV, 2);
    }
    meet(&b, false, TEST, &a, false, TEST, 20, ALL);    /* a visit, called from the other side */
    CHECK(a.away && !b.away && b.id == id && s_lb.copy_dropped, "the one that moved on is the pet");
}

uint64_t pet_siphash(const uint8_t key[16], const uint8_t *in, size_t len);     /* pet_core.c */

/*
 * A stranger who has been told a pet's id (every hello says it) claims to
 * hold a newer copy. Without the pet's secret it proves nothing, and the
 * pet stays where it is.
 */
static void test_stranger(void)
{
    /* The hash is the published one: its first two test vectors, key 00..0f. */
    uint8_t key[16], msg[1] = {0};
    for (int i = 0; i < 16; i++) {
        key[i] = (uint8_t)i;
    }
    CHECK(pet_siphash(key, msg, 0) == 0x726fdb47dd0e0e31ull, "SipHash-2-4 of nothing");
    CHECK(pet_siphash(key, msg, 1) == 0x74f839c593dc67fdull, "SipHash-2-4 of one byte");

    static pet_t a, liar;
    device(&a, 61, "Hazel", FORM_ROAMER);
    uint32_t id = a.id, meals = a.meals;

    /* The liar's device: a pet made to carry the same id and more hops, with a secret of its own. */
    device(&liar, 62, "Hazel", FORM_ROAMER);
    liar.id = id;
    liar.hops = 9;
    meet(&liar, false, TEST, &a, false, TEST, 20, ALL);
    CHECK(!a.away && a.id == id && a.meals == meals && !s_lb.copy_dropped, "a claim without the secret drops nothing");
    CHECK(pet_link_state(&s_lb) == PET_LINK_DONE, "and the visit goes on as a visit");

    /* The same, as the one that gives, and with a proof that is a guess. */
    static pet_link_t l;
    pet_link_start(&l, &a, TEST, true, false, 4242, saved, NULL);
    uint8_t hello[27] = {1, 24, 0, 1, PET_REALM_TEST, 0, 1};
    uint32_t hops = 9;
    memcpy(hello + 3 + 4, &id, 4);
    memcpy(hello + 3 + 8, &hops, 4);
    memcpy(hello + 3 + 12, &meals, 4);
    pet_link_in(&l, hello, sizeof(hello));
    uint8_t guess[11] = {5, 8, 0, 1, 2, 3, 4, 5, 6, 7, 8};
    pet_link_in(&l, guess, sizeof(guess));
    CHECK(!a.away && a.id == id && !l.copy_dropped, "a guessed proof drops nothing");
    CHECK(!l.pet_sent, "and a pet that may be the older copy is not given to whoever asked");

    /* With no secret at all (a pet from an old save, before the host gave it one) nothing is proved. */
    static pet_t b;
    b = a;
    memset(a.secret, 0, sizeof(a.secret));
    memset(b.secret, 0, sizeof(b.secret));
    b.hops = 3;
    meet(&b, false, TEST, &a, false, TEST, 20, ALL);
    CHECK(!a.away && !b.away, "two copies with no secret both stay: neither can prove itself");
}

int main(void)
{
    test_stranger();
    test_give();
    test_visit();
    test_refusals();
    test_dropped_links();
    test_stale_copy_fed();

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all link checks passed\n");
    return 0;
}
