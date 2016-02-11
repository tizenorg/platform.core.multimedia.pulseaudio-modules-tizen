
#include <pulsecore/core.h>

#define DEVICE_TYPE_SPEAKER                 "builtin-speaker"
#define DEVICE_TYPE_RECEIVER                "builtin-receiver"
#define DEVICE_TYPE_MIC                     "builtin-mic"
#define DEVICE_TYPE_AUDIO_JACK              "audio-jack"
#define DEVICE_TYPE_BT                      "bt"
#define DEVICE_TYPE_HDMI                    "hdmi"
#define DEVICE_TYPE_FORWARDING              "forwarding"
#define DEVICE_TYPE_USB_AUDIO               "usb-audio"
#define DEVICE_TYPE_NONE                    "none"

#define DEVICE_PROFILE_BT_SCO               "sco"
#define DEVICE_PROFILE_BT_A2DP              "a2dp"

#define DEVICE_ROLE_NORMAL                  "normal"
#define DEVICE_ROLE_CALL_VOICE              "call-voice"
#define DEVICE_ROLE_CALL_VIDEO              "call-video"
#define DEVICE_ROLE_VOIP                    "voip"
#define DEVICE_ROLE_LOW_LATENCY             "low-latency"
#define DEVICE_ROLE_HIGH_LATENCY            "high-latency"
#define DEVICE_ROLE_UHQA                    "uhqa"

typedef enum dm_device_direction_type {
    DM_DEVICE_DIRECTION_NONE,
    DM_DEVICE_DIRECTION_IN = 0x1,
    DM_DEVICE_DIRECTION_OUT = 0x2,
    DM_DEVICE_DIRECTION_BOTH = DM_DEVICE_DIRECTION_IN | DM_DEVICE_DIRECTION_OUT
} dm_device_direction_t;

typedef enum dm_device_changed_into_type {
    DM_DEVICE_CHANGED_INFO_STATE,
    DM_DEVICE_CHANGED_INFO_IO_DIRECTION
} dm_device_changed_info_t;

typedef enum dm_device_state_type {
    DM_DEVICE_STATE_DEACTIVATED = 0,
    DM_DEVICE_STATE_ACTIVATED
} dm_device_state_t;

typedef enum dm_device_bt_sco_status_type {
    DM_DEVICE_BT_SCO_STATUS_DISCONNECTED = 0,
    DM_DEVICE_BT_SCO_STATUS_CONNECTED,
    DM_DEVICE_BT_SCO_STATUS_OPENED
} dm_device_bt_sco_status_t;

typedef struct pa_device_manager pa_device_manager;
typedef struct dm_device dm_device;

typedef struct _hook_call_data_for_conn_changed {
    unsigned event_id;
    pa_bool_t is_connected;
    dm_device *device;
} pa_device_manager_hook_data_for_conn_changed;

typedef struct _hook_call_data_for_info_changed {
    dm_device_changed_info_t changed_info;
    dm_device *device;
} pa_device_manager_hook_data_for_info_changed;

pa_device_manager* pa_device_manager_get(pa_core* c);
pa_device_manager* pa_device_manager_ref(pa_device_manager *dm);
void pa_device_manager_unref(pa_device_manager *dm);

/* get device or list */
pa_idxset* pa_device_manager_get_device_list(pa_device_manager *dm);
dm_device* pa_device_manager_get_device(pa_device_manager *dm, const char *device_type);
dm_device* pa_device_manager_get_device_by_id(pa_device_manager *dm, uint32_t id);

/* query device */
pa_sink* pa_device_manager_get_sink(dm_device *device_item, const char *role);
pa_source* pa_device_manager_get_source(dm_device *device_item, const char *role);
uint32_t pa_device_manager_get_device_id(dm_device *device_item);
const char* pa_device_manager_get_device_type(dm_device *device_item);
const char* pa_device_manager_get_device_subtype(dm_device *device_item);
dm_device_direction_t pa_device_manager_get_device_direction(dm_device *device_item);
pa_usec_t pa_device_manager_get_device_creation_time(dm_device *device_item);
bool pa_device_manager_is_device_use_internal_codec(dm_device *device_item, dm_device_direction_t direction, const char *role);

/* set/get device state */
void pa_device_manager_set_device_state(dm_device *device_item, dm_device_direction_t direction, dm_device_state_t state);
dm_device_state_t pa_device_manager_get_device_state(dm_device *device_item, dm_device_direction_t direction);

/* get device with sink or source */
dm_device* pa_device_manager_get_device_with_sink(pa_sink *sink);
dm_device* pa_device_manager_get_device_with_source(pa_source *source);

/* load pulse device */
int pa_device_manager_load_sink(pa_device_manager *dm, const char *device_type, const char *device_profile, const char *role);
int pa_device_manager_load_source(pa_device_manager *dm, const char *device_type, const char *device_profile, const char *role);

/* bt sco control/query */
int pa_device_manager_bt_sco_open(pa_device_manager *dm);
void pa_device_manager_bt_sco_get_status(pa_device_manager *dm, dm_device_bt_sco_status_t *status);
int pa_device_manager_bt_sco_close(pa_device_manager *dm);
int pa_device_manager_bt_sco_get_property(pa_device_manager *dm, bool *is_wide_band, bool *nrec);
