/*

  Copyright (C) 2017 Gonzalo José Carracedo Carballal

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation, version 3.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this program.  If not, see
  <http://www.gnu.org/licenses/>

*/

#ifndef _MSG_H
#define _MSG_H

#include <sigutils/util/util.h>
#include <stdint.h>

#include "analyzer.h"
#include "serialize.h"
#include <sgdp4/sgdp4-types.h>
#include "correctors/tle.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define SUSCAN_ANALYZER_MESSAGE_TYPE_SOURCE_INFO   0x0
#define SUSCAN_ANALYZER_MESSAGE_TYPE_SOURCE_INIT   0x1
#define SUSCAN_ANALYZER_MESSAGE_TYPE_CHANNEL       0x2
#define SUSCAN_ANALYZER_MESSAGE_TYPE_EOS           0x3
#define SUSCAN_ANALYZER_MESSAGE_TYPE_READ_ERROR    0x4
#define SUSCAN_ANALYZER_MESSAGE_TYPE_INTERNAL      0x5
#define SUSCAN_ANALYZER_MESSAGE_TYPE_SAMPLES_LOST  0x6
#define SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR     0x7 /* Channel inspector */
#define SUSCAN_ANALYZER_MESSAGE_TYPE_PSD           0x8 /* Main spectrum */
#define SUSCAN_ANALYZER_MESSAGE_TYPE_SAMPLES       0x9 /* Sample batch */
#define SUSCAN_ANALYZER_MESSAGE_TYPE_THROTTLE      0xa /* Set throttle */
#define SUSCAN_ANALYZER_MESSAGE_TYPE_PARAMS        0xb /* Analyzer params */
#define SUSCAN_ANALYZER_MESSAGE_TYPE_GET_PARAMS    0xc
#define SUSCAN_ANALYZER_MESSAGE_TYPE_SEEK          0xd
#define SUSCAN_ANALYZER_MESSAGE_TYPE_HISTORY_SIZE  0xe
#define SUSCAN_ANALYZER_MESSAGE_TYPE_REPLAY        0xf

/* Invalid message. No one should even send this. */
#define SUSCAN_ANALYZER_MESSAGE_TYPE_INVALID       0x8000000

#define SUSCAN_ANALYZER_INIT_SUCCESS               0
#define SUSCAN_ANALYZER_INIT_PROGRESS              1
#define SUSCAN_ANALYZER_INIT_FAILURE              -1

/* 
  Discardable messages that arrive later than this
  should be considered as expired and therefore should
  be discarded 
 */

#define SUSCAN_ANALYZER_EXPIRE_DELTA_MS            50

/* Generic status message */
SUSCAN_SERIALIZABLE(suscan_analyzer_status_msg) {
  int32_t code;
  union {
    char *err_msg;
    char *message;
  };
  const suscan_analyzer_t *sender;
};

/* Channel notification message */
struct suscan_analyzer_channel_msg {
  const suscan_source_t *source;
  PTR_LIST(struct sigutils_channel, channel);
  const suscan_analyzer_t *sender;
};

/* Throttle parameters */
SUSCAN_SERIALIZABLE(suscan_analyzer_throttle_msg) {
  SUSCOUNT samp_rate; /* Samp rate == 0: reset */
};

/* Throttle parameters */
SUSCAN_SERIALIZABLE(suscan_analyzer_seek_msg) {
  struct timeval position;
};

/* History size */
SUSCAN_SERIALIZABLE(suscan_analyzer_history_size_msg) {
  SUSCOUNT buffer_length; /* In bytes */
};

/* Replay enabled */
SUSCAN_SERIALIZABLE(suscan_analyzer_replay_msg) {
  SUBOOL replay;
};


/* Channel spectrum message */
SUSCAN_SERIALIZABLE(suscan_analyzer_psd_msg) {
  int64_t fc;
  uint32_t inspector_id;
  struct   timeval timestamp; /* Timestamp after PSD */
  struct   timeval rt_time;   /* Real time timestamp */
  SUBOOL   looped;
  SUSCOUNT history_size;
  SUFLOAT  samp_rate;
  SUFLOAT  measured_samp_rate;
  SUFLOAT  N0;
  SUSCOUNT psd_size;
  SUFLOAT *psd_data;
};

/* These messages allow partial deserialization */
SUSCAN_PARTIAL_DESERIALIZER_PROTO(suscan_analyzer_psd_msg);

/* Channel sample batch */
SUSCAN_SERIALIZABLE(suscan_analyzer_sample_batch_msg) {
  uint32_t   inspector_id;
  SUCOMPLEX *samples;
  SUSCOUNT   sample_count;
};

/*
 * Channel inspector command. This is request-response: sample
 * updates are treated separately
 */
enum suscan_analyzer_inspector_msgkind {
  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_NOOP,
  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_OPEN,
  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_ID,
  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_GET_CONFIG,
  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_CONFIG,
  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_ESTIMATOR,
  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SPECTRUM,
  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_RESET_EQUALIZER,
  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_CLOSE,
  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_FREQ,
  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_BANDWIDTH,
  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_WATERMARK,
  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE,
  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_OBJECT,
  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_INVALID_ARGUMENT,
  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_KIND,
  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_INVALID_CHANNEL,
  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_TLE,
  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_ORBIT_REPORT,
  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_INVALID_CORRECTION,
  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SIGNAL,
  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_COUNT
};

SUINLINE const char *
suscan_analyzer_inspector_msgkind_to_string(
    enum suscan_analyzer_inspector_msgkind kind)
{
#define SUSCAN_COMP_MSGKIND(kind) \
  case JOIN(SUSCAN_ANALYZER_INSPECTOR_MSGKIND_, kind): \
    return STRINGIFY(kind)

  switch (kind) {
    SUSCAN_COMP_MSGKIND(OPEN);
    SUSCAN_COMP_MSGKIND(SET_ID);
    SUSCAN_COMP_MSGKIND(GET_CONFIG);
    SUSCAN_COMP_MSGKIND(SET_CONFIG);
    SUSCAN_COMP_MSGKIND(ESTIMATOR);
    SUSCAN_COMP_MSGKIND(SPECTRUM);
    SUSCAN_COMP_MSGKIND(RESET_EQUALIZER);
    SUSCAN_COMP_MSGKIND(CLOSE);
    SUSCAN_COMP_MSGKIND(SET_FREQ);
    SUSCAN_COMP_MSGKIND(SET_BANDWIDTH);
    SUSCAN_COMP_MSGKIND(SET_WATERMARK);
    SUSCAN_COMP_MSGKIND(WRONG_HANDLE);
    SUSCAN_COMP_MSGKIND(WRONG_OBJECT);
    SUSCAN_COMP_MSGKIND(INVALID_ARGUMENT);
    SUSCAN_COMP_MSGKIND(WRONG_KIND);
    SUSCAN_COMP_MSGKIND(INVALID_CHANNEL);
    SUSCAN_COMP_MSGKIND(SET_TLE);
    SUSCAN_COMP_MSGKIND(ORBIT_REPORT);
    SUSCAN_COMP_MSGKIND(INVALID_CORRECTION);
    SUSCAN_COMP_MSGKIND(SIGNAL);

    default:
      return "UNKNOWN";
  }
}

SUSCAN_SERIALIZABLE(suscan_analyzer_inspector_msg) {
  union {
    enum suscan_analyzer_inspector_msgkind kind;
    uint32_t int32_kind;
  };

  uint32_t inspector_id; /* Per-inspector identifier */
  uint32_t req_id;       /* Per-request identifier */
  uint32_t handle;       /* Handle */
  int32_t  status;

  struct timeval rt_time;

  union {
    struct {
      char *class_name;
      struct sigutils_channel channel;
      suscan_config_t *config;
      SUBOOL precise;
      uint32_t fs;  /* Baseband rate */
      SUFLOAT equiv_fs; /* Channel rate */
      SUFLOAT bandwidth;
      SUFLOAT lo;
      PTR_LIST(char, estimator);
      PTR_LIST(char, spectsrc);
    };

    struct {
      uint32_t estimator_id;
      SUBOOL   enabled;
      SUFLOAT  value;
    };

    struct {
      uint32_t  spectsrc_id;
      SUFLOAT  *spectrum_data;
      SUSCOUNT  spectrum_size;
      SUSCOUNT  samp_rate;
      SUFREQ    fc;
      SUFLOAT   N0;
    };

    struct {
      SUBOOL  tle_enable;
      orbit_t tle_orbit;
    };

    struct {
      char    *signal_name;
      SUDOUBLE signal_value;
    };
    
    struct suscan_orbit_report orbit_report;

    SUSCOUNT watermark;
  };
};

/***************************** Sender methods ********************************/
void suscan_analyzer_status_msg_destroy(struct suscan_analyzer_status_msg *status);
struct suscan_analyzer_status_msg *suscan_analyzer_status_msg_new(
    uint32_t code,
    const char *msg);

SUBOOL suscan_analyzer_send_status(
    suscan_analyzer_t *analyzer,
    uint32_t type,
    int code,
    const char *err_msg_fmt, ...);

SUBOOL suscan_analyzer_send_detector_channels(
    suscan_analyzer_t *analyzer,
    const su_channel_detector_t *detector);

SUBOOL suscan_analyzer_send_psd(
    suscan_analyzer_t *analyzer,
    const su_channel_detector_t *detector);

SUBOOL suscan_analyzer_send_psd_from_smoothpsd(
    suscan_analyzer_t *self,
    const su_smoothpsd_t *smoothpsd,
    SUBOOL looped,
    SUSCOUNT history_size);

SUBOOL suscan_analyzer_send_source_info(
    suscan_analyzer_t *self,
    const struct suscan_source_info *info);

/***************** Message constructors and destructors **********************/
/* Status message */
struct suscan_analyzer_status_msg *suscan_analyzer_status_msg_new(
    uint32_t code,
    const char *msg);
void suscan_analyzer_status_msg_destroy(struct suscan_analyzer_status_msg *status);

/* Channel list update */
struct suscan_analyzer_channel_msg *suscan_analyzer_channel_msg_new(
    const suscan_analyzer_t *analyzer,
    struct sigutils_channel **list,
    unsigned int len);
void suscan_analyzer_channel_msg_take_channels(
    struct suscan_analyzer_channel_msg *msg,
    struct sigutils_channel ***pchannel_list,
    unsigned int *pchannel_count);
void suscan_analyzer_channel_msg_destroy(struct suscan_analyzer_channel_msg *msg);

/* Channel inspector commands */
const char *suscan_analyzer_inspector_msgkind_to_str(
    enum suscan_analyzer_inspector_msgkind kind);

struct suscan_analyzer_inspector_msg *suscan_analyzer_inspector_msg_new(
    enum suscan_analyzer_inspector_msgkind kind,
    uint32_t req_id);

SUFLOAT *suscan_analyzer_inspector_msg_take_spectrum(
    struct suscan_analyzer_inspector_msg *msg);

void suscan_analyzer_inspector_msg_destroy(
    struct suscan_analyzer_inspector_msg *msg);

/* Spectrum update message */
struct suscan_analyzer_psd_msg *
suscan_analyzer_psd_msg_new_from_data(
    SUFLOAT samp_rate,
    const SUFLOAT *psd_data,
    SUSCOUNT psd_size);

struct suscan_analyzer_psd_msg *suscan_analyzer_psd_msg_new(
    const su_channel_detector_t *cd);

SUFLOAT *suscan_analyzer_psd_msg_take_psd(struct suscan_analyzer_psd_msg *msg);

void suscan_analyzer_psd_msg_destroy(struct suscan_analyzer_psd_msg *msg);

/* Sample batch message */
struct suscan_analyzer_sample_batch_msg *suscan_analyzer_sample_batch_msg_new(
    uint32_t inspector_id,
    const SUCOMPLEX *samples,
    SUSCOUNT count);

void suscan_analyzer_sample_batch_msg_destroy(
    struct suscan_analyzer_sample_batch_msg *msg);

/* Generic serializer / deserializer */
SUBOOL
suscan_analyzer_msg_serialize(
    uint32_t type,
    const void *ptr,
    grow_buf_t *buffer);

SUBOOL
suscan_analyzer_msg_deserialize_partial(uint32_t *type, grow_buf_t *buffer);

SUBOOL
suscan_analyzer_msg_deserialize(
    uint32_t *type,
    void **ptr,
    grow_buf_t *buffer);

/* Generic message disposer */
void suscan_analyzer_dispose_message(uint32_t type, void *ptr);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _MSG_H */
