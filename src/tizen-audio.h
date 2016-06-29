/*
 * audio-hal
 *
 * Copyright (c) 2015 - 2016 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef footizenaudiofoo
#define footizenaudiofoo

#include <stdint.h>

/**
 * @file tizen-audio.h
 * @brief This file contains the Audio Hardware Abstraction Layer Interfaces.
 */

/**
 * @addtogroup TIZEN_AUDIO_HAL_MODULE
 * @{
 */

/**
 * @brief Enumeration for return codes.
 * @since_tizen 3.0
 */
typedef enum audio_return {
    AUDIO_RET_OK                        = 0,
    AUDIO_ERR_UNDEFINED                 = (int32_t)0x80001000,
    AUDIO_ERR_RESOURCE                  = (int32_t)0x80001001,
    AUDIO_ERR_PARAMETER                 = (int32_t)0x80001002,
    AUDIO_ERR_IOCTL                     = (int32_t)0x80001003,
    AUDIO_ERR_INVALID_STATE             = (int32_t)0x80001004,
    AUDIO_ERR_INTERNAL                  = (int32_t)0x80001005,
    /* add new enemerator here */
    AUDIO_ERR_NOT_IMPLEMENTED           = (int32_t)0x80001100,
} audio_return_t ;

/**
 * @brief Enumeration for audio direction.
 * @since_tizen 3.0
 */
typedef enum audio_direction {
    AUDIO_DIRECTION_IN,                 /**< Capture */
    AUDIO_DIRECTION_OUT,                /**< Playback */
} audio_direction_t;

/**
 * @brief Device information including type, direction and id.
 * @since_tizen 3.0
 */
typedef struct device_info {
    const char *type;
    uint32_t direction;
    uint32_t id;
} device_info_t;

/**
 * @brief Volume information including type, gain and direction.
 * @since_tizen 3.0
 */
typedef struct audio_volume_info {
    const char *type;
    const char *gain;
    uint32_t direction;
} audio_volume_info_t ;

/**
 * @brief Route information including role and device.
 * @since_tizen 3.0
 */
typedef struct audio_route_info {
    const char *role;
    device_info_t *device_infos;
    uint32_t num_of_devices;
} audio_route_info_t;

/**
 * @brief Route option including role, name and value.
 * @since_tizen 3.0
 */
typedef struct audio_route_option {
    const char *role;
    const char *name;
    int32_t value;
} audio_route_option_t;

/**
 * @brief Stream information including role, direction and index.
 * @since_tizen 3.0
 */
typedef struct audio_stream_info {
    const char *role;
    uint32_t direction;
    uint32_t idx;
} audio_stream_info_t ;

/**
 * @brief Called when audio hal implementation needs to send a message. (optional)
 * @since_tizen 3.0
 * @param[in] name The message name
 * @param[in] value The message value
 * @param[in] user_data The user data passed from the callback registration function
 *
 * @remarks Some audio hal implementation may not have these functions.\n
 * (@c message_cb, @c audio_add_message_cb and @c audio_remove_message_cb)
 *
 * @see audio_add_message_cb()
 * @see audio_remove_message_cb()
 */
typedef void (*message_cb)(const char *name, int value, void *user_data);

/* Overall */
typedef struct audio_interface {
    /* Initialization & de-initialization */
    audio_return_t (*init)(void **audio_handle);
    audio_return_t (*deinit)(void *audio_handle);
    /* Volume */
    audio_return_t (*get_volume_level_max)(void *audio_handle, audio_volume_info_t *info, uint32_t *level);
    audio_return_t (*get_volume_level)(void *audio_handle, audio_volume_info_t *info, uint32_t *level);
    audio_return_t (*set_volume_level)(void *audio_handle, audio_volume_info_t *info, uint32_t level);
    audio_return_t (*get_volume_value)(void *audio_handle, audio_volume_info_t *info, uint32_t level, double *value);
    audio_return_t (*get_volume_mute)(void *audio_handle, audio_volume_info_t *info, uint32_t *mute);
    audio_return_t (*set_volume_mute)(void *audio_handle, audio_volume_info_t *info, uint32_t mute);
    /* Routing */
    audio_return_t (*update_route)(void *audio_handle, audio_route_info_t *info);
    audio_return_t (*update_route_option)(void *audio_handle, audio_route_option_t *option);
    /* Stream */
    audio_return_t (*notify_stream_connection_changed)(void *audio_handle, audio_stream_info_t *info, uint32_t is_connected);
    /* PCM */
    audio_return_t (*pcm_open)(void *audio_handle, void **pcm_handle, uint32_t direction, void *sample_spec, uint32_t period_size, uint32_t periods);
    audio_return_t (*pcm_start)(void *audio_handle, void *pcm_handle);
    audio_return_t (*pcm_stop)(void *audio_handle, void *pcm_handle);
    audio_return_t (*pcm_close)(void *audio_handle, void *pcm_handle);
    audio_return_t (*pcm_avail)(void *audio_handle, void *pcm_handle, uint32_t *avail);
    audio_return_t (*pcm_write)(void *audio_handle, void *pcm_handle, const void *buffer, uint32_t frames);
    audio_return_t (*pcm_read)(void *audio_handle, void *pcm_handle, void *buffer, uint32_t frames);
    audio_return_t (*pcm_get_fd)(void *audio_handle, void *pcm_handle, int *fd);
    audio_return_t (*pcm_recover)(void *audio_handle, void *pcm_handle, int revents);
    audio_return_t (*pcm_get_params)(void *audio_handle, void *pcm_handle, uint32_t direction, void **sample_spec, uint32_t *period_size, uint32_t *periods);
    audio_return_t (*pcm_set_params)(void *audio_handle, void *pcm_handle, uint32_t direction, void *sample_spec, uint32_t period_size, uint32_t periods);
    /* Message callback (optional) */
    audio_return_t (*add_message_cb)(void *audio_handle, message_cb callback, void *user_data);
    audio_return_t (*remove_message_cb)(void *audio_handle, message_cb callback);
} audio_interface_t;

/**
 * @brief Initializes audio hal.
 * @since_tizen 3.0
 * @param[out] audio_handle The audio hal handle
 *
 * @return @c 0 on success,
 *         otherwise a negative error value
 * @retval #AUDIO_RET_OK Success
 * @see audio_deinit()
 */
audio_return_t audio_init(void **audio_handle);

/**
 * @brief De-initializes audio hal.
 * @since_tizen 3.0
 * @param[in] audio_handle The audio hal handle
 *
 * @return @c 0 on success,
 *         otherwise a negative error value
 * @retval #AUDIO_RET_OK Success
 * @see audio_init()
 */
audio_return_t audio_deinit(void *audio_handle);

/**
 * @brief Gets the maximum volume level supported for a particular volume information.
 * @since_tizen 3.0
 * @param[in] audio_handle The audio hal handle
 * @param[in] info The audio volume information
 * @param[out] level The maximum volume level
 *
 * @return @c 0 on success,
 *         otherwise a negative error value
 * @retval #AUDIO_RET_OK Success
 * @see audio_set_volume_level()
 * @see audio_get_volume_level()
 * @see audio_get_volume_value()
 */
audio_return_t audio_get_volume_level_max(void *audio_handle, audio_volume_info_t *info, uint32_t *level);

/**
 * @brief Gets the volume level specified for a particular volume information.
 * @since_tizen 3.0
 * @param[in] audio_handle The audio hal handle
 * @param[in] info The audio volume information
 * @param[out] level The current volume level
 *
 * @return @c 0 on success,
 *         otherwise a negative error value
 * @retval #AUDIO_RET_OK Success
 * @see audio_set_volume_level()
 * @see audio_get_volume_level_max()
 * @see audio_get_volume_value()
 */
audio_return_t audio_get_volume_level(void *audio_handle, audio_volume_info_t *info, uint32_t *level);

/**
 * @brief Sets the volume level specified for a particular volume information.
 * @since_tizen 3.0
 * @param[in] audio_handle The audio hal handle
 * @param[in] info The audio volume information
 * @param[in] level The volume level to be set
 *
 * @return @c 0 on success,
 *         otherwise a negative error value
 * @retval #AUDIO_RET_OK Success
 * @see audio_get_volume_level()
 * @see audio_get_volume_level_max()
 * @see audio_get_volume_value()
 */
audio_return_t audio_set_volume_level(void *audio_handle, audio_volume_info_t *info, uint32_t level);

/**
 * @brief Gets the volume value specified for a particular volume information and level.
 * @since_tizen 3.0
 * @param[in] audio_handle The audio hal handle
 * @param[in] info The audio volume information
 * @param[in] level The volume level
 * @param[out] value The volume value (range is from 0.0 to 1.0 inclusive, 1.0 = 100%)
 *
 * @return @c 0 on success,
 *         otherwise a negative error value
 * @retval #AUDIO_RET_OK Success
 * @see audio_set_volume_level()
 * @see audio_get_volume_level()
 * @see audio_get_volume_level_max()
 */
audio_return_t audio_get_volume_value(void *audio_handle, audio_volume_info_t *info, uint32_t level, double *value);

/**
 * @brief Gets the volume mute specified for a particular volume information.
 * @since_tizen 3.0
 * @param[in] audio_handle The audio hal handle
 * @param[in] info The audio volume information
 * @param[out] mute The volume mute state : (@c 0 = unmute, @c 1 = mute)
 *
 * @return @c 0 on success,
 *         otherwise a negative error value
 * @retval #AUDIO_RET_OK Success
 * @see audio_set_volume_mute()
 */
audio_return_t audio_get_volume_mute(void *audio_handle, audio_volume_info_t *info, uint32_t *mute);

/**
 * @brief Sets the volume mute specified for a particular volume information.
 * @since_tizen 3.0
 * @param[in] audio_handle The audio hal handle
 * @param[in] info The audio volume information
 * @param[in] mute The volume mute state to be set : (@c 0 = unmute, @c 1 = mute)
 *
 * @return @c 0 on success,
 *         otherwise a negative error value
 * @retval #AUDIO_RET_OK Success
 * @see audio_get_volume_mute()
 */
audio_return_t audio_set_volume_mute(void *audio_handle, audio_volume_info_t *info, uint32_t mute);

/**
 * @brief Updates the audio routing according to audio route information.
 * @since_tizen 3.0
 * @param[in] audio_handle The audio hal handle
 * @param[in] info The audio route information including role and devices
 *
 * @return @c 0 on success,
 *         otherwise a negative error value
 * @retval #AUDIO_RET_OK Success
 * @see audio_update_route_option()
 */
audio_return_t audio_update_route(void *audio_handle, audio_route_info_t *info);

/**
 * @brief Updates audio routing option according to audio route option.
 * @since_tizen 3.0
 * @param[in] audio_handle The audio hal handle
 * @param[in] option The option that can be used for audio routing including role, name and value
 *
 * @remarks This option can be used for audio routing.\n
 * It is recommended to apply this option for routing per each role.
 *
 * @return @c 0 on success,
 *         otherwise a negative error value
 * @retval #AUDIO_RET_OK Success
 * @see audio_update_route()
 */
audio_return_t audio_update_route_option(void *audio_handle, audio_route_option_t *option);

/**
 * @brief Gets notified when a stream is connected and disconnected.
 * @since_tizen 3.0
 * @param[in] audio_handle The audio hal handle
 * @param[in] info The stream information including role, direction, index
 * @param[in] is_connected The connection state of this stream (@c true = connected, @c false = disconnected)
 *
 * @remarks This information can be used for audio routing, volume controls and so on.
 *
 * @return @c 0 on success,
 *         otherwise a negative error value
 * @retval #AUDIO_RET_OK Success
 */
audio_return_t audio_notify_stream_connection_changed(void *audio_handle, audio_stream_info_t *info, uint32_t is_connected);

/**
 * @brief Opens a PCM device.
 * @since_tizen 3.0
 * @param[in] audio_handle The audio hal handle
 * @param[out] pcm_handle The PCM handle
 * @param[in] direction The direction of PCM
 * @param[in] sample_spec The sample specification
 * @param[in] period_size The period size
 * @param[in] periods The periods
 *
 * @return @c 0 on success,
 *         otherwise a negative error value
 * @retval #AUDIO_RET_OK Success
 * @see audio_pcm_close()
 */
audio_return_t audio_pcm_open(void *audio_handle, void **pcm_handle, uint32_t direction, void *sample_spec, uint32_t period_size, uint32_t periods);

/**
 * @brief Starts a PCM device.
 * @since_tizen 3.0
 * @param[in] audio_handle The audio hal handle
 * @param[in] pcm_handle The PCM handle to be started
 *
 * @return @c 0 on success,
 *         otherwise a negative error value
 * @retval #AUDIO_RET_OK Success
 * @see audio_pcm_avail()
 * @see audio_pcm_write()
 * @see audio_pcm_read()
 * @see audio_pcm_stop()
 * @see audio_pcm_recover()
 */
audio_return_t audio_pcm_start(void *audio_handle, void *pcm_handle);

/**
 * @brief Stops a PCM device.
 * @since_tizen 3.0
 * @param[in] audio_handle The audio hal handle
 * @param[in] pcm_handle The PCM handle to be stopped
 *
 * @return @c 0 on success,
 *         otherwise a negative error value
 * @retval #AUDIO_RET_OK Success
 * @see audio_pcm_start()
 */
audio_return_t audio_pcm_stop(void *audio_handle, void *pcm_handle);

/**
 * @brief Closes a PCM device.
 * @since_tizen 3.0
 * @param[in] audio_handle The audio hal handle
 * @param[in] pcm_handle The PCM handle to be closed
 *
 * @return @c 0 on success,
 *         otherwise a negative error value
 * @retval #AUDIO_RET_OK Success
 * @see audio_pcm_open()
 */
audio_return_t audio_pcm_close(void *audio_handle, void *pcm_handle);

/**
 * @brief Gets available number of frames.
 * @since_tizen 3.0
 * @param[in] audio_handle The audio hal handle
 * @param[in] pcm_handle The PCM handle
 * @param[out] avail The available number of frames
 *
 * @return @c 0 on success,
 *         otherwise a negative error value
 * @retval #AUDIO_RET_OK Success
 * @see audio_pcm_write()
 * @see audio_pcm_read()
 */
audio_return_t audio_pcm_avail(void *audio_handle, void *pcm_handle, uint32_t *avail);

/**
 * @brief Writes frames to a PCM device.
 * @since_tizen 3.0
 * @param[in] audio_handle The audio hal handle
 * @param[in] pcm_handle The PCM handle
 * @param[in] buffer The buffer containing frames
 * @param[in] frames The number of frames to be written
 *
 * @return @c 0 on success,
 *         otherwise a negative error value
 * @retval #AUDIO_RET_OK Success
 * @see audio_pcm_avail()
 * @see audio_pcm_recover()
 */
audio_return_t audio_pcm_write(void *audio_handle, void *pcm_handle, const void *buffer, uint32_t frames);

/**
 * @brief Reads frames from a PCM device.
 * @since_tizen 3.0
 * @param[in] audio_handle The audio hal handle
 * @param[in] pcm_handle The PCM handle
 * @param[out] buffer The buffer containing frames
 * @param[in] frames The number of frames to be read
 *
 * @return @c 0 on success,
 *         otherwise a negative error value
 * @retval #AUDIO_RET_OK Success
 * @see audio_pcm_avail()
 * @see audio_pcm_recover()
 */
audio_return_t audio_pcm_read(void *audio_handle, void *pcm_handle, void *buffer, uint32_t frames);

/**
 * @brief Gets poll descriptor for a PCM handle.
 * @since_tizen 3.0
 * @param[in] audio_handle The audio hal handle
 * @param[in] pcm_handle The PCM handle
 * @param[out] fd The poll descriptor
 *
 * @return @c 0 on success,
 *         otherwise a negative error value
 * @retval #AUDIO_RET_OK Success
 * @see audio_pcm_open()
 * @see audio_pcm_recover()
 */
audio_return_t audio_pcm_get_fd(void *audio_handle, void *pcm_handle, int *fd);

/**
 * @brief Recovers the PCM state.
 * @since_tizen 3.0
 * @param[in] audio_handle The audio hal handle
 * @param[in] pcm_handle The PCM handle
 * @param[in] revents The returned event from pollfd
 *
 * @return @c 0 on success,
 *         otherwise a negative error value
 * @retval #AUDIO_RET_OK Success
 * @see audio_pcm_start()
 * @see audio_pcm_write()
 * @see audio_pcm_read()
 * @see audio_pcm_get_fd()
 */
audio_return_t audio_pcm_recover(void *audio_handle, void *pcm_handle, int revents);

/**
 * @brief Gets parameters of a PCM device.
 * @since_tizen 3.0
 * @param[in] audio_handle The audio hal handle
 * @param[in] pcm_handle The PCM handle
 * @param[in] direction The direction of PCM
 * @param[out] sample_spec The sample specification
 * @param[out] period_size The period size
 * @param[out] periods The periods
 *
 * @return @c 0 on success,
 *         otherwise a negative error value
 * @retval #AUDIO_RET_OK Success
 * @see audio_pcm_set_params()
 */
audio_return_t audio_pcm_get_params(void *audio_handle, void *pcm_handle, uint32_t direction, void **sample_spec, uint32_t *period_size, uint32_t *periods);

/**
 * @brief Sets hardware and software parameters of a PCM device.
 * @since_tizen 3.0
 * @param[in] audio_handle The audio hal handle
 * @param[in] pcm_handle The PCM handle
 * @param[in] direction The direction of PCM
 * @param[in] sample_spec The sample specification
 * @param[in] period_size The period size
 * @param[in] periods The periods
 *
 * @return @c 0 on success,
 *         otherwise a negative error value
 * @retval #AUDIO_RET_OK Success
 * @see audio_pcm_set_params()
 */
audio_return_t audio_pcm_set_params(void *audio_handle, void *pcm_handle, uint32_t direction, void *sample_spec, uint32_t period_size, uint32_t periods);

/**
 * @brief Adds the message callback function. (optional)
 * @since_tizen 3.0
 * @param[in] audio_handle The audio hal handle
 * @param[in] message_cb The message callback function
 * @param[in] user_data The user data passed to the callback function
 *
 * @remarks Some audio hal implementation may not have these functions.\n
 * (@c message_cb, @c audio_add_message_cb and @c audio_remove_message_cb)
 *
 * @see message_cb()
 * @see audio_remove_message_cb()
 */
audio_return_t audio_add_message_cb(void *audio_handle, message_cb callback, void *user_data);

/**
 * @brief Removes the message callback function. (optional)
 * @since_tizen 3.0
 * @param[in] audio_handle The audio hal handle
 * @param[in] message_cb The message callback function to be removed
 *
 * @remarks Some audio hal implementation may not have these functions.\n
 * (@c message_cb, @c audio_add_message_cb and @c audio_remove_message_cb)
 *
 * @see message_cb()
 * @see audio_add_message_cb()
 */
audio_return_t audio_remove_message_cb(void *audio_handle, message_cb callback);

/**
* @}
*/

/**
* @}
*/

#endif
