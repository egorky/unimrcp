/*
 * Copyright 2023-2024 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Mandatory rules concerning plugin implementation.
 * 1. Each plugin MUST implement a plugin/engine creator function
 *    with the exact signature and name (the main entry point)
 *        MRCP_PLUGIN_DECLARE(mrcp_engine_t*) mrcp_plugin_create(apr_pool_t *pool)
 * 2. Each plugin MUST declare its version number
 *        MRCP_PLUGIN_VERSION_DECLARE
 * 3. One and only one response MUST be sent back to the received request.
 * 4. Methods (callbacks) of the MRCP engine channel MUST not block.
 *   (asynchronous response can be sent from the context of other thread)
 * 5. Methods (callbacks) of the MPF engine stream MUST not block.
 */

#include <stdlib.h> // For getenv, strtol
#include "mrcp_recog_engine.h"
#include "mrcp_engine_loader.h" // For mrcp_engine_config_get
#include "mpf_activity_detector.h"
#include "apt_consumer_task.h"
#include "apt_log.h"
#include "apt_string_table.h" // For apt_string_table_item_get
#include "apt_pair.h"         // For apt_pair_array_t
#include "mrcp_generic_header.h"
#include "mrcp_message.h" // For mrcp_resource_header_get, etc.

#include "google_stt_streamer.hpp" // C wrapper for Google STT

#define GOOGLE_SRG_ENGINE_TASK_NAME "Google SRG Engine"

/** MRCP log source */
MRCP_PLUGIN_LOG_SOURCE_IMPLEMENT(GOOGLE_SRG_PLUGIN, "GOOGLE-SRG")

/** Declare this macro to set plugin version */
MRCP_PLUGIN_VERSION_DECLARE

typedef struct google_srg_engine_t google_srg_engine_t;
typedef struct google_srg_channel_t google_srg_channel_t;
typedef struct google_srg_msg_t google_srg_msg_t;


/** Google SRG engine */
struct google_srg_engine_t {
    apt_consumer_task_t *task;

    /* Configuration parameters */
    char       *credentials_path;
    char       *default_language_code;
    apt_bool_t  default_interim_results;
    char       *default_model;
    apt_bool_t  enable_automatic_punctuation;
    apt_bool_t  enable_google_vad; 
    apr_pool_t *pool; // Engine's pool for storing config strings
};

/** Google SRG channel */
struct google_srg_channel_t {
    /** Back pointer to engine */
    google_srg_engine_t     *srg_engine;
    /** Engine channel base */
    mrcp_engine_channel_t   *channel;

    /** Active (in-progress) recognition request */
    mrcp_message_t          *recog_request;
    /** Pending stop response */
    mrcp_message_t          *stop_response;
    /** Indicates whether input timers are started */
    apt_bool_t               timers_started;
    /** Voice activity detector (UniMRCP's, might be used for no-input) */
    mpf_activity_detector_t *detector;

    /** Google STT specific fields */
    google_stt_streamer_handle_t stt_streamer;
    apt_bool_t               stream_active;
    char                    *current_language_code;
    int                      current_sample_rate_hz;
    apt_bool_t               current_interim_results;
    char                    *current_model;
    apt_bool_t               current_punctuation;
};

/** Message types for the consumer task */
typedef enum {
    //GOOGLE_SRG_MSG_OPEN_CHANNEL, // Not used as open is synchronous for streamer creation
    GOOGLE_SRG_MSG_CLOSE_CHANNEL,
    GOOGLE_SRG_MSG_REQUEST_PROCESS,
    GOOGLE_SRG_MSG_STT_RESULT,      
    GOOGLE_SRG_MSG_STT_STREAM_ERROR 
} google_srg_msg_type_e;

/** Google SRG task message */
struct google_srg_msg_t {
    google_srg_msg_type_e  type;
    mrcp_engine_channel_t *channel;
    mrcp_message_t        *request; 

    /* Fields for STT results/errors */
    char                  *transcript;
    apt_bool_t             is_final;
    char                  *error_message;
    mrcp_recog_completion_cause_e completion_cause; 
};


/* Engine vtable functions */
static apt_bool_t google_srg_engine_destroy(mrcp_engine_t *engine);
static apt_bool_t google_srg_engine_open(mrcp_engine_t *engine);
static apt_bool_t google_srg_engine_close(mrcp_engine_t *engine);
static mrcp_engine_channel_t* google_srg_engine_channel_create(mrcp_engine_t *engine, apr_pool_t *pool);

static const struct mrcp_engine_method_vtable_t engine_vtable = {
    google_srg_engine_destroy,
    google_srg_engine_open,
    google_srg_engine_close,
    google_srg_engine_channel_create
};

/* Channel vtable functions */
static apt_bool_t google_srg_channel_destroy(mrcp_engine_channel_t *channel);
static apt_bool_t google_srg_channel_open(mrcp_engine_channel_t *channel);
static apt_bool_t google_srg_channel_close(mrcp_engine_channel_t *channel);
static apt_bool_t google_srg_channel_request_process(mrcp_engine_channel_t *channel, mrcp_message_t *request);

static const struct mrcp_engine_channel_method_vtable_t channel_vtable = {
    google_srg_channel_destroy,
    google_srg_channel_open,
    google_srg_channel_close,
    google_srg_channel_request_process
};

/* Stream vtable functions */
static apt_bool_t google_srg_stream_destroy(mpf_audio_stream_t *stream);
static apt_bool_t google_srg_stream_open_rx(mpf_audio_stream_t *stream, mpf_codec_t *codec);
static apt_bool_t google_srg_stream_close_rx(mpf_audio_stream_t *stream);
static apt_bool_t google_srg_stream_write(mpf_audio_stream_t *stream, const mpf_frame_t *frame);

static const mpf_audio_stream_vtable_t audio_stream_vtable = {
    google_srg_stream_destroy,
    NULL, 
    NULL, 
    NULL, 
    google_srg_stream_open_rx,
    google_srg_stream_close_rx,
    google_srg_stream_write,
    NULL  
};

/* Task message processing */
static apt_bool_t google_srg_msg_process(apt_task_t *task, apt_task_msg_t *msg);

/* STT result callback */
static void google_srg_on_stt_result(const char* transcript, bool is_final, const char* error_msg, void* user_data);

/* Helper for signaling task messages */
static apt_bool_t google_srg_msg_signal(google_srg_msg_type_e type, mrcp_engine_channel_t *channel, mrcp_message_t *request);
static apt_bool_t google_srg_msg_signal_stt_result(mrcp_engine_channel_t *channel, const char* transcript, apt_bool_t is_final, const char* error_msg);
static apt_bool_t google_srg_msg_signal_stream_error(mrcp_engine_channel_t* channel, const char* error_msg, mrcp_recog_completion_cause_e cause);


/* MRCP RECOGNIZE request dispatcher */
static apt_bool_t google_srg_channel_request_dispatch(mrcp_engine_channel_t *channel, mrcp_message_t *request);
static apt_bool_t google_srg_channel_recognize(mrcp_engine_channel_t *channel, mrcp_message_t *request, mrcp_message_t *response);
static apt_bool_t google_srg_channel_stop(mrcp_engine_channel_t *channel, mrcp_message_t *request, mrcp_message_t *response);
static apt_bool_t google_srg_channel_define_grammar(mrcp_engine_channel_t *channel, mrcp_message_t *request, mrcp_message_t *response);
static apt_bool_t google_srg_channel_start_input_timers(mrcp_engine_channel_t *channel, mrcp_message_t *request, mrcp_message_t *response);


/* Helper to send RECOGNITION-COMPLETE event */
static apt_bool_t google_srg_recognition_complete(google_srg_channel_t *srg_channel, mrcp_recog_completion_cause_e cause, const char* nlsml_body);
static apt_bool_t google_srg_engine_config_load(google_srg_engine_t *srg_engine, mrcp_engine_config_t *config);


/** Create Google SRG engine */
MRCP_PLUGIN_DECLARE(mrcp_engine_t*) mrcp_plugin_create(apr_pool_t *pool) {
    google_srg_engine_t *srg_engine = (google_srg_engine_t*) apr_palloc(pool, sizeof(google_srg_engine_t));
    apt_task_t *task;
    apt_task_vtable_t *vtable;
    apt_task_msg_pool_t *msg_pool;

    srg_engine->pool = pool; // Store engine pool for config strings
    srg_engine->credentials_path = NULL;
    srg_engine->default_language_code = NULL;
    srg_engine->default_interim_results = FALSE;
    srg_engine->default_model = NULL;
    srg_engine->enable_automatic_punctuation = FALSE;
    srg_engine->enable_google_vad = TRUE; 

    msg_pool = apt_task_msg_pool_create_dynamic(sizeof(google_srg_msg_t), pool);
    srg_engine->task = apt_consumer_task_create(srg_engine, msg_pool, pool);
    if (!srg_engine->task) {
        return NULL;
    }
    task = apt_consumer_task_base_get(srg_engine->task);
    apt_task_name_set(task, GOOGLE_SRG_ENGINE_TASK_NAME);
    vtable = apt_task_vtable_get(task);
    if (vtable) {
        vtable->process_msg = google_srg_msg_process;
    }

    return mrcp_engine_create(
        MRCP_RECOGNIZER_RESOURCE, 
        srg_engine,               
        &engine_vtable,           
        pool);                    
}

/** Load engine configuration */
static apt_bool_t google_srg_engine_config_load(google_srg_engine_t *srg_engine, mrcp_engine_config_t *config) {
    int i;
    const apt_pair_t *pair;
    const char *name;
    const char *value;

    if (!config || !config->params) {
        apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_WARNING, "No configuration parameters found for Google SRG engine.");
        return TRUE; 
    }

    for (i = 0; i < config->params->nelts; i++) {
        pair = &APR_ARRAY_IDX(config->params, i, apt_pair_t);
        name = pair->name.buf;
        value = pair->value.buf;

        apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_DEBUG, "Processing param: %s = %s", name, value);

        if (strcasecmp(name, "google-credentials-path") == 0) {
            srg_engine->credentials_path = apr_pstrdup(srg_engine->pool, value);
        } else if (strcasecmp(name, "google-default-language") == 0) {
            srg_engine->default_language_code = apr_pstrdup(srg_engine->pool, value);
        } else if (strcasecmp(name, "google-default-interim-results") == 0) {
            srg_engine->default_interim_results = (strcasecmp(value, "true") == 0);
        } else if (strcasecmp(name, "google-default-model") == 0) {
            srg_engine->default_model = apr_pstrdup(srg_engine->pool, value);
        } else if (strcasecmp(name, "google-enable-automatic-punctuation") == 0) {
            srg_engine->enable_automatic_punctuation = (strcasecmp(value, "true") == 0);
        } else if (strcasecmp(name, "google-vad-enable") == 0) {
            srg_engine->enable_google_vad = (strcasecmp(value, "true") == 0);
        }
    }

    if (!srg_engine->credentials_path && !getenv("GOOGLE_APPLICATION_CREDENTIALS")) {
        apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_ERROR, "Missing required parameter: google-credentials-path, and GOOGLE_APPLICATION_CREDENTIALS not set.");
        return FALSE;
    }
     if (srg_engine->credentials_path) { 
        apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_INFO, "Set Google Credentials Path: %s", srg_engine->credentials_path);
    } else {
        apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_INFO, "Using Google Default Credentials (GOOGLE_APPLICATION_CREDENTIALS or environment).");
    }

    if (!srg_engine->default_language_code) {
        apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_WARNING, "Missing google-default-language, defaulting to 'en-US'");
        srg_engine->default_language_code = apr_pstrdup(srg_engine->pool, "en-US");
    }
    apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_INFO, "Set Google Default Language: %s", srg_engine->default_language_code);
    apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_INFO, "Set Google Default Interim Results: %s", srg_engine->default_interim_results ? "true" : "false");
    apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_INFO, "Set Google Default Model: %s", srg_engine->default_model ? srg_engine->default_model : "(not set)");
    apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_INFO, "Set Google Enable Automatic Punctuation: %s", srg_engine->enable_automatic_punctuation ? "true" : "false");
    apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_INFO, "Set Google VAD Enable: %s", srg_engine->enable_google_vad ? "true" : "false");

    return TRUE;
}


/** Destroy recognizer engine */
static apt_bool_t google_srg_engine_destroy(mrcp_engine_t *engine) {
    google_srg_engine_t *srg_engine = (google_srg_engine_t*) engine->obj;
    if (srg_engine->task) {
        apt_task_t *task = apt_consumer_task_base_get(srg_engine->task);
        apt_task_destroy(task);
        srg_engine->task = NULL;
    }
    return TRUE;
}

/** Open recognizer engine */
static apt_bool_t google_srg_engine_open(mrcp_engine_t *engine) {
    google_srg_engine_t *srg_engine = (google_srg_engine_t*) engine->obj;
    mrcp_engine_config_t *config = mrcp_engine_config_get(engine);

    if (!google_srg_engine_config_load(srg_engine, config)) {
        apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_ERROR, "Failed to load Google SRG engine configuration.");
        return mrcp_engine_open_respond(engine, FALSE);
    }

    if (srg_engine->task) {
        apt_task_t *task = apt_consumer_task_base_get(srg_engine->task);
        apt_task_start(task);
    }
    return mrcp_engine_open_respond(engine, TRUE);
}

/** Close recognizer engine */
static apt_bool_t google_srg_engine_close(mrcp_engine_t *engine) {
    google_srg_engine_t *srg_engine = (google_srg_engine_t*) engine->obj;
    if (srg_engine->task) {
        apt_task_t *task = apt_consumer_task_base_get(srg_engine->task);
        apt_task_terminate(task, TRUE);
    }
    return mrcp_engine_close_respond(engine);
}

/** Create Google SRG engine channel */
static mrcp_engine_channel_t* google_srg_engine_channel_create(mrcp_engine_t *engine, apr_pool_t *pool) {
    mpf_stream_capabilities_t *capabilities;
    mpf_termination_t *termination;

    google_srg_channel_t *srg_channel = (google_srg_channel_t*) apr_palloc(pool, sizeof(google_srg_channel_t));
    srg_channel->srg_engine = (google_srg_engine_t*) engine->obj;
    srg_channel->recog_request = NULL;
    srg_channel->stop_response = NULL;
    srg_channel->detector = mpf_activity_detector_create(pool); 
    srg_channel->timers_started = FALSE; 

    srg_channel->stt_streamer = NULL;
    srg_channel->stream_active = FALSE;
    srg_channel->current_language_code = NULL;
    srg_channel->current_sample_rate_hz = 0;
    srg_channel->current_interim_results = FALSE;
    srg_channel->current_model = NULL;
    srg_channel->current_punctuation = FALSE;

    capabilities = mpf_sink_stream_capabilities_create(pool);
    mpf_codec_capabilities_add(
        &capabilities->codecs,
        MPF_SAMPLE_RATE_8000 | MPF_SAMPLE_RATE_16000, 
        "LPCM");

    termination = mrcp_engine_audio_termination_create(
        srg_channel,          
        &audio_stream_vtable, 
        capabilities,         
        pool);                

    srg_channel->channel = mrcp_engine_channel_create(
        engine,               
        &channel_vtable,      
        srg_channel,          
        termination,          
        pool);                

    return srg_channel->channel;
}

/** Destroy engine channel */
static apt_bool_t google_srg_channel_destroy(mrcp_engine_channel_t *channel) {
    google_srg_channel_t *srg_channel = (google_srg_channel_t*) channel->method_obj;
    if (srg_channel->stt_streamer) {
        if (srg_channel->stream_active) {
            google_stt_streamer_close(srg_channel->stt_streamer);
        }
        google_stt_streamer_destroy(srg_channel->stt_streamer);
        srg_channel->stt_streamer = NULL;
    }
    apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_DEBUG, "Google SRG Channel Destroyed " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(channel->session->last_request));
    return TRUE;
}

/** Open engine channel */
static apt_bool_t google_srg_channel_open(mrcp_engine_channel_t *channel) {
    google_srg_channel_t *srg_channel = (google_srg_channel_t*) channel->method_obj;
    google_srg_engine_t *srg_engine = srg_channel->srg_engine;

    srg_channel->stt_streamer = google_stt_streamer_create(
        srg_engine->credentials_path,
        google_srg_on_stt_result,
        channel 
    );

    if (!srg_channel->stt_streamer) {
        apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_WARNING, "Failed to create Google STT streamer for channel " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(channel->session->last_request));
        return mrcp_engine_channel_open_respond(channel, FALSE);
    }
    apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_INFO, "Google STT streamer created for channel " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(channel->session->last_request));
    return mrcp_engine_channel_open_respond(channel, TRUE);
}

/** Close engine channel */
static apt_bool_t google_srg_channel_close(mrcp_engine_channel_t *channel) {
    google_srg_channel_t *srg_channel = (google_srg_channel_t*) channel->method_obj;
    apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_INFO, "Close Google SRG Channel " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(channel->session->last_request));

    if (srg_channel->stt_streamer) {
        if(srg_channel->stream_active) { 
            apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_WARNING, "Closing STT streamer due to channel close during active stream " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(channel->session->last_request));
            google_stt_streamer_close(srg_channel->stt_streamer); 
            srg_channel->stream_active = FALSE;
        }
    }
    return google_srg_msg_signal(GOOGLE_SRG_MSG_CLOSE_CHANNEL, channel, NULL);
}

/** Process MRCP channel request */
static apt_bool_t google_srg_channel_request_process(mrcp_engine_channel_t *channel, mrcp_message_t *request) {
    return google_srg_msg_signal(GOOGLE_SRG_MSG_REQUEST_PROCESS, channel, request);
}


/** Audio stream vtable functions */
static apt_bool_t google_srg_stream_destroy(mpf_audio_stream_t *stream) {
    return TRUE;
}

static apt_bool_t google_srg_stream_open_rx(mpf_audio_stream_t *stream, mpf_codec_t *codec) {
    google_srg_channel_t *srg_channel = (google_srg_channel_t*) stream->obj;
    if(srg_channel && srg_channel->recog_request) {
         apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_INFO, "Audio Stream Opened (Rx) " APT_SIDRES_FMT " Codec: %s @ %u Hz",
            MRCP_MESSAGE_SIDRES(srg_channel->recog_request),
            codec->name.buf,
            codec->sampling_rate);
    } else {
        apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_INFO, "Audio Stream Opened (Rx) - No active request");
    }
    return TRUE;
}

static apt_bool_t google_srg_stream_close_rx(mpf_audio_stream_t *stream) {
    google_srg_channel_t *srg_channel = (google_srg_channel_t*) stream->obj;
     if(srg_channel && srg_channel->recog_request) {
        apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_INFO, "Audio Stream Closed (Rx) " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(srg_channel->recog_request));
    } else {
        apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_INFO, "Audio Stream Closed (Rx) - No active request");
    }
    if (srg_channel && srg_channel->stream_active) {
        apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_WARNING, "Audio stream closed unexpectedly during active STT stream " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(srg_channel->recog_request));
        google_srg_msg_signal_stream_error(srg_channel->channel, "Audio stream closed unexpectedly", RECOGNIZER_COMPLETION_CAUSE_ERROR); 
        srg_channel->stream_active = FALSE; 
        if (srg_channel->stt_streamer) {
             google_stt_streamer_close(srg_channel->stt_streamer); 
        }
    }
    return TRUE;
}

static apt_bool_t google_srg_stream_write(mpf_audio_stream_t *stream, const mpf_frame_t *frame) {
    google_srg_channel_t *srg_channel = (google_srg_channel_t*) stream->obj;

    if (srg_channel->stop_response) { 
        mrcp_engine_channel_message_send(srg_channel->channel, srg_channel->stop_response);
        srg_channel->stop_response = NULL;
        return TRUE;
    }

    if (!srg_channel->recog_request || !srg_channel->stream_active || !srg_channel->stt_streamer) {
        return TRUE; 
    }

    if ((frame->type & MEDIA_FRAME_TYPE_AUDIO)) {
        if (frame->codec_frame.size > 0) {
            int rv = google_stt_streamer_send_audio(
                srg_channel->stt_streamer,
                frame->codec_frame.buffer,
                frame->codec_frame.size
            );
            if (rv != 0) {
                apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_WARNING, "Failed to send audio to Google STT " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(srg_channel->recog_request));
                google_srg_msg_signal_stream_error(srg_channel->channel, "Failed to send audio to STT service", RECOGNIZER_COMPLETION_CAUSE_ERROR);
                srg_channel->stream_active = FALSE; 
                 if (srg_channel->stt_streamer) { 
                    google_stt_streamer_close(srg_channel->stt_streamer);
                }
            }
        }
    } else if ((frame->type & MEDIA_FRAME_TYPE_EVENT)) {
        if (frame->marker == MPF_MARKER_TIMER) {
            if (srg_channel->timers_started && !srg_channel->srg_engine->enable_google_vad && srg_channel->stream_active) {
                // This condition implies we are using UniMRCP's VAD for No-Input detection
                mpf_detector_event_e det_event = mpf_activity_detector_event_get(srg_channel->detector);
                if (det_event == MPF_DETECTOR_EVENT_NOINPUT) {
                    apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_INFO, "No-Input Timeout detected by MPF VAD " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(srg_channel->recog_request));
                    // Signal this to the main task to send RECOGNITION_COMPLETE with NO_INPUT_TIMEOUT
                    google_srg_msg_signal_stream_error(srg_channel->channel, "No input timeout", RECOGNIZER_COMPLETION_CAUSE_NO_INPUT_TIMEOUT);
                    srg_channel->stream_active = FALSE;
                    if (srg_channel->stt_streamer) {
                        google_stt_streamer_finish(srg_channel->stt_streamer); 
                    }
                }
            }
        }
    }
    return TRUE;
}

/** STT result callback (called from gRPC thread) */
static void google_srg_on_stt_result(const char* transcript, bool is_final, const char* error_msg, void* user_data) {
    mrcp_engine_channel_t *channel = (mrcp_engine_channel_t*) user_data;
    google_srg_channel_t *srg_channel = (google_srg_channel_t*) channel->method_obj;

    if (!srg_channel->recog_request && !error_msg) { 
        apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_DEBUG, "STT result received for already completed/stopped request. Ignoring. Final: %d", is_final);
        return;
    }
    
    apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_DEBUG, "STT Result Received (is_final: %d): '%s' (Error: '%s') " APT_SIDRES_FMT,
        is_final, transcript ? transcript : "N/A", error_msg ? error_msg : "N/A",
        srg_channel->recog_request ? MRCP_MESSAGE_SIDRES(srg_channel->recog_request) : "N/A");

    google_srg_msg_signal_stt_result(channel, transcript, is_final, error_msg);
}


/** Process RECOGNIZE request */
static apt_bool_t google_srg_channel_recognize(mrcp_engine_channel_t *channel, mrcp_message_t *request, mrcp_message_t *response) {
    google_srg_channel_t *srg_channel = (google_srg_channel_t*) channel->method_obj;
    google_srg_engine_t *srg_engine = srg_channel->srg_engine;
    mrcp_recog_header_t *recog_header;
    const mpf_codec_descriptor_t *descriptor;

    if (srg_channel->stream_active || !srg_channel->stt_streamer) {
        apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_WARNING, "Recognize called on active stream or null streamer " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(request));
        response->start_line.status_code = MRCP_STATUS_CODE_METHOD_FAILED;
        return FALSE; 
    }

    descriptor = mrcp_engine_sink_stream_codec_get(channel);
    if (!descriptor) {
        apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_WARNING, "Failed to Get Codec Descriptor " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(request));
        response->start_line.status_code = MRCP_STATUS_CODE_METHOD_FAILED;
        return FALSE;
    }
    srg_channel->current_sample_rate_hz = descriptor->sampling_rate;

    recog_header = (mrcp_recog_header_t*) mrcp_resource_header_get(request);
    if (recog_header) {
        if (mrcp_resource_header_property_check(request, RECOGNIZER_HEADER_START_INPUT_TIMERS) == TRUE) {
            srg_channel->timers_started = recog_header->start_input_timers;
        }
        if (mrcp_resource_header_property_check(request, RECOGNIZER_HEADER_NO_INPUT_TIMEOUT) == TRUE && srg_channel->detector) {
            mpf_activity_detector_noinput_timeout_set(srg_channel->detector, recog_header->no_input_timeout);
        }
    } else { // If no recog_header, ensure timers_started default is respected (usually FALSE from channel create)
        srg_channel->timers_started = FALSE; // Explicitly set if no header, or based on MRCP default for the resource
    }


    const apt_str_t* speech_lang_header = mrcp_generic_header_property_get(request, GENERIC_HEADER_SPEECH_LANGUAGE);
    if (speech_lang_header && speech_lang_header->length > 0) {
        srg_channel->current_language_code = apr_pstrdup(request->pool, speech_lang_header->buf);
    } else {
        srg_channel->current_language_code = apr_pstrdup(request->pool, srg_engine->default_language_code);
    }

    const apt_str_t* interim_header = apt_string_table_item_get(request->header.vendor_specific_data, "X-Google-Interim-Results");
    if (interim_header && interim_header->length > 0) {
        srg_channel->current_interim_results = (strcasecmp(interim_header->buf, "true") == 0);
    } else {
        srg_channel->current_interim_results = srg_engine->default_interim_results;
    }
    
    const apt_str_t* model_header = apt_string_table_item_get(request->header.vendor_specific_data, "X-Google-Model");
     if (model_header && model_header->length > 0) {
        srg_channel->current_model = apr_pstrdup(request->pool, model_header->buf); 
    } else {
        srg_channel->current_model = srg_engine->default_model ? apr_pstrdup(request->pool, srg_engine->default_model) : NULL;
    }
    srg_channel->current_punctuation = srg_engine->enable_automatic_punctuation;


    apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_INFO, "Starting STT Stream: Lang=%s, Rate=%dHz, Interim=%d, Model=%s, Punct=%d " APT_SIDRES_FMT,
        srg_channel->current_language_code, srg_channel->current_sample_rate_hz, srg_channel->current_interim_results,
        srg_channel->current_model ? srg_channel->current_model : "(default)", srg_channel->current_punctuation,
        MRCP_MESSAGE_SIDRES(request));

    // The C++ wrapper needs to be extended to accept model and punctuation if they are dynamic
    // For now, StartStream only takes lang, rate, interim. Model/punctuation are defaults in C++ side.
    int rv = google_stt_streamer_start(
        srg_channel->stt_streamer,
        srg_channel->current_language_code,
        srg_channel->current_sample_rate_hz,
        srg_channel->current_interim_results
    );

    if (rv != 0) {
        apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_WARNING, "Failed to start Google STT stream " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(request));
        response->start_line.status_code = MRCP_STATUS_CODE_METHOD_FAILED;
        return FALSE; 
    }
    srg_channel->stream_active = TRUE;
    srg_channel->recog_request = request; 

    response->start_line.request_state = MRCP_REQUEST_STATE_INPROGRESS;
    mrcp_engine_channel_message_send(channel, response);
    return TRUE; 
}

/** Process STOP request */
static apt_bool_t google_srg_channel_stop(mrcp_engine_channel_t *channel, mrcp_message_t *request, mrcp_message_t *response) {
    google_srg_channel_t *srg_channel = (google_srg_channel_t*) channel->method_obj;
    apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_INFO, "Processing STOP request " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(request));

    if (!srg_channel->recog_request || !srg_channel->stream_active || !srg_channel->stt_streamer) {
        apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_WARNING, "STOP called on inactive stream, null streamer, or no active recog " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(request));
        if (!srg_channel->recog_request) { // No active RECOGNIZE
             response->start_line.request_state = MRCP_REQUEST_STATE_COMPLETE; 
             response->start_line.status_code = MRCP_STATUS_CODE_METHOD_FAILED; // Or SUCCESS if no active request is not an error
             mrcp_engine_channel_message_send(channel, response);
        } else { // RECOGNIZE was active but stream is not - means an error already occurred
            // The RECOGNITION_COMPLETE for the error should have been sent.
            // Send the STOP response as complete.
            response->start_line.request_state = MRCP_REQUEST_STATE_COMPLETE;
            response->start_line.status_code = MRCP_STATUS_CODE_SUCCESS; // STOP itself is successful
            mrcp_engine_channel_message_send(channel, response);
        }
        return TRUE; 
    }
    
    srg_channel->stop_response = response; 

    int rv = google_stt_streamer_finish(srg_channel->stt_streamer); 
    if (rv != 0) {
        apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_WARNING, "Failed to finish Google STT stream (WritesDone) on STOP " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(request));
        if (srg_channel->stop_response) { // Send STOP response even if WritesDone fails
            mrcp_engine_channel_message_send(channel, srg_channel->stop_response);
            srg_channel->stop_response = NULL;
        }
        google_srg_recognition_complete(srg_channel, RECOGNIZER_COMPLETION_CAUSE_ERROR, NULL);
        srg_channel->stream_active = FALSE; 
         if (srg_channel->stt_streamer) {
            google_stt_streamer_close(srg_channel->stt_streamer); 
        }
    } else {
        apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_DEBUG, "Google STT stream finish (WritesDone) called successfully on STOP " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(request));
    }
    return TRUE; 
}

/** Process DEFINE_GRAMMAR (rudimentary) */
static apt_bool_t google_srg_channel_define_grammar(mrcp_engine_channel_t *channel, mrcp_message_t *request, mrcp_message_t *response) {
    apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_INFO, "DEFINE_GRAMMAR received " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(request));
    response->start_line.request_state = MRCP_REQUEST_STATE_COMPLETE;
    response->start_line.status_code = MRCP_STATUS_CODE_SUCCESS;
    // UniMRCP requires Completion-Cause for COMPLETE responses
    mrcp_generic_header_t *req_generic_header = mrcp_generic_header_get(response);
    if(req_generic_header) {
        apt_string_assign(&req_generic_header->completion_cause,"000",response->pool); // "000" for success
        mrcp_generic_header_property_add(response,GENERIC_HEADER_COMPLETION_CAUSE);
    }
    return mrcp_engine_channel_message_send(channel, response);
}

/** Process START-INPUT-TIMERS request */
static apt_bool_t google_srg_channel_start_input_timers(mrcp_engine_channel_t *channel, mrcp_message_t *request, mrcp_message_t *response) {
    google_srg_channel_t *srg_channel = (google_srg_channel_t*)channel->method_obj;
    srg_channel->timers_started = TRUE;
    apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_INFO, "Input Timers Started " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(request));
    response->start_line.request_state = MRCP_REQUEST_STATE_COMPLETE; 
    response->start_line.status_code = MRCP_STATUS_CODE_SUCCESS;
    return mrcp_engine_channel_message_send(channel,response);
}


/** Dispatch MRCP request */
static apt_bool_t google_srg_channel_request_dispatch(mrcp_engine_channel_t *channel, mrcp_message_t *request) {
    apt_bool_t processed_internally = TRUE; 
    mrcp_message_t *response = mrcp_response_create(request, request->pool);

    switch (request->start_line.method_id) {
        case RECOGNIZER_SET_PARAMS:
            apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_INFO, "SET-PARAMS received " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(request));
            // For now, always succeed. Implement parameter setting if needed.
            response->start_line.status_code = MRCP_STATUS_CODE_SUCCESS;
            break;
        case RECOGNIZER_GET_PARAMS:
             apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_INFO, "GET-PARAMS received " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(request));
            // For now, always succeed. Implement parameter getting if needed.
            response->start_line.status_code = MRCP_STATUS_CODE_SUCCESS;
            break;
        case RECOGNIZER_DEFINE_GRAMMAR:
            return google_srg_channel_define_grammar(channel, request, response); 
        case RECOGNIZER_RECOGNIZE:
            return google_srg_channel_recognize(channel, request, response); 
        case RECOGNIZER_GET_RESULT:
            apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_INFO, "GET-RESULT received " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(request));
            response->start_line.status_code = MRCP_STATUS_CODE_METHOD_NOT_SUPPORTED; // Or whatever is appropriate
            break;
        case RECOGNIZER_START_INPUT_TIMERS:
            return google_srg_channel_start_input_timers(channel, request, response);
        case RECOGNIZER_STOP:
            return google_srg_channel_stop(channel, request, response); 
        default:
            apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_WARNING, "Unhandled MRCP method ID: %d " APT_SIDRES_FMT, request->start_line.method_id, MRCP_MESSAGE_SIDRES(request));
            response->start_line.status_code = MRCP_STATUS_CODE_METHOD_NOT_SUPPORTED;
            processed_internally = FALSE; // Fall through to default send
            break;
    }

    if (!processed_internally) { // If not handled by specific functions that send their own response
        mrcp_engine_channel_message_send(channel, response);
    }
    return TRUE;
}

/** Helper to send RECOGNITION-COMPLETE event */
static apt_bool_t google_srg_recognition_complete(google_srg_channel_t *srg_channel, mrcp_recog_completion_cause_e cause, const char* nlsml_body) {
    mrcp_message_t *message;
    mrcp_recog_header_t *recog_header;

    if (!srg_channel->recog_request) {
        apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_WARNING, "Recognition complete called with no active recog_request. Cause: %03d", cause);
        // If there's a pending STOP response, it might mean the STOP was processed after an error already cleared recog_request
        if (srg_channel->stop_response && cause != RECOGNIZER_COMPLETION_CAUSE_SUCCESS) { // Avoid sending STOP response if it was already a success
             apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_INFO, "Sending pending STOP response due to error completion " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(srg_channel->channel->session->last_request));
             mrcp_engine_channel_message_send(srg_channel->channel, srg_channel->stop_response);
             srg_channel->stop_response = NULL;
        }
        return FALSE;
    }

    message = mrcp_event_create(
        srg_channel->recog_request,
        RECOGNIZER_RECOGNITION_COMPLETE,
        srg_channel->recog_request->pool);
    if (!message) {
        return FALSE;
    }

    message->start_line.request_state = MRCP_REQUEST_STATE_COMPLETE;
    recog_header = (mrcp_recog_header_t*) mrcp_resource_header_prepare(message);
    if (recog_header) {
        recog_header->completion_cause = cause;
        mrcp_resource_header_property_add(message, RECOGNIZER_HEADER_COMPLETION_CAUSE);
    }

    if (nlsml_body && cause == RECOGNIZER_COMPLETION_CAUSE_SUCCESS) {
        mrcp_generic_header_t *generic_header = mrcp_generic_header_prepare(message);
        if (generic_header) {
            apt_string_assign(&generic_header->content_type, "application/nlsml+xml", message->pool);
            mrcp_generic_header_property_add(message, GENERIC_HEADER_CONTENT_TYPE);
        }
        apt_string_assign_n(&message->body, nlsml_body, strlen(nlsml_body), message->pool);
    }
    
    apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_INFO, "Sending RECOGNITION-COMPLETE, Cause: %03d " APT_SIDRES_FMT, cause, MRCP_MESSAGE_SIDRES(srg_channel->recog_request));

    // Clear active request first before sending, to prevent race conditions if message_send is somehow reentrant for this channel
    mrcp_message_t* temp_recog_request = srg_channel->recog_request;
    srg_channel->recog_request = NULL; 
    srg_channel->stream_active = FALSE; 
    
    mrcp_engine_channel_message_send(srg_channel->channel, message);

    // If a STOP request was pending and we are now sending RECOGNITION_COMPLETE,
    // we also need to send the response to the STOP message itself.
    if (srg_channel->stop_response) {
        apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_INFO, "Sending pending STOP response " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(temp_recog_request));
        srg_channel->stop_response->start_line.request_state = MRCP_REQUEST_STATE_COMPLETE; // Ensure it's marked complete
        mrcp_engine_channel_message_send(srg_channel->channel, srg_channel->stop_response);
        srg_channel->stop_response = NULL;
    }
    
    return TRUE;
}


/** Task message signaling functions */
static apt_bool_t google_srg_msg_signal(google_srg_msg_type_e type, mrcp_engine_channel_t *channel, mrcp_message_t *request) {
    apt_bool_t status = FALSE;
    google_srg_channel_t *srg_channel = (google_srg_channel_t*) channel->method_obj;
    google_srg_engine_t *srg_engine = srg_channel->srg_engine;
    apt_task_t *task = apt_consumer_task_base_get(srg_engine->task);
    apt_task_msg_t *msg = apt_task_msg_get(task);

    if (msg) {
        google_srg_msg_t *srg_msg;
        msg->type = TASK_MSG_USER; // All our custom messages are TASK_MSG_USER
        srg_msg = (google_srg_msg_t*) msg->data;

        srg_msg->type = type; // This is our internal subtype
        srg_msg->channel = channel;
        srg_msg->request = request; 
        srg_msg->transcript = NULL;
        srg_msg->is_final = FALSE;
        srg_msg->error_message = NULL;
        srg_msg->completion_cause = RECOGNIZER_COMPLETION_CAUSE_SUCCESS; 
        status = apt_task_msg_signal(task, msg);
    } else {
        apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_WARNING, "Failed to get message from task pool for type %d", type);
    }
    return status;
}

static apt_bool_t google_srg_msg_signal_stt_result(mrcp_engine_channel_t *channel, const char* transcript, apt_bool_t is_final, const char* error_msg) {
    apt_bool_t status = FALSE;
    google_srg_channel_t *srg_channel = (google_srg_channel_t*) channel->method_obj;
    google_srg_engine_t *srg_engine = srg_channel->srg_engine;
    apt_task_t *task = apt_consumer_task_base_get(srg_engine->task);
    apt_task_msg_t *msg = apt_task_msg_get(task);

    if (msg) {
        google_srg_msg_t *srg_msg;
        msg->type = TASK_MSG_USER;
        srg_msg = (google_srg_msg_t*) msg->data;

        srg_msg->type = GOOGLE_SRG_MSG_STT_RESULT;
        srg_msg->channel = channel;
        srg_msg->request = NULL; 
        srg_msg->transcript = transcript ? apr_pstrdup(apt_task_msg_pool_get(msg), transcript) : NULL;
        srg_msg->is_final = is_final;
        srg_msg->error_message = error_msg ? apr_pstrdup(apt_task_msg_pool_get(msg), error_msg) : NULL;
        srg_msg->completion_cause = RECOGNIZER_COMPLETION_CAUSE_SUCCESS; 

        status = apt_task_msg_signal(task, msg);
    } else {
         apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_WARNING, "Failed to get message from task pool for STT result");
    }
    return status;
}

static apt_bool_t google_srg_msg_signal_stream_error(mrcp_engine_channel_t* channel, const char* error_msg, mrcp_recog_completion_cause_e cause) {
    apt_bool_t status = FALSE;
    google_srg_channel_t *srg_channel = (google_srg_channel_t*) channel->method_obj;
    google_srg_engine_t *srg_engine = srg_channel->srg_engine;
    apt_task_t *task = apt_consumer_task_base_get(srg_engine->task);
    apt_task_msg_t *msg = apt_task_msg_get(task);

    if (msg) {
        google_srg_msg_t *srg_msg;
        msg->type = TASK_MSG_USER;
        srg_msg = (google_srg_msg_t*) msg->data;

        srg_msg->type = GOOGLE_SRG_MSG_STT_STREAM_ERROR;
        srg_msg->channel = channel;
        srg_msg->request = NULL;
        srg_msg->transcript = NULL;
        srg_msg->is_final = TRUE; 
        srg_msg->error_message = error_msg ? apr_pstrdup(apt_task_msg_pool_get(msg), error_msg) : NULL;
        srg_msg->completion_cause = cause;

        status = apt_task_msg_signal(task, msg);
    } else {
        apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_WARNING, "Failed to get message from task pool for stream error");
    }
    return status;
}


/** Process task messages */
static apt_bool_t google_srg_msg_process(apt_task_t *task, apt_task_msg_t *msg) {
    google_srg_msg_t *srg_msg = (google_srg_msg_t*) msg->data;
    google_srg_channel_t *srg_channel = NULL;
    
    if(srg_msg->channel) { 
         srg_channel = (google_srg_channel_t*) srg_msg->channel->method_obj;
    } else if (msg->type == TASK_MSG_USER) { // Only log if it's a user message that unexpectedly lacks a channel
        apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_WARNING, "SRG user message received with no channel context (type: %d)", srg_msg->type);
        return TRUE; // Cannot process further
    }


    switch (msg->type) {
        case TASK_MSG_USER: 
            if (srg_channel == NULL) { 
                apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_WARNING, "SRG user message (type %d) received with null srg_channel", srg_msg->type);
                break;
            }
            switch (srg_msg->type) { 
                case GOOGLE_SRG_MSG_CLOSE_CHANNEL:
                    mrcp_engine_channel_close_respond(srg_msg->channel);
                    break;
                case GOOGLE_SRG_MSG_REQUEST_PROCESS:
                    google_srg_channel_request_dispatch(srg_msg->channel, srg_msg->request);
                    break;
                case GOOGLE_SRG_MSG_STT_RESULT:
                    if (!srg_channel->recog_request && !srg_msg->error_message) { 
                        apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_DEBUG, "STT Result processed for already completed/stopped request. Ignoring. Final: %d", srg_msg->is_final);
                        break;
                    }

                    if (srg_msg->error_message) {
                        apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_WARNING, "STT Error: %s " APT_SIDRES_FMT, srg_msg->error_message, 
                            srg_channel->recog_request ? MRCP_MESSAGE_SIDRES(srg_channel->recog_request) : "N/A");
                        google_srg_recognition_complete(srg_channel, RECOGNIZER_COMPLETION_CAUSE_CONNECTOR_ERROR, NULL); 
                        if (srg_channel->stt_streamer) { 
                            google_stt_streamer_close(srg_channel->stt_streamer);
                        }
                    } else if (srg_msg->is_final) {
                        char* nlsml_body = apr_psprintf(srg_channel->channel->pool, // Use channel pool for NLSML body
                            "<?xml version=\"1.0\"?>\n"
                            "<result>\n"
                            "  <interpretation grammar=\"%s\" confidence=\"0.90\">\n" 
                            "    <instance>%s</instance>\n"
                            "    <input mode=\"speech\">%s</input>\n"
                            "  </interpretation>\n"
                            "</result>",
                            srg_channel->recog_request ? srg_channel->recog_request->channel_id.session_id.buf : "unknown_session", 
                            srg_msg->transcript ? srg_msg->transcript : "",
                            srg_msg->transcript ? srg_msg->transcript : ""
                        );
                        apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_INFO, "Final Result: %s " APT_SIDRES_FMT, srg_msg->transcript ? srg_msg->transcript : "",
                             srg_channel->recog_request ? MRCP_MESSAGE_SIDRES(srg_channel->recog_request) : "N/A");
                        google_srg_recognition_complete(srg_channel, RECOGNIZER_COMPLETION_CAUSE_SUCCESS, nlsml_body);
                         if (srg_channel->stt_streamer) { 
                            google_stt_streamer_close(srg_channel->stt_streamer);
                        }
                    } else { 
                        apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_INFO, "Interim Result: %s " APT_SIDRES_FMT, srg_msg->transcript ? srg_msg->transcript : "", 
                            srg_channel->recog_request ? MRCP_MESSAGE_SIDRES(srg_channel->recog_request) : "N/A");
                        // TODO: Optionally construct and send RECOGNIZER_INTERMEDIATE_RESULT event
                    }
                    break;
                case GOOGLE_SRG_MSG_STT_STREAM_ERROR:
                     if (!srg_channel->recog_request) break; 
                     apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_WARNING, "STT Stream Error: %s, Cause: %03d " APT_SIDRES_FMT,
                        srg_msg->error_message ? srg_msg->error_message : "Unknown stream error",
                        srg_msg->completion_cause,
                        MRCP_MESSAGE_SIDRES(srg_channel->recog_request));
                    google_srg_recognition_complete(srg_channel, srg_msg->completion_cause, NULL);
                    break;
                default:
                    apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_WARNING, "Unhandled SRG message type: %d", srg_msg->type);
                    break;
            }
            break;
        case TASK_MSG_TERMINATE:
            apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_INFO, "Google SRG Task Terminated");
            break;
        case TASK_MSG_BREAK:
            apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_INFO, "Google SRG Task Break");
            break;
        default:
            apt_log(GOOGLE_SRG_LOG_MARK, APT_PRIO_WARNING, "Unknown message type for SRG task: %d", msg->type);
            break;
    }
    return TRUE;
}
