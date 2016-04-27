#ifndef foostreammanagerrestrictionprivfoo
#define foostreammanagerrestrictionprivfoo

#include "stream-manager.h"

 /* dbus method args */
 #define STREAM_MANAGER_METHOD_ARGS_BLOCK_RECORDING_MEDIA     "block_recording_media"

int32_t handle_restrictions(pa_stream_manager *m, const char *name, uint32_t value);
bool check_restrictions(pa_stream_manager *m, void *stream, stream_type_t stream_type);

#endif
