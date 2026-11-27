/*
 * Places: see pet_places.h.
 *
 * The name lists here are a starter set that leans toward US chains. The
 * owner can replace them, a kind at a time, from a file on the card
 * (pet_config.h).
 */
#include "pet_places.h"

#include <stdio.h>
#include <string.h>

/* ---- Tuning -------------------------------------------------------------- */

#define SETTLE_S        600     /* this long among the same networks makes a new habitat */
#define CAND_FORGET_S   120     /* a place it was settling into is dropped this long after losing it */
#define LEAVE_S         300     /* this long without its networks and it has left */
#define ARRIVE_HITS     2       /* scans in a row that must match a known habitat */
/*
 * Scans come every 30 s plugged in but as rarely as every 15 minutes at
 * night, so leaving and forgetting take two scans in a row as well as time.
 */
#define MISSES          2
#define SCAN_GAP_MAX_S  1200    /* scans further apart than this say nothing about the time between */
#define HOME_MINUTES    120
#define SHARE_MIN_POINTS 20.0f  /* network food needed before "where it came from" means anything */
#define GEN            (-1)

static const struct {
    const char *name;
    const char *how;
    const char *key;
} KINDS[PLACE_KIND_COUNT] = {
    [PLACE_COFFEE]     = {"Coffee shops", "visit a coffee shop", "coffee"},
    [PLACE_HOTEL]      = {"Hotels", "stay at a hotel", "hotel"},
    [PLACE_STORE]      = {"Stores", "go shopping", "store"},
    [PLACE_AIRPORT]    = {"Airports", "visit an airport", "airport"},
    [PLACE_INFLIGHT]   = {"In the air", "find Wi-Fi on a plane", "inflight"},
    [PLACE_TRAIN]      = {"Trains", "ride a train", "train"},
    [PLACE_LIBRARY]    = {"Libraries", "visit a library", "library"},
    [PLACE_UNIVERSITY] = {"Universities", "visit a campus", "university"},
    [PLACE_HOSPITAL]   = {"Hospitals", "visit a hospital", "hospital"},
    [PLACE_MUSEUM]     = {"Museums", "visit a museum", "museum"},
    [PLACE_PARK]       = {"Parks", "find a park visitor center", "park"},
    [PLACE_RESTAURANT] = {"Restaurants", "eat out", "restaurant"},
    [PLACE_SAFETY]     = {"Public safety", "visit a fire or police station", "safety"},
};

/*
 * The built-in list: lower-case pieces of network names. A number is a
 * chain (two spellings of one chain share a number); GEN is a general word,
 * which cannot tell one place from the next and so counts at most once a
 * day.
 *
 * The numbers are only positions within this table. What is remembered of
 * a chain is place_chain_id() of its first spelling here. Saves made before
 * that kept the number as a bit, so the numbers of chains that were in the
 * list then (up to 2026-11) must not change: place_legacy_chain() reads
 * them. New chains can take any unused number.
 */
static const struct {
    uint8_t kind;
    int8_t brand;
    const char *piece;
} NAMES[] = {
    {PLACE_COFFEE, 0, "starbucks"},     {PLACE_COFFEE, 1, "dunkin"},
    {PLACE_COFFEE, 2, "peets"},         {PLACE_COFFEE, 2, "peet's"},
    {PLACE_COFFEE, 3, "tim hortons"},   {PLACE_COFFEE, 3, "timhortons"},
    {PLACE_COFFEE, 4, "caribou"},       {PLACE_COFFEE, 5, "dutch bros"},
    {PLACE_COFFEE, 6, "panera"},        {PLACE_COFFEE, 7, "costa coffee"},
    {PLACE_COFFEE, 8, "blue bottle"},   {PLACE_COFFEE, 9, "philz"},
    {PLACE_COFFEE, 10, "coffee bean"},  {PLACE_COFFEE, 11, "scooters"},
    {PLACE_COFFEE, 12, "7brew"},        {PLACE_COFFEE, 12, "7 brew"},
    {PLACE_COFFEE, GEN, "coffee"},      {PLACE_COFFEE, GEN, "cafe"},
    {PLACE_COFFEE, GEN, "espresso"},    {PLACE_COFFEE, GEN, "roasters"},

    {PLACE_HOTEL, 0, "marriott"},       {PLACE_HOTEL, 1, "hilton"},
    {PLACE_HOTEL, 1, "hhonors"},        {PLACE_HOTEL, 2, "hyatt"},
    {PLACE_HOTEL, 3, "holiday inn"},    {PLACE_HOTEL, 3, "holidayinn"},
    {PLACE_HOTEL, 4, "hampton"},        {PLACE_HOTEL, 5, "best western"},
    {PLACE_HOTEL, 5, "bestwestern"},    {PLACE_HOTEL, 6, "wyndham"},
    {PLACE_HOTEL, 7, "motel 6"},        {PLACE_HOTEL, 7, "motel6"},
    {PLACE_HOTEL, 8, "la quinta"},      {PLACE_HOTEL, 8, "laquinta"},
    {PLACE_HOTEL, 9, "sheraton"},       {PLACE_HOTEL, 10, "westin"},
    {PLACE_HOTEL, 11, "courtyard"},     {PLACE_HOTEL, 12, "residence inn"},
    {PLACE_HOTEL, 13, "fairfield"},     {PLACE_HOTEL, 14, "doubletree"},
    {PLACE_HOTEL, 15, "embassy suites"},{PLACE_HOTEL, 16, "comfort inn"},
    {PLACE_HOTEL, 16, "comfort suites"},{PLACE_HOTEL, 17, "quality inn"},
    {PLACE_HOTEL, 18, "radisson"},      {PLACE_HOTEL, 19, "four seasons"},
    {PLACE_HOTEL, 20, "ihg"},           {PLACE_HOTEL, 21, "super 8"},
    {PLACE_HOTEL, 22, "days inn"},      {PLACE_HOTEL, 23, "ramada"},
    {PLACE_HOTEL, 24, "drury"},         {PLACE_HOTEL, 25, "omni hotel"},
    {PLACE_HOTEL, 26, "staybridge"},    {PLACE_HOTEL, 27, "homewood"},
    {PLACE_HOTEL, 28, "extended stay"}, {PLACE_HOTEL, 29, "red roof"},
    {PLACE_HOTEL, GEN, "hotel"},        {PLACE_HOTEL, GEN, "motel"},
    {PLACE_HOTEL, GEN, "resort"},       {PLACE_HOTEL, GEN, "lodge"},

    {PLACE_STORE, 0, "target guest"},   {PLACE_STORE, 1, "walmart"},
    {PLACE_STORE, 1, "wal-mart"},       {PLACE_STORE, 2, "costco"},
    {PLACE_STORE, 3, "bestbuy"},        {PLACE_STORE, 3, "best buy"},
    {PLACE_STORE, 4, "homedepot"},      {PLACE_STORE, 4, "home depot"},
    {PLACE_STORE, 5, "lowes"},          {PLACE_STORE, 5, "lowe's"},
    {PLACE_STORE, 6, "kroger"},         {PLACE_STORE, 7, "safeway"},
    {PLACE_STORE, 8, "wholefoods"},     {PLACE_STORE, 8, "whole foods"},
    {PLACE_STORE, 9, "trader joe"},     {PLACE_STORE, 10, "cvs"},
    {PLACE_STORE, 11, "walgreens"},     {PLACE_STORE, 12, "ikea"},
    {PLACE_STORE, 13, "apple store"},   {PLACE_STORE, 14, "macys"},
    {PLACE_STORE, 14, "macy's"},        {PLACE_STORE, 15, "kohls"},
    {PLACE_STORE, 15, "kohl's"},        {PLACE_STORE, 16, "publix"},
    {PLACE_STORE, 17, "sams club"},     {PLACE_STORE, 17, "sam's club"},
    {PLACE_STORE, 18, "nordstrom"},     {PLACE_STORE, 19, "barnes"},
    {PLACE_STORE, 20, "meijer"},        {PLACE_STORE, 21, "albertsons"},
    {PLACE_STORE, 22, "petsmart"},      {PLACE_STORE, 23, "rei guest"},
    {PLACE_STORE, GEN, "mall"},         {PLACE_STORE, GEN, "outlet"},
    {PLACE_STORE, GEN, "supermarket"},

    {PLACE_AIRPORT, 0, "boingo"},       {PLACE_AIRPORT, 1, "sfo free"},
    {PLACE_AIRPORT, 2, "flylax"},       {PLACE_AIRPORT, 2, "lax free"},
    {PLACE_AIRPORT, 3, "atl free"},     {PLACE_AIRPORT, 4, "seatac"},
    {PLACE_AIRPORT, 4, "sea-tac"},      {PLACE_AIRPORT, 5, "phx free"},
    {PLACE_AIRPORT, 6, "jfk"},          {PLACE_AIRPORT, 7, "iah free"},
    {PLACE_AIRPORT, 8, "den airport"},  {PLACE_AIRPORT, 9, "dfw"},
    {PLACE_AIRPORT, 10, "heathrow"},    {PLACE_AIRPORT, 11, "pdx"},
    {PLACE_AIRPORT, GEN, "airport"},    {PLACE_AIRPORT, GEN, "aeroport"},
    {PLACE_AIRPORT, GEN, "aeropuerto"}, {PLACE_AIRPORT, GEN, "flughafen"},

    {PLACE_INFLIGHT, 0, "gogoinflight"},{PLACE_INFLIGHT, 0, "gogo"},
    {PLACE_INFLIGHT, 1, "united_wi-fi"},{PLACE_INFLIGHT, 1, "unitedwifi"},
    {PLACE_INFLIGHT, 2, "deltawifi"},   {PLACE_INFLIGHT, 2, "delta wifi"},
    {PLACE_INFLIGHT, 3, "aa-inflight"}, {PLACE_INFLIGHT, 3, "aainflight"},
    {PLACE_INFLIGHT, 4, "southwestwifi"},{PLACE_INFLIGHT, 5, "alaska_wifi"},
    {PLACE_INFLIGHT, 5, "alaskawifi"},  {PLACE_INFLIGHT, 6, "fly-fi"},
    {PLACE_INFLIGHT, 7, "flynet"},      {PLACE_INFLIGHT, 8, "onair"},
    {PLACE_INFLIGHT, GEN, "inflight"},  {PLACE_INFLIGHT, GEN, "in-flight"},
    {PLACE_INFLIGHT, GEN, "onboard wifi"},

    {PLACE_TRAIN, 0, "amtrak"},         {PLACE_TRAIN, 1, "acela"},
    {PLACE_TRAIN, 2, "njtransit"},      {PLACE_TRAIN, 2, "nj transit"},
    {PLACE_TRAIN, 3, "caltrain"},       {PLACE_TRAIN, 4, "metrolink"},
    {PLACE_TRAIN, 5, "bartwifi"},       {PLACE_TRAIN, 5, "bart-wifi"},
    {PLACE_TRAIN, 6, "transitwireless"},{PLACE_TRAIN, 7, "septa"},
    {PLACE_TRAIN, 8, "wmata"},          {PLACE_TRAIN, 9, "brightline"},
    {PLACE_TRAIN, 10, "eurostar"},      {PLACE_TRAIN, 11, "wifionice"},
    {PLACE_TRAIN, GEN, "railway"},      {PLACE_TRAIN, GEN, "train wifi"},
    {PLACE_TRAIN, GEN, "railwifi"},     {PLACE_TRAIN, GEN, "on the train"},

    {PLACE_LIBRARY, 0, "nypl"},         {PLACE_LIBRARY, 1, "lapl"},
    {PLACE_LIBRARY, GEN, "library"},    {PLACE_LIBRARY, GEN, "biblio"},

    {PLACE_UNIVERSITY, GEN, "eduroam"}, {PLACE_UNIVERSITY, GEN, "university"},
    {PLACE_UNIVERSITY, GEN, "college"}, {PLACE_UNIVERSITY, GEN, "campus"},

    {PLACE_HOSPITAL, 0, "kaiser"},      {PLACE_HOSPITAL, 1, "mayo clinic"},
    {PLACE_HOSPITAL, GEN, "hospital"},  {PLACE_HOSPITAL, GEN, "medical center"},
    {PLACE_HOSPITAL, GEN, "clinic"},    {PLACE_HOSPITAL, GEN, "healthcare"},

    {PLACE_MUSEUM, 0, "smithsonian"},   {PLACE_MUSEUM, 1, "moma"},
    {PLACE_MUSEUM, GEN, "museum"},      {PLACE_MUSEUM, GEN, "aquarium"},
    {PLACE_MUSEUM, GEN, "planetarium"}, {PLACE_MUSEUM, GEN, "science center"},

    {PLACE_PARK, 0, "yosemite"},        {PLACE_PARK, 1, "yellowstone"},
    {PLACE_PARK, 2, "grand canyon"},    {PLACE_PARK, 2, "grandcanyon"},
    {PLACE_PARK, 3, "zion"},            {PLACE_PARK, 4, "glacier"},
    {PLACE_PARK, 5, "xanterra"},        {PLACE_PARK, 6, "acadia"},
    {PLACE_PARK, 7, "smokies"},         {PLACE_PARK, 8, "denali"},
    {PLACE_PARK, GEN, "visitor center"},{PLACE_PARK, GEN, "visitorcenter"},
    {PLACE_PARK, GEN, "national park"}, {PLACE_PARK, GEN, "nationalpark"},
    {PLACE_PARK, GEN, "state park"},    {PLACE_PARK, GEN, "ranger station"},

    {PLACE_RESTAURANT, 0, "mcdonalds"},     {PLACE_RESTAURANT, 0, "mcdonald's"},
    {PLACE_RESTAURANT, 1, "taco bell"},     {PLACE_RESTAURANT, 1, "tacobell"},
    {PLACE_RESTAURANT, 2, "five guys"},     {PLACE_RESTAURANT, 2, "fiveguys"},
    {PLACE_RESTAURANT, 3, "chick-fil-a"},   {PLACE_RESTAURANT, 3, "chickfila"},
    {PLACE_RESTAURANT, 4, "wendys"},        {PLACE_RESTAURANT, 4, "wendy's"},
    {PLACE_RESTAURANT, 5, "burger king"},   {PLACE_RESTAURANT, 5, "burgerking"},
    {PLACE_RESTAURANT, 6, "chipotle"},      {PLACE_RESTAURANT, 7, "pizza hut"},
    {PLACE_RESTAURANT, 7, "pizzahut"},      {PLACE_RESTAURANT, 8, "dominos"},
    {PLACE_RESTAURANT, 8, "domino's"},      {PLACE_RESTAURANT, 9, "arbys"},
    {PLACE_RESTAURANT, 9, "arby's"},        {PLACE_RESTAURANT, 10, "popeyes"},
    {PLACE_RESTAURANT, 11, "sonic drive"},  {PLACE_RESTAURANT, 12, "dairy queen"},
    {PLACE_RESTAURANT, 13, "whataburger"},  {PLACE_RESTAURANT, 14, "in-n-out"},
    {PLACE_RESTAURANT, 15, "jack in the box"}, {PLACE_RESTAURANT, 16, "panda express"},
    {PLACE_RESTAURANT, 17, "olive garden"}, {PLACE_RESTAURANT, 18, "applebee"},
    {PLACE_RESTAURANT, 19, "chilis"},       {PLACE_RESTAURANT, 19, "chili's"},
    {PLACE_RESTAURANT, 20, "ihop"},         {PLACE_RESTAURANT, 21, "dennys"},
    {PLACE_RESTAURANT, 21, "denny's"},      {PLACE_RESTAURANT, 22, "waffle house"},
    {PLACE_RESTAURANT, 23, "cracker barrel"}, {PLACE_RESTAURANT, 24, "buffalo wild"},
    {PLACE_RESTAURANT, 25, "red robin"},    {PLACE_RESTAURANT, 26, "culvers"},
    {PLACE_RESTAURANT, 26, "culver's"},     {PLACE_RESTAURANT, 27, "zaxby"},
    {PLACE_RESTAURANT, 28, "raising cane"}, {PLACE_RESTAURANT, 29, "jimmy john"},
    {PLACE_RESTAURANT, 30, "kfc guest"},
    {PLACE_RESTAURANT, GEN, "restaurant"},  {PLACE_RESTAURANT, GEN, "pizza"},
    {PLACE_RESTAURANT, GEN, "pizzeria"},    {PLACE_RESTAURANT, GEN, "grill"},
    {PLACE_RESTAURANT, GEN, "diner"},       {PLACE_RESTAURANT, GEN, "burger"},
    {PLACE_RESTAURANT, GEN, "taqueria"},    {PLACE_RESTAURANT, GEN, "bistro"},
    {PLACE_RESTAURANT, GEN, "bbq"},

    /*
     * Fire, police and rescue. Whole phrases only: "fire" and "police" by
     * themselves turn up in too many joke names on home networks.
     */
    {PLACE_SAFETY, GEN, "fire dep"},        {PLACE_SAFETY, GEN, "firedep"},
    {PLACE_SAFETY, GEN, "fire station"},    {PLACE_SAFETY, GEN, "firestation"},
    {PLACE_SAFETY, GEN, "firehouse"},       {PLACE_SAFETY, GEN, "fire house"},
    {PLACE_SAFETY, GEN, "fire rescue"},     {PLACE_SAFETY, GEN, "fire-rescue"},
    {PLACE_SAFETY, GEN, "fire district"},   {PLACE_SAFETY, GEN, "volunteer fire"},
    {PLACE_SAFETY, GEN, "police dep"},      {PLACE_SAFETY, GEN, "policedep"},
    {PLACE_SAFETY, GEN, "police station"},  {PLACE_SAFETY, GEN, "sheriff"},
    {PLACE_SAFETY, GEN, "public safety"},   {PLACE_SAFETY, GEN, "publicsafety"},
    {PLACE_SAFETY, GEN, "rescue squad"},    {PLACE_SAFETY, GEN, "ambulance"},
};

#define NAMES_COUNT ((int)(sizeof(NAMES) / sizeof(NAMES[0])))

int place_tier(int count)
{
    return count >= 10 ? 3 : (count >= 3 ? 2 : (count >= 1 ? 1 : 0));
}

const char *place_tier_name(int tier)
{
    static const char *const NAME[PLACE_TIER_MAX + 1] = {"", "bronze", "silver", "gold"};
    return tier >= 0 && tier <= PLACE_TIER_MAX ? NAME[tier] : "";
}

const char *place_kind_name(place_kind_t kind)
{
    return kind < PLACE_KIND_COUNT ? KINDS[kind].name : "?";
}

const char *place_kind_how(place_kind_t kind)
{
    return kind < PLACE_KIND_COUNT ? KINDS[kind].how : "";
}

const char *place_kind_key(place_kind_t kind)
{
    return kind < PLACE_KIND_COUNT ? KINDS[kind].key : "";
}

uint16_t place_chain_id(const char *spelling)
{
    uint32_t h = 2166136261u;
    for (; *spelling; spelling++) {
        char ch = *spelling;
        if (ch >= 'A' && ch <= 'Z') {
            ch = (char)(ch + 32);
        }
        h = (h ^ (uint8_t)ch) * 16777619u;
    }
    uint16_t id = (uint16_t)(h ^ (h >> 16));
    return id ? id : 1;
}

/* The first spelling the built-in list has for a chain, or NULL. */
static const char *builtin_first(int kind, int brand)
{
    for (int i = 0; i < NAMES_COUNT; i++) {
        if (NAMES[i].kind == kind && NAMES[i].brand == brand) {
            return NAMES[i].piece;
        }
    }
    return NULL;
}

uint16_t place_legacy_chain(place_kind_t kind, int bit)
{
    const char *first = bit >= 0 && bit < 32 ? builtin_first((int)kind, bit) : NULL;
    return first ? place_chain_id(first) : 0;
}

static place_name_t s_builtin[NAMES_COUNT];
static const place_name_t *s_names;
static int s_names_count;

int place_names_builtin(const place_name_t **list)
{
    if (!s_builtin[0].piece) {
        for (int i = 0; i < NAMES_COUNT; i++) {
            s_builtin[i].kind = NAMES[i].kind;
            s_builtin[i].piece = NAMES[i].piece;
            s_builtin[i].chain = NAMES[i].brand == GEN
                                     ? 0
                                     : place_chain_id(builtin_first(NAMES[i].kind, NAMES[i].brand));
        }
    }
    if (list) {
        *list = s_builtin;
    }
    return NAMES_COUNT;
}

void place_names_set(const place_name_t *list, int count)
{
    if (list) {
        s_names = list;
        s_names_count = count;
    } else {
        s_names_count = place_names_builtin(&s_names);
    }
}

bool place_match_name(const char *ssid, place_kind_t *kind, uint16_t *chain)
{
    char low[40];
    size_t n = 0;
    if (!ssid) {
        return false;
    }
    for (; ssid[n] && n < sizeof(low) - 1; n++) {
        char ch = ssid[n];
        low[n] = (ch >= 'A' && ch <= 'Z') ? (char)(ch + 32) : ch;
    }
    low[n] = '\0';
    if (n < 3) {
        return false;
    }
    if (!s_names) {
        place_names_set(NULL, 0);
    }
    /* A chain wins over a general word: "Starbucks Coffee" is Starbucks. */
    int found = -1;
    for (int i = 0; i < s_names_count; i++) {
        if (strstr(low, s_names[i].piece)) {
            if (s_names[i].chain) {
                found = i;
                break;
            }
            if (found < 0) {
                found = i;
            }
        }
    }
    if (found < 0) {
        return false;
    }
    *kind = (place_kind_t)s_names[found].kind;
    *chain = s_names[found].chain;
    return true;
}

uint16_t place_print(uint32_t salt, const uint8_t *id, size_t len)
{
    uint32_t h = 2166136261u ^ salt;
    for (size_t i = 0; i < len; i++) {
        h = (h ^ id[i]) * 16777619u;
    }
    h ^= salt >> 7;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    uint16_t p = (uint16_t)(h ^ (h >> 16));
    return p ? p : 1;
}

/* ---- Habitats ------------------------------------------------------------ */

void places_init(pet_places_t *p)
{
    memset(p, 0, sizeof(*p));
    p->here = -1;
    p->maybe = -1;
}

static bool has_print(const uint16_t *list, int n, uint16_t print)
{
    for (int i = 0; i < n; i++) {
        if (list[i] == print) {
            return true;
        }
    }
    return false;
}

static int shared(const uint16_t *a, int na, const uint16_t *b, int nb)
{
    int n = 0;
    for (int i = 0; i < na; i++) {
        n += has_print(b, nb, a[i]);
    }
    return n;
}

/* Two networks in common, or one where there are only a couple to go on. */
static int needed(int na, int nb)
{
    return (na < nb ? na : nb) <= 2 ? 1 : 2;
}

static int best_habitat(const pet_places_t *p, const pet_scan_t *scan)
{
    int best = -1, best_shared = 0;
    for (int i = 0; i < HABITAT_MAX; i++) {
        const habitat_t *h = &p->habitat[i];
        if (!h->prints || !scan->prints) {
            continue;
        }
        int s = shared(scan->print, scan->prints, h->print, h->prints);
        if (s >= needed(scan->prints, h->prints) && s > best_shared) {
            best = i;
            best_shared = s;
        }
    }
    return best;
}

/* Keeps a habitat's prints current: routers get replaced and neighbours move. */
static void refresh(pet_places_t *p, habitat_t *h, const pet_scan_t *scan)
{
    bool age = ++p->refresh >= 16;
    if (age) {
        p->refresh = 0;
    }
    for (int i = 0; i < h->prints; i++) {
        if (has_print(scan->print, scan->prints, h->print[i])) {
            if (h->score[i] < 200) {
                h->score[i]++;
            }
        } else if (age && h->score[i] > 0) {
            h->score[i]--;
        }
    }
    for (int i = 0; i < scan->prints; i++) {
        if (has_print(h->print, h->prints, scan->print[i])) {
            continue;
        }
        if (h->prints < HABITAT_PRINTS) {
            h->print[h->prints] = scan->print[i];
            h->score[h->prints++] = 1;
            continue;
        }
        int low = 0;
        for (int k = 1; k < HABITAT_PRINTS; k++) {
            if (h->score[k] < h->score[low]) {
                low = k;
            }
        }
        if (h->score[low] == 0) {
            h->print[low] = scan->print[i];
            h->score[low] = 1;
        }
    }
}

/* A free slot, or failing that the least lived-in habitat the owner never named. */
static int slot_for_new(const pet_places_t *p)
{
    int pick = -1;
    for (int i = 0; i < HABITAT_MAX; i++) {
        const habitat_t *h = &p->habitat[i];
        if (!h->prints) {
            return i;
        }
        if (!h->name[0] && i != places_home(p) &&
            (pick < 0 || h->minutes < p->habitat[pick].minutes)) {
            pick = i;
        }
    }
    return pick;
}

static uint32_t track_habitat(pet_places_t *p, const pet_scan_t *scan, uint32_t now, uint32_t day,
                              uint32_t step)
{
    uint32_t ev = 0;
    int best = best_habitat(p, scan);

    if (p->here >= 0) {
        if (best == p->here) {
            habitat_t *h = &p->habitat[p->here];
            p->here_secs = (uint16_t)(p->here_secs + step);
            h->minutes += p->here_secs / 60;
            p->here_secs %= 60;
            p->here_last = now;
            p->here_misses = 0;
            refresh(p, h, scan);
            return 0;
        }
        if (p->here_misses < UINT8_MAX) {
            p->here_misses++;
        }
        if (now - p->here_last < LEAVE_S || p->here_misses < MISSES) {
            return 0;
        }
        p->here = -1;
        p->maybe = -1;
        p->cand_prints = 0;
        ev |= PLACE_EV_LEFT;
    }

    if (best >= 0) {
        if (p->maybe == best) {
            p->maybe_hits++;
        } else {
            p->maybe = (int8_t)best;
            p->maybe_hits = 1;
        }
        if (p->maybe_hits >= ARRIVE_HITS) {
            p->here = (int8_t)best;
            p->here_last = now;
            p->here_misses = 0;
            p->maybe = -1;
            p->cand_prints = 0;
            if (p->habitat[best].visits < UINT16_MAX) {
                p->habitat[best].visits++;
            }
            ev |= PLACE_EV_ARRIVED;
        }
        return ev;
    }
    p->maybe = -1;

    /* Nowhere it knows. Is it staying put? */
    if (!scan->prints) {
        return ev;
    }
    if (p->cand_prints &&
        shared(scan->print, scan->prints, p->cand, p->cand_prints) >=
            needed(scan->prints, p->cand_prints)) {
        p->cand_last = now;
        p->cand_misses = 0;
        for (int i = 0; i < scan->prints && p->cand_prints < HABITAT_PRINTS; i++) {
            if (!has_print(p->cand, p->cand_prints, scan->print[i])) {
                p->cand[p->cand_prints++] = scan->print[i];
            }
        }
        if (now - p->cand_since >= SETTLE_S) {
            int slot = slot_for_new(p);
            if (slot >= 0) {
                habitat_t *h = &p->habitat[slot];
                memset(h, 0, sizeof(*h));
                memcpy(h->print, p->cand, sizeof(h->print));
                h->prints = p->cand_prints;
                memset(h->score, 2, h->prints);
                h->visits = 1;
                h->minutes = SETTLE_S / 60;
                h->first_day = day;
                p->here = (int8_t)slot;
                p->here_last = now;
                p->here_misses = 0;
                ev |= PLACE_EV_NEW;
            }
            p->cand_prints = 0;
        }
    } else if (!p->cand_prints ||
               (++p->cand_misses >= MISSES && now - p->cand_last > CAND_FORGET_S)) {
        p->cand_misses = 0;
        p->cand_prints = scan->prints < HABITAT_PRINTS ? scan->prints : HABITAT_PRINTS;
        memcpy(p->cand, scan->print, p->cand_prints * sizeof(uint16_t));
        p->cand_since = p->cand_last = now;
    }
    return ev;
}

/*
 * A faint place name was heard. True once it has been heard again long
 * enough after the first time to mean the pet is staying there.
 */
static bool stayed(pet_places_t *p, int kind, uint16_t chain, uint32_t now)
{
    for (int i = 0; i < p->waits; i++) {
        if (p->wait[i].kind == kind && p->wait[i].chain == chain) {
            p->wait[i].last = now;
            return now - p->wait[i].first >= PLACE_STAY_S;
        }
    }
    int slot = p->waits;
    if (slot >= PLACE_WAIT_MAX) {
        slot = 0;       /* full: the one heard longest ago makes way */
        for (int i = 1; i < PLACE_WAIT_MAX; i++) {
            if (p->wait[i].last < p->wait[slot].last) {
                slot = i;
            }
        }
    } else {
        p->waits++;
    }
    p->wait[slot].kind = (uint8_t)kind;
    p->wait[slot].chain = chain;
    p->wait[slot].first = p->wait[slot].last = now;
    return false;
}

static uint32_t track_badges(pet_places_t *p, const pet_scan_t *scan, uint32_t now, uint32_t day,
                             place_kind_t *badge_kind)
{
    uint32_t ev = 0;
    /* Names not heard for a while are forgotten: next time starts the wait again. */
    for (int i = 0; i < p->waits;) {
        if (now - p->wait[i].last > PLACE_WAIT_FORGET_S) {
            p->wait[i] = p->wait[--p->waits];
        } else {
            i++;
        }
    }
    for (int i = 0; i < scan->matches && i < SCAN_MATCHES_MAX; i++) {
        int k = scan->match[i].kind;
        uint16_t chain = scan->match[i].chain;
        if (k >= PLACE_KIND_COUNT) {
            continue;
        }
        if (scan->match[i].faint && !stayed(p, k, chain, now)) {
            continue;
        }
        int before = place_tier(p->kind[k].count);
        if (chain) {
            /* Each chain counts once, ever. */
            bool counted = p->kind[k].chains >= PLACE_CHAINS_MAX;   /* no room to remember more */
            for (int c = 0; c < p->kind[k].chains && !counted; c++) {
                counted = p->kind[k].chain[c] == chain;
            }
            if (counted) {
                continue;
            }
            p->kind[k].chain[p->kind[k].chains++] = chain;
        } else if (p->kind[k].last_day == day) {
            /* A general word counts once a day, and not on top of a chain. */
            continue;
        }
        p->kind[k].last_day = day;
        if (p->kind[k].count < UINT8_MAX) {
            p->kind[k].count++;
        }
        if (place_tier(p->kind[k].count) > before) {
            ev |= PLACE_EV_BADGE;
            if (badge_kind) {
                *badge_kind = (place_kind_t)k;
            }
        }
    }
    return ev;
}

static uint32_t track_quiet(pet_places_t *p, const pet_scan_t *scan, uint32_t now, bool gap)
{
    if (scan->wifi_heard || scan->ble_heard) {
        p->quiet_since = 0;
        return 0;
    }
    if (!p->quiet_since || gap) {
        p->quiet_since = now;       /* after a gap in scanning the silence starts over */
        p->quiet_scans = p->quiet_carried = 0;
        return 0;
    }
    if (p->quiet_scans < UINT16_MAX) {
        p->quiet_scans++;
        p->quiet_carried = (uint16_t)(p->quiet_carried + (scan->motion >= 2));
    }
    if (scan->motion != 0 && p->quiet_carried * 2 < p->quiet_scans) {
        return 0;                   /* silent, but mostly lying still */
    }
    uint32_t quiet = now - p->quiet_since;
    int tier = quiet >= OFFGRID_GOLD_S ? 3 : (quiet >= OFFGRID_SILVER_S ? 2 :
                                              (quiet >= OFFGRID_BRONZE_S ? 1 : 0));
    if (tier > p->offgrid_tier) {
        p->offgrid_tier = (uint8_t)tier;
        return PLACE_EV_OFFGRID;
    }
    return 0;
}

uint32_t places_scan(pet_places_t *p, const pet_scan_t *scan, uint32_t now, uint32_t day,
                     place_kind_t *badge_kind)
{
    uint32_t step = p->last_scan && now > p->last_scan ? now - p->last_scan : 0;
    bool gap = step > SCAN_GAP_MAX_S;
    if (gap) {
        step = 0;
    }
    p->last_scan = now;

    uint32_t ev = track_habitat(p, scan, now, day, step);
    ev |= track_badges(p, scan, now, day, badge_kind);
    ev |= track_quiet(p, scan, now, gap);
    return ev;
}

void places_fed(pet_places_t *p, float value)
{
    if (p->here >= 0) {
        p->habitat[p->here].net_points += value;
    } else {
        p->away_points += value;
    }
}

int places_home(const pet_places_t *p)
{
    int home = -1;
    for (int i = 0; i < HABITAT_MAX; i++) {
        const habitat_t *h = &p->habitat[i];
        if (h->prints && h->minutes >= HOME_MINUTES &&
            (home < 0 || h->minutes > p->habitat[home].minutes)) {
            home = i;
        }
    }
    return home;
}

int places_count(const pet_places_t *p)
{
    int n = 0;
    for (int i = 0; i < HABITAT_MAX; i++) {
        n += p->habitat[i].prints > 0;
    }
    return n;
}

float places_top_share(const pet_places_t *p)
{
    float total = p->away_points + p->carried_total, top = p->carried_top;
    for (int i = 0; i < HABITAT_MAX; i++) {
        if (p->habitat[i].prints) {
            total += p->habitat[i].net_points;
            if (p->habitat[i].net_points > top) {
                top = p->habitat[i].net_points;
            }
        }
    }
    return total >= SHARE_MIN_POINTS ? top / total : -1;
}

void places_label(const pet_places_t *p, int index, char *out, size_t cap)
{
    if (index < 0 || index >= HABITAT_MAX || !p->habitat[index].prints) {
        snprintf(out, cap, "nowhere");
    } else if (p->habitat[index].name[0]) {
        snprintf(out, cap, "%s", p->habitat[index].name);
    } else if (index == places_home(p)) {
        snprintf(out, cap, "Home");
    } else {
        snprintf(out, cap, "Place %d", index + 1);
    }
}
