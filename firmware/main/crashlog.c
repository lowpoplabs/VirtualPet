/*
 * Remembers why the chip restarted. On battery there is no serial port to
 * watch, so a crash leaves a note in memory that survives a restart (not a
 * loss of power), and the next start-up reports it.
 */
#include "crashlog.h"

#include <stdio.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_debug_helpers.h"
#include "esp_log.h"
#include "esp_private/panic_internal.h"
#include "esp_system.h"
#include "xtensa_context.h"

#define CRASH_MAGIC 0x43525348u
#define HISTORY     8
#define TRACE       12

typedef struct {
    uint32_t magic;
    uint32_t boots;             /* restarts counted since power was last lost */
    uint8_t reasons[HISTORY];   /* newest last */
    uint32_t panic_boot;        /* the boot count the note below belongs to; 0 = none */
    uint32_t pc, a0, excvaddr, exccause;
    char reason[96];
    uint32_t trace[TRACE];      /* the chain of calls that led there, innermost first */
} crash_note_t;

static RTC_NOINIT_ATTR crash_note_t s_note;
static const char *TAG = "crash";

void __real_esp_panic_handler(void *info);

/* Runs inside the panic itself: no library calls, nothing that could fault. */
void IRAM_ATTR __wrap_esp_panic_handler(void *arg)
{
    const panic_info_t *info = arg;
    if (s_note.magic == CRASH_MAGIC && info) {
        const XtExcFrame *f = info->frame;
        s_note.panic_boot = s_note.boots;
        s_note.pc = (uint32_t)info->addr;
        s_note.a0 = f ? (uint32_t)f->a0 : 0;
        s_note.excvaddr = f ? (uint32_t)f->excvaddr : 0;
        s_note.exccause = f ? (uint32_t)f->exccause : 0;
        /* A failed check (abort, assert) carries its message separately. */
        const char *why = g_panic_abort && g_panic_abort_details ? g_panic_abort_details
                                                                 : info->reason;
        size_t i = 0;
        for (; why && why[i] && i < sizeof(s_note.reason) - 1; i++) {
            s_note.reason[i] = why[i];
        }
        s_note.reason[i] = '\0';
        for (int t = 0; t < TRACE; t++) {
            s_note.trace[t] = 0;
        }
        if (f) {
            esp_backtrace_frame_t bt = {
                .pc = (uint32_t)f->pc, .sp = (uint32_t)f->a1, .next_pc = (uint32_t)f->a0};
            for (int t = 0; t < TRACE; t++) {
                s_note.trace[t] = bt.pc;
                if (bt.next_pc == 0 || !esp_backtrace_get_next_frame(&bt)) {
                    break;
                }
            }
        }
    }
    __real_esp_panic_handler(arg);
}

static const char *reason_name(int r)
{
    switch (r) {
    case ESP_RST_POWERON:   return "power-on";
    case ESP_RST_SW:        return "restart";
    case ESP_RST_PANIC:     return "crash";
    case ESP_RST_INT_WDT:   return "interrupt-watchdog";
    case ESP_RST_TASK_WDT:  return "task-watchdog";
    case ESP_RST_WDT:       return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep-sleep";
    case ESP_RST_BROWNOUT:  return "brownout";
    case ESP_RST_USB:       return "usb";
    case ESP_RST_JTAG:      return "jtag";
    default:                return "other";
    }
}

void crashlog_boot(char *line, size_t len)
{
    const int why = esp_reset_reason();
    if (s_note.magic != CRASH_MAGIC) {
        memset(&s_note, 0, sizeof(s_note));
        s_note.magic = CRASH_MAGIC;
    }
    memmove(s_note.reasons, s_note.reasons + 1, HISTORY - 1);
    s_note.reasons[HISTORY - 1] = (uint8_t)why;
    s_note.boots++;

    char history[HISTORY * 20] = "";
    size_t used = 0;
    for (int i = 0; i < HISTORY && used < sizeof(history); i++) {
        if (s_note.reasons[i]) {
            used += snprintf(history + used, sizeof(history) - used, "%s%s", used ? ", " : "",
                             reason_name(s_note.reasons[i]));
        }
    }
    ESP_LOGI(TAG, "start %lu since power-on, this one: %s", (unsigned long)s_note.boots,
             reason_name(why));
    ESP_LOGI(TAG, "recent starts, oldest first: %s", history);
    if (s_note.panic_boot) {
        s_note.reason[sizeof(s_note.reason) - 1] = '\0';
        ESP_LOGW(TAG, "last crash (during start %lu): %s, pc 0x%08lx a0 0x%08lx addr 0x%08lx cause %lu",
                 (unsigned long)s_note.panic_boot, s_note.reason, (unsigned long)s_note.pc,
                 (unsigned long)s_note.a0, (unsigned long)s_note.excvaddr,
                 (unsigned long)s_note.exccause);
        char trace[TRACE * 12] = "";
        size_t at = 0;
        for (int t = 0; t < TRACE && s_note.trace[t] && at < sizeof(trace); t++) {
            at += snprintf(trace + at, sizeof(trace) - at, " 0x%08lx", (unsigned long)s_note.trace[t]);
        }
        ESP_LOGW(TAG, "calls:%s", trace);
    }
    /* One line for the card log: this start's reason, and the crash if it was the previous run's. */
    if (line && len) {
        if (s_note.panic_boot && s_note.panic_boot + 1 == s_note.boots) {
            snprintf(line, len, "%s pc=%08lx a0=%08lx addr=%08lx %s", reason_name(why),
                     (unsigned long)s_note.pc, (unsigned long)s_note.a0,
                     (unsigned long)s_note.excvaddr, s_note.reason);
            for (char *c = line; *c; c++) {
                if (*c == ',' || *c == '"' || *c == '\n') {
                    *c = ' ';
                }
            }
        } else {
            snprintf(line, len, "%s", reason_name(why));
        }
    }
}
