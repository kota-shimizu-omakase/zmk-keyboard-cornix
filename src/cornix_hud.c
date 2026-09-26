/*
 * Push layer and BLE profile state to a host companion app over raw HID.
 *
 * Report (CONFIG_RAW_HID_REPORT_SIZE bytes, keyboard -> host):
 *   [0] 0xCD magic
 *   [1] protocol version (1)
 *   [2] 0x01 state message
 *   [3..6] active layer bitmask by layer id, little endian
 *   [7] highest active layer id
 *   [8] active BLE profile index
 *   [9] 1 if the active profile is connected
 *
 * The host sends [0xCD, 0x01] to ask for the current state, so it can sync
 * on connect instead of waiting for the next change.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <string.h>

#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>
#include <raw_hid/events.h>

#if IS_ENABLED(CONFIG_ZMK_BLE)
#include <zmk/ble.h>
#include <zmk/events/ble_active_profile_changed.h>
#endif

#define HUD_MAGIC 0xCD
#define HUD_VERSION 1
#define HUD_MSG_STATE 0x01
#define HUD_REQ_STATE 0x01

static uint8_t report[CONFIG_RAW_HID_REPORT_SIZE];

/* Sends go through the system workqueue: the host's request arrives on the
 * Bluetooth RX thread, and bursts of layer changes coalesce into one report
 * carrying the latest state. */
static void send_state(struct k_work *work) {
    memset(report, 0, sizeof(report));
    report[0] = HUD_MAGIC;
    report[1] = HUD_VERSION;
    report[2] = HUD_MSG_STATE;
    sys_put_le32(zmk_keymap_layer_state(), &report[3]);
    report[7] = zmk_keymap_layer_index_to_id(zmk_keymap_highest_layer_active());
#if IS_ENABLED(CONFIG_ZMK_BLE)
    report[8] = zmk_ble_active_profile_index();
    report[9] = zmk_ble_active_profile_is_connected();
#endif
    raise_raw_hid_sent_event((struct raw_hid_sent_event){.data = report, .length = sizeof(report)});
}

static K_WORK_DEFINE(send_state_work, send_state);

static int cornix_hud_listener(const zmk_event_t *eh) {
    const struct raw_hid_received_event *rx = as_raw_hid_received_event(eh);
    if (rx) {
        if (rx->length >= 2 && rx->data[0] == HUD_MAGIC && rx->data[1] == HUD_REQ_STATE) {
            k_work_submit(&send_state_work);
        }
        return ZMK_EV_EVENT_BUBBLE;
    }

    k_work_submit(&send_state_work);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(cornix_hud, cornix_hud_listener);
ZMK_SUBSCRIPTION(cornix_hud, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(cornix_hud, raw_hid_received_event);
#if IS_ENABLED(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(cornix_hud, zmk_ble_active_profile_changed);
#endif
