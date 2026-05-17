#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "input.h"

Input *input_new(void) {
    Input *inp = calloc(1, sizeof *inp);
    inp->ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    return inp;
}

void input_free(Input *inp) {
    if (inp->state)  xkb_state_unref(inp->state);
    if (inp->keymap) xkb_keymap_unref(inp->keymap);
    xkb_context_unref(inp->ctx);
    free(inp);
}

void input_set_keymap(Input *inp, int fd, uint32_t size) {
    char *str = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    if (str == MAP_FAILED) return;

    struct xkb_keymap *km = xkb_keymap_new_from_string(
        inp->ctx, str, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(str, size);
    close(fd);
    if (!km) return;

    struct xkb_state *st = xkb_state_new(km);
    if (inp->state)  xkb_state_unref(inp->state);
    if (inp->keymap) xkb_keymap_unref(inp->keymap);
    inp->keymap = km;
    inp->state  = st;
}

void input_update_mods(Input *inp,
    uint32_t dep, uint32_t lat, uint32_t lock, uint32_t group) {
    if (inp->state)
        xkb_state_update_mask(inp->state, dep, lat, lock, 0, 0, group);
}

/* build ESC [ 1 ; mod X  (xterm modifier-encoded cursor/function key) */
static int mod_cursor(char *buf, char final, int mod) {
    buf[0] = '\x1b'; buf[1] = '['; buf[2] = '1'; buf[3] = ';';
    buf[4] = '0' + mod; buf[5] = final;
    return 6;
}

/* build ESC [ code ~ or ESC [ code ; mod ~ */
static int mod_tilde(char *buf, int code, int mod) {
    if (mod == 1)
        return snprintf(buf, 32, "\x1b[%d~", code);
    return snprintf(buf, 32, "\x1b[%d;%d~", code, mod);
}

xkb_keysym_t input_keysym_mods(Input *inp, uint32_t key,
                               bool *ctrl_out, bool *shift_out) {
    if (!inp->state) { *ctrl_out = *shift_out = false; return XKB_KEY_NoSymbol; }
    xkb_keycode_t kc = key + 8;
    *ctrl_out  = xkb_state_mod_name_is_active(inp->state, XKB_MOD_NAME_CTRL,
                     XKB_STATE_MODS_EFFECTIVE) > 0;
    *shift_out = xkb_state_mod_name_is_active(inp->state, XKB_MOD_NAME_SHIFT,
                     XKB_STATE_MODS_EFFECTIVE) > 0;
    return xkb_state_key_get_one_sym(inp->state, kc);
}

int input_key(Input *inp, uint32_t key, uint32_t key_state,
              bool app_cursor, bool app_keypad, char buf[32]) {
    if (!inp->state || key_state == 0 /* released */) return 0;

    xkb_keycode_t kc = key + 8;
    xkb_keysym_t  ks = xkb_state_key_get_one_sym(inp->state, kc);

    bool ctrl  = xkb_state_mod_name_is_active(inp->state,
                     XKB_MOD_NAME_CTRL,  XKB_STATE_MODS_EFFECTIVE) > 0;
    bool shift = xkb_state_mod_name_is_active(inp->state,
                     XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0;
    bool alt   = xkb_state_mod_name_is_active(inp->state,
                     XKB_MOD_NAME_ALT,   XKB_STATE_MODS_EFFECTIVE) > 0;

    /* modifier parameter (xterm format: 1=none,2=shift,3=alt,4=alt+shift,
       5=ctrl,6=ctrl+shift,7=ctrl+alt,8=ctrl+alt+shift) */
    int  mod     = 1 + (shift?1:0) + (alt?2:0) + (ctrl?4:0);
    bool has_mod = (mod > 1);

    /* application keypad mode */
    if (app_keypad) {
        const char *kpseq = NULL;
        switch (ks) {
        case XKB_KEY_KP_0:         kpseq = "\x1bOp"; break;
        case XKB_KEY_KP_1:         kpseq = "\x1bOq"; break;
        case XKB_KEY_KP_2:         kpseq = "\x1bOr"; break;
        case XKB_KEY_KP_3:         kpseq = "\x1bOs"; break;
        case XKB_KEY_KP_4:         kpseq = "\x1bOt"; break;
        case XKB_KEY_KP_5:         kpseq = "\x1bOu"; break;
        case XKB_KEY_KP_6:         kpseq = "\x1bOv"; break;
        case XKB_KEY_KP_7:         kpseq = "\x1bOw"; break;
        case XKB_KEY_KP_8:         kpseq = "\x1bOx"; break;
        case XKB_KEY_KP_9:         kpseq = "\x1bOy"; break;
        case XKB_KEY_KP_Decimal:   kpseq = "\x1bOn"; break;
        case XKB_KEY_KP_Separator: kpseq = "\x1bOl"; break;
        case XKB_KEY_KP_Add:       kpseq = "\x1bOk"; break;
        case XKB_KEY_KP_Subtract:  kpseq = "\x1bOm"; break;
        case XKB_KEY_KP_Multiply:  kpseq = "\x1bOj"; break;
        case XKB_KEY_KP_Divide:    kpseq = "\x1bOo"; break;
        case XKB_KEY_KP_Enter:     kpseq = "\x1bOM"; break;
        default: break;
        }
        if (kpseq) { memcpy(buf, kpseq, 3); return 3; }
    }

    /* special keys → escape sequences */
    const char *seq = NULL;
    switch (ks) {
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        if (alt) { buf[0]='\x1b'; buf[1]='\r'; return 2; }
        buf[0]='\r'; return 1;

    case XKB_KEY_BackSpace:
        if (alt) { buf[0]='\x1b'; buf[1]='\x7f'; return 2; }
        buf[0]='\x7f'; return 1;

    case XKB_KEY_Tab:
        if (shift) { seq = "\x1b[Z"; break; } /* Back-Tab */
        if (alt)   { buf[0]='\x1b'; buf[1]='\t'; return 2; }
        buf[0]='\t'; return 1;

    case XKB_KEY_Escape:
        buf[0]='\x1b'; return 1;

    case XKB_KEY_Up:
        if (has_mod) return mod_cursor(buf, 'A', mod);
        seq = app_cursor ? "\x1bOA" : "\x1b[A"; break;
    case XKB_KEY_Down:
        if (has_mod) return mod_cursor(buf, 'B', mod);
        seq = app_cursor ? "\x1bOB" : "\x1b[B"; break;
    case XKB_KEY_Right:
        if (has_mod) return mod_cursor(buf, 'C', mod);
        seq = app_cursor ? "\x1bOC" : "\x1b[C"; break;
    case XKB_KEY_Left:
        if (has_mod) return mod_cursor(buf, 'D', mod);
        seq = app_cursor ? "\x1bOD" : "\x1b[D"; break;
    case XKB_KEY_Home:
        if (has_mod) return mod_cursor(buf, 'H', mod);
        seq = app_cursor ? "\x1bOH" : "\x1b[H"; break;
    case XKB_KEY_End:
        if (has_mod) return mod_cursor(buf, 'F', mod);
        seq = app_cursor ? "\x1bOF" : "\x1b[F"; break;

    case XKB_KEY_Page_Up:   return mod_tilde(buf, 5, mod);
    case XKB_KEY_Page_Down: return mod_tilde(buf, 6, mod);
    case XKB_KEY_Insert:    return mod_tilde(buf, 2, mod);
    case XKB_KEY_Delete:    return mod_tilde(buf, 3, mod);

    case XKB_KEY_F1:
        if (has_mod) return mod_cursor(buf, 'P', mod);
        seq = "\x1bOP"; break;
    case XKB_KEY_F2:
        if (has_mod) return mod_cursor(buf, 'Q', mod);
        seq = "\x1bOQ"; break;
    case XKB_KEY_F3:
        if (has_mod) return mod_cursor(buf, 'R', mod);
        seq = "\x1bOR"; break;
    case XKB_KEY_F4:
        if (has_mod) return mod_cursor(buf, 'S', mod);
        seq = "\x1bOS"; break;
    case XKB_KEY_F5:  return mod_tilde(buf, 15, mod);
    case XKB_KEY_F6:  return mod_tilde(buf, 17, mod);
    case XKB_KEY_F7:  return mod_tilde(buf, 18, mod);
    case XKB_KEY_F8:  return mod_tilde(buf, 19, mod);
    case XKB_KEY_F9:  return mod_tilde(buf, 20, mod);
    case XKB_KEY_F10: return mod_tilde(buf, 21, mod);
    case XKB_KEY_F11: return mod_tilde(buf, 23, mod);
    case XKB_KEY_F12: return mod_tilde(buf, 24, mod);

    default: break;
    }

    if (seq) {
        int n = (int)strlen(seq);
        memcpy(buf, seq, (size_t)n);
        /* prepend ESC for Alt+special sequences that don't already encode it */
        return n;
    }

    /* ctrl+letter */
    if (ctrl && ks >= 'a' && ks <= 'z') {
        char c = (char)(ks - 'a' + 1);
        if (alt) { buf[0]='\x1b'; buf[1]=c; return 2; }
        buf[0] = c; return 1;
    }
    if (ctrl && ks >= 'A' && ks <= 'Z') {
        char c = (char)(ks - 'A' + 1);
        if (alt) { buf[0]='\x1b'; buf[1]=c; return 2; }
        buf[0] = c; return 1;
    }
    /* special ctrl combos */
    if (ctrl && ks == '[')  { buf[0]='\x1b'; return 1; }
    if (ctrl && ks == '\\') { buf[0]='\x1c'; return 1; }
    if (ctrl && ks == ']')  { buf[0]='\x1d'; return 1; }
    if (ctrl && ks == '^')  { buf[0]='\x1e'; return 1; }
    if (ctrl && ks == '_')  { buf[0]='\x1f'; return 1; }
    if (ctrl && ks == ' ')  { buf[0]='\0';   return 1; }

    /* printable: get UTF-8 from xkbcommon */
    int n = xkb_state_key_get_utf8(inp->state, kc, buf, 32);
    if (n > 0 && (unsigned char)buf[0] >= 0x20) {
        /* Alt+ASCII: prepend ESC (but not for AltGr-composed multi-byte output) */
        if (alt && n == 1 && (unsigned char)buf[0] < 0x80 && n < 31) {
            buf[1] = buf[0]; buf[0] = '\x1b';
            return 2;
        }
        return n;
    }

    return 0;
}
