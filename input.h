#pragma once
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>

typedef struct {
    struct xkb_context *ctx;
    struct xkb_keymap  *keymap;
    struct xkb_state   *state;
} Input;

Input *input_new(void);
void   input_free(Input *inp);

/* Update keymap from compositor-provided keymap fd */
void   input_set_keymap(Input *inp, int fd, uint32_t size);

/* Update modifier state */
void   input_update_mods(Input *inp,
    uint32_t mods_depressed, uint32_t mods_latched,
    uint32_t mods_locked,   uint32_t group);

/* Translate a keypress to bytes to send to pty.
   Returns number of bytes written to buf (max 32). */
int    input_key(Input *inp, uint32_t key, uint32_t state_val,
                 bool app_cursor, char buf[32]);

/* Return keysym and current Ctrl/Shift state for hotkey detection. */
xkb_keysym_t input_keysym_mods(Input *inp, uint32_t key,
                                bool *ctrl_out, bool *shift_out);
