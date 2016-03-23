#ifndef foostreammanagervolumefoo
#define foostreammanagervolumefoo

#include "stream-manager.h"

int32_t pa_stream_manager_volume_get_level(pa_stream_manager *m, stream_type_t stream_type, const char *volume_type, uint32_t *volume_level);
int32_t pa_stream_manager_volume_set_level(pa_stream_manager *m, stream_type_t stream_type, const char *volume_type, uint32_t volume_level);
int32_t pa_stream_manager_volume_get_mute(pa_stream_manager *m, stream_type_t stream_type, const char *volume_type, bool *volume_mute);
int32_t pa_stream_manager_volume_set_mute(pa_stream_manager *m, stream_type_t stream_type, const char *volume_type, bool volume_mute);
int32_t pa_stream_manager_volume_get_max_level(pa_stream_manager *m, stream_type_t stream_type, const char *volume_type, uint32_t *volume_level);
int32_t pa_stream_manager_volume_get_mute_by_idx(pa_stream_manager *m, stream_type_t stream_type, uint32_t stream_idx, bool *volume_mute);
int32_t pa_stream_manager_volume_set_mute_by_idx(pa_stream_manager *m, stream_type_t stream_type, uint32_t stream_idx, bool volume_mute);

#endif
