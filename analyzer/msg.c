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

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <libgen.h>
#include <stdint.h>
#include <sigutils/util/compat-time.h>

#define SU_LOG_DOMAIN "msg"

#include "mq.h"
#include "msg.h"
#include "source.h"
#include <sgdp4/sgdp4.h>

#ifdef bool
#  undef bool
#endif /* bool */

/**************************** Status message **********************************/
SUSCAN_SERIALIZER_PROTO(suscan_analyzer_status_msg)
{
  SUSCAN_PACK_BOILERPLATE_START;

  SUSCAN_PACK(int, self->code);
  SUSCAN_PACK(str, self->err_msg);

  SUSCAN_PACK_BOILERPLATE_END;
}

SUSCAN_DESERIALIZER_PROTO(suscan_analyzer_status_msg)
{
  SUSCAN_UNPACK_BOILERPLATE_START;

  SUSCAN_UNPACK(int32, self->code);
  SUSCAN_UNPACK(str, self->err_msg);

  SUSCAN_UNPACK_BOILERPLATE_END;
}

void
suscan_analyzer_status_msg_destroy(struct suscan_analyzer_status_msg *status)
{
  if (status->err_msg != NULL)
    free(status->err_msg);

  free(status);
}

struct suscan_analyzer_status_msg *
suscan_analyzer_status_msg_new(uint32_t code, const char *msg)
{
  char *msg_dup = NULL;
  struct suscan_analyzer_status_msg *new;

  if (msg != NULL)
    if ((msg_dup = strdup(msg)) == NULL)
      return NULL;

  if ((new = malloc(sizeof(struct suscan_analyzer_status_msg))) == NULL) {
    if (msg_dup != NULL)
      free(msg_dup);
    return NULL;
  }

  new->err_msg = msg_dup;
  new->code = code;

  return new;
}

/***************************** Channel message ********************************/
void
suscan_analyzer_channel_msg_take_channels(
    struct suscan_analyzer_channel_msg *msg,
    struct sigutils_channel ***pchannel_list,
    unsigned int *pchannel_count)
{
  *pchannel_list = msg->channel_list;
  *pchannel_count = msg->channel_count;

  msg->channel_list = NULL;
  msg->channel_count = 0;
}

void
suscan_analyzer_channel_msg_destroy(struct suscan_analyzer_channel_msg *msg)
{
  unsigned int i;

  for (i = 0; i < msg->channel_count; ++i)
    if (msg->channel_list[i] != NULL)
      su_channel_destroy(msg->channel_list[i]);

  if (msg->channel_list != NULL)
    free(msg->channel_list);

  free(msg);
}

struct suscan_analyzer_channel_msg *
suscan_analyzer_channel_msg_new(
    const suscan_analyzer_t *analyzer,
    struct sigutils_channel **list,
    unsigned int len)
{
  unsigned int i;
  struct suscan_analyzer_channel_msg *new = NULL;
  unsigned int n = 0;
  SUFREQ fc;

  if ((new = calloc(1, sizeof(struct suscan_analyzer_channel_msg))) == NULL)
    goto fail;

  if (len > 0)
    if ((new->channel_list = calloc(len, sizeof(struct sigutils_channel *)))
        == NULL)
      goto fail;

  new->channel_count = len;
  new->source = NULL;
  new->sender = analyzer;

  fc = suscan_analyzer_get_source_info(analyzer)->frequency;

  for (i = 0; i < len; ++i)
    if (list[i] != NULL)
      if (SU_CHANNEL_IS_VALID(list[i])) {
        if ((new->channel_list[n] = su_channel_dup(list[i])) == NULL)
          goto fail;

        new->channel_list[n]->fc   += fc;
        new->channel_list[n]->f_hi += fc;
        new->channel_list[n]->f_lo += fc;
        new->channel_list[n]->ft    = fc;
        ++n;
      }

  new->channel_count = n;

  return new;

fail:
  if (new != NULL)
    suscan_analyzer_channel_msg_destroy(new);

  return NULL;
}

/******************************* PSD message **********************************/
SUFLOAT *
suscan_analyzer_psd_msg_take_psd(struct suscan_analyzer_psd_msg *msg)
{
  SUFLOAT *result = msg->psd_data;

  msg->psd_data = NULL;
  msg->psd_size = 0;

  return result;
}

SUSCAN_SERIALIZER_PROTO(suscan_analyzer_psd_msg)
{
  SUSCAN_PACK_BOILERPLATE_START;

  SUSCAN_PACK(int,   self->fc);
  SUSCAN_PACK(uint,  self->inspector_id);
  SUSCAN_PACK(uint,  self->timestamp.tv_sec);
  SUSCAN_PACK(uint,  self->timestamp.tv_usec);
  SUSCAN_PACK(uint,  self->rt_time.tv_sec);
  SUSCAN_PACK(uint,  self->rt_time.tv_usec);
  SUSCAN_PACK(bool,  self->looped);
  SUSCAN_PACK(uint,  self->history_size);
  SUSCAN_PACK(float, self->samp_rate);
  SUSCAN_PACK(float, self->measured_samp_rate);
  SUSCAN_PACK(float, self->N0);

  SU_TRYCATCH(
      suscan_pack_compact_single_array(
          buffer,
          self->psd_data,
          self->psd_size),
      goto fail);

  SUSCAN_PACK_BOILERPLATE_END;
}

SUSCAN_PARTIAL_DESERIALIZER_PROTO(suscan_analyzer_psd_msg)
{
  uint64_t tv_sec = 0;
  uint32_t tv_usec = 0;
  SUSCAN_UNPACK_BOILERPLATE_START;

  SUSCAN_UNPACK(int64,  self->fc);
  SUSCAN_UNPACK(uint32, self->inspector_id);

  SUSCAN_UNPACK(uint64, tv_sec);
  SUSCAN_UNPACK(uint32, tv_usec);
  self->timestamp.tv_sec  = tv_sec;
  self->timestamp.tv_usec = tv_usec;

  SUSCAN_UNPACK(uint64, tv_sec);
  SUSCAN_UNPACK(uint32, tv_usec);
  self->rt_time.tv_sec  = tv_sec;
  self->rt_time.tv_usec = tv_usec;

  SUSCAN_UNPACK(bool,   self->looped);
  SUSCAN_UNPACK(uint64, self->history_size);
  SUSCAN_UNPACK(float,  self->samp_rate);
  SUSCAN_UNPACK(float,  self->measured_samp_rate);
  SUSCAN_UNPACK(float,  self->N0);

  SUSCAN_UNPACK_BOILERPLATE_END;
}

SUSCAN_DESERIALIZER_PROTO(suscan_analyzer_psd_msg)
{
  SUSCAN_UNPACK_BOILERPLATE_START;

  SU_TRY_FAIL(
    suscan_analyzer_psd_msg_deserialize_partial(self, buffer));

  SU_TRY_FAIL(
      suscan_unpack_compact_single_array(
          buffer,
          &self->psd_data,
          &self->psd_size));

  SUSCAN_UNPACK_BOILERPLATE_END;
}

void
suscan_analyzer_psd_msg_destroy(struct suscan_analyzer_psd_msg *msg)
{
  if (msg->psd_data != NULL)
    free(msg->psd_data);

  free(msg);
}

struct suscan_analyzer_psd_msg *
suscan_analyzer_psd_msg_new_from_data(
    SUFLOAT samp_rate,
    const SUFLOAT *psd_data,
    SUSCOUNT psd_size)
{
  struct suscan_analyzer_psd_msg *new = NULL;

  SU_TRYCATCH(
      new = calloc(1, sizeof(struct suscan_analyzer_psd_msg)),
      goto fail);

  new->psd_size = psd_size;
  new->samp_rate = samp_rate;

  new->fc = 0;

  SU_TRYCATCH(
      new->psd_data = malloc(sizeof(SUFLOAT) * new->psd_size),
      goto fail);

  memcpy(new->psd_data, psd_data, psd_size * sizeof(SUFLOAT));

  gettimeofday(&new->rt_time, NULL);

  return new;

fail:
  if (new != NULL)
    suscan_analyzer_psd_msg_destroy(new);

  return NULL;
}

struct suscan_analyzer_psd_msg *
suscan_analyzer_psd_msg_new(const su_channel_detector_t *cd)
{
  struct suscan_analyzer_psd_msg *new = NULL;
  unsigned int i;

  SU_TRYCATCH(
      new = calloc(1, sizeof(struct suscan_analyzer_psd_msg)),
      goto fail);

  if (cd != NULL) {
    new->psd_size = cd->params.window_size;
    new->samp_rate = cd->params.samp_rate;

    if (cd->params.decimation > 1)
      new->samp_rate /= cd->params.decimation;

    new->fc = 0;

    SU_TRYCATCH(
        new->psd_data = malloc(sizeof(SUFLOAT) * new->psd_size),
        goto fail);

    switch (cd->params.mode) {
      case SU_CHANNEL_DETECTOR_MODE_AUTOCORRELATION:
        for (i = 0; i < new->psd_size; ++i)
          new->psd_data[i] = SU_C_REAL(cd->fft[i]);
        break;

      default:
        for (i = 0; i < new->psd_size; ++i) {
          new->psd_data[i] = SU_C_REAL(cd->fft[i] * SU_C_CONJ(cd->fft[i]));
          new->psd_data[i] /= cd->params.window_size;;
        }
    }
  }

  gettimeofday(&new->rt_time, NULL);

  return new;

fail:
  if (new != NULL)
    suscan_analyzer_psd_msg_destroy(new);

  return NULL;
}

/***************************** Inspector message ******************************/
SUSCAN_SERIALIZABLE(sigutils_channel);

SUSCAN_SERIALIZER_PROTO(sigutils_channel)
{
  SUSCAN_PACK_BOILERPLATE_START;

  SUSCAN_PACK(freq,  self->fc);
  SUSCAN_PACK(freq,  self->f_lo);
  SUSCAN_PACK(freq,  self->f_hi);
  SUSCAN_PACK(float, self->bw);
  SUSCAN_PACK(float, self->snr);
  SUSCAN_PACK(float, self->S0);
  SUSCAN_PACK(float, self->N0);
  SUSCAN_PACK(freq,  self->ft);

  SUSCAN_PACK(uint, self->age);
  SUSCAN_PACK(uint, self->present);

  SUSCAN_PACK_BOILERPLATE_END;
}

SUSCAN_DESERIALIZER_PROTO(sigutils_channel)
{
  SUSCAN_UNPACK_BOILERPLATE_START;

  SUSCAN_UNPACK(freq,   self->fc);
  SUSCAN_UNPACK(freq,   self->f_lo);
  SUSCAN_UNPACK(freq,   self->f_hi);
  SUSCAN_UNPACK(float,  self->bw);
  SUSCAN_UNPACK(float,  self->snr);
  SUSCAN_UNPACK(float,  self->S0);
  SUSCAN_UNPACK(float,  self->N0);
  SUSCAN_UNPACK(freq,   self->ft);

  SUSCAN_UNPACK(uint32, self->age);
  SUSCAN_UNPACK(uint32, self->present);

  SUSCAN_UNPACK_BOILERPLATE_END;
}

SUPRIVATE SUBOOL
suscan_analyzer_inspector_msg_serialize_open(
    grow_buf_t *buffer,
    const struct suscan_analyzer_inspector_msg *self)
{
  SUSCAN_PACK_BOILERPLATE_START;
  unsigned int i;

  SUSCAN_PACK(str,   self->class_name);

  SU_TRYCATCH(sigutils_channel_serialize(&self->channel, buffer), goto fail);

  if (self->config != NULL) {
    SU_TRYCATCH(suscan_config_serialize(self->config, buffer), goto fail);
  } else {
    SUSCAN_PACK(str, "<nullconfig>");
    SU_TRYCATCH(cbor_pack_map_start(buffer, 0) == 0, goto fail);
  }

  SUSCAN_PACK(uint,  self->handle);
  SUSCAN_PACK(bool,  self->precise);
  SUSCAN_PACK(uint,  self->fs);
  SUSCAN_PACK(float, self->equiv_fs);
  SUSCAN_PACK(float, self->bandwidth);
  SUSCAN_PACK(float, self->lo);

  SU_TRYCATCH(
      cbor_pack_array_start(buffer, self->estimator_count) == 0,
      goto fail);
  for (i = 0; i < self->estimator_count; ++i)
    SUSCAN_PACK(str, self->estimator_list[i]);

  SU_TRYCATCH(
      cbor_pack_array_start(buffer, self->spectsrc_count) == 0,
      goto fail);
  for (i = 0; i < self->spectsrc_count; ++i)
    SUSCAN_PACK(str, self->spectsrc_list[i]);

  SUSCAN_PACK_BOILERPLATE_END;
}

SUPRIVATE SUBOOL
suscan_analyzer_inspector_msg_deserialize_open(
    grow_buf_t *buffer,
    struct suscan_analyzer_inspector_msg *self)
{
  SUSCAN_UNPACK_BOILERPLATE_START;
  char *name = NULL;
  uint64_t nelem;
  SUBOOL end_required = SU_FALSE;
  unsigned int i;

  SUSCAN_UNPACK(str,    self->class_name);

  SU_TRYCATCH(sigutils_channel_deserialize(&self->channel, buffer), goto fail);

  SU_TRYCATCH(self->config = suscan_config_new(NULL), goto fail);
  SU_TRYCATCH(suscan_config_deserialize(self->config, buffer), goto fail);

  SUSCAN_UNPACK(uint32, self->handle);
  SUSCAN_UNPACK(bool,   self->precise);
  SUSCAN_UNPACK(uint32, self->fs);
  SUSCAN_UNPACK(float,  self->equiv_fs);
  SUSCAN_UNPACK(float,  self->bandwidth);
  SUSCAN_UNPACK(float,  self->lo);

  SU_TRYCATCH(
      cbor_unpack_array_start(
          buffer,
          &nelem,
          &end_required) == 0,
      goto fail);
  SU_TRYCATCH(!end_required, goto fail);
  self->estimator_count = nelem;

  SU_TRYCATCH(
      self->estimator_list = calloc(
          self->estimator_count,
          sizeof(struct suscan_estimator_class *)),
      goto fail);

  for (i = 0; i < self->estimator_count; ++i)
    SUSCAN_UNPACK(str, self->estimator_list[i]);

  SU_TRYCATCH(
      cbor_unpack_array_start(
          buffer,
          &nelem,
          &end_required) == 0,
      goto fail);
  SU_TRYCATCH(!end_required, goto fail);
  self->spectsrc_count = nelem;

  SU_TRYCATCH(
      self->spectsrc_list = calloc(
          self->spectsrc_count,
          sizeof(struct suscan_spectsrc_class *)),
      goto fail);

  for (i = 0; i < self->spectsrc_count; ++i)
    SUSCAN_UNPACK(str, self->spectsrc_list[i]);

  SUSCAN_UNPACK_BOILERPLATE_FINALLY;

  if (name != NULL)
    free(name);

  SUSCAN_UNPACK_BOILERPLATE_RETURN;
}

SUPRIVATE SUBOOL
suscan_analyzer_inspector_msg_serialize_config(
    grow_buf_t *buffer,
    const struct suscan_analyzer_inspector_msg *self)
{
  SUSCAN_PACK_BOILERPLATE_START;
  SU_TRYCATCH(suscan_config_serialize(self->config, buffer), goto fail);
  SUSCAN_PACK_BOILERPLATE_END;
}

SUPRIVATE SUBOOL
suscan_analyzer_inspector_msg_deserialize_config(
    grow_buf_t *buffer,
    struct suscan_analyzer_inspector_msg *self)
{
  SUSCAN_UNPACK_BOILERPLATE_START;
  SU_TRYCATCH(self->config = suscan_config_new(NULL), goto fail);
  SU_TRYCATCH(suscan_config_deserialize(self->config, buffer), goto fail);
  SUSCAN_UNPACK_BOILERPLATE_END;
}

SUPRIVATE SUBOOL
suscan_analyzer_inspector_msg_serialize_estimator(
    grow_buf_t *buffer,
    const struct suscan_analyzer_inspector_msg *self)
{
  SUSCAN_PACK_BOILERPLATE_START;
  SUSCAN_PACK(uint,  self->estimator_id);
  SUSCAN_PACK(bool,  self->enabled);
  SUSCAN_PACK(float, self->value);
  SUSCAN_PACK_BOILERPLATE_END;
}

SUPRIVATE SUBOOL
suscan_analyzer_inspector_msg_deserialize_estimator(
    grow_buf_t *buffer,
    struct suscan_analyzer_inspector_msg *self)
{
  SUSCAN_UNPACK_BOILERPLATE_START;
  SUSCAN_UNPACK(uint32, self->estimator_id);
  SUSCAN_UNPACK(bool,   self->enabled);
  SUSCAN_UNPACK(float,  self->value);
  SUSCAN_UNPACK_BOILERPLATE_END;
}

SUPRIVATE SUBOOL
suscan_analyzer_inspector_msg_serialize_spectrum(
    grow_buf_t *buffer,
    const struct suscan_analyzer_inspector_msg *self)
{
  SUSCAN_PACK_BOILERPLATE_START;
  SUSCAN_PACK(uint,   self->spectsrc_id);
  SUSCAN_PACK(freq,   self->fc);
  SUSCAN_PACK(float,  self->N0);
  SUSCAN_PACK(uint,   self->samp_rate);

  SU_TRYCATCH(
      suscan_pack_compact_float_array(
          buffer,
          self->spectrum_data,
          self->spectrum_size),
      goto fail);

  SUSCAN_PACK_BOILERPLATE_END;
}

SUPRIVATE SUBOOL
suscan_analyzer_inspector_msg_deserialize_spectrum(
    grow_buf_t *buffer,
    struct suscan_analyzer_inspector_msg *self)
{
  SUSCAN_UNPACK_BOILERPLATE_START;
  SUSCAN_UNPACK(uint32, self->spectsrc_id);
  SUSCAN_UNPACK(freq,   self->fc);
  SUSCAN_UNPACK(float,  self->N0);
  SUSCAN_UNPACK(uint64, self->samp_rate);

  SU_TRYCATCH(
      suscan_unpack_compact_float_array(
          buffer,
          &self->spectrum_data,
          &self->spectrum_size),
      goto fail);

  SUSCAN_UNPACK_BOILERPLATE_END;
}

SUPRIVATE SUBOOL
suscan_analyzer_inspector_msg_serialize_set_freq(
    grow_buf_t *buffer,
    const struct suscan_analyzer_inspector_msg *self)
{
  SUSCAN_PACK_BOILERPLATE_START;

  SUSCAN_PACK(freq, self->channel.fc);
  SUSCAN_PACK(freq, self->channel.ft);

  SUSCAN_PACK_BOILERPLATE_END;
}

SUPRIVATE SUBOOL
suscan_analyzer_inspector_msg_deserialize_set_freq(
    grow_buf_t *buffer,
    struct suscan_analyzer_inspector_msg *self)
{
  SUSCAN_UNPACK_BOILERPLATE_START;

  SUSCAN_UNPACK(freq, self->channel.fc);
  SUSCAN_UNPACK(freq, self->channel.ft);

  SUSCAN_UNPACK_BOILERPLATE_END;
}

SUPRIVATE SUBOOL
suscan_analyzer_inspector_msg_serialize_set_bandwidth(
    grow_buf_t *buffer,
    const struct suscan_analyzer_inspector_msg *self)
{
  SUSCAN_PACK_BOILERPLATE_START;

  SUSCAN_PACK(float, self->channel.bw);

  SUSCAN_PACK_BOILERPLATE_END;
}

SUPRIVATE SUBOOL
suscan_analyzer_inspector_msg_deserialize_set_bandwidth(
    grow_buf_t *buffer,
    struct suscan_analyzer_inspector_msg *self)
{
  SUSCAN_UNPACK_BOILERPLATE_START;

  SUSCAN_UNPACK(float, self->channel.bw);

  SUSCAN_UNPACK_BOILERPLATE_END;
}

SUPRIVATE SUBOOL
suscan_analyzer_inspector_msg_serialize_set_watermark(
    grow_buf_t *buffer,
    const struct suscan_analyzer_inspector_msg *self)
{
  SUSCAN_PACK_BOILERPLATE_START;

  SUSCAN_PACK(uint, self->watermark);

  SUSCAN_PACK_BOILERPLATE_END;
}

SUPRIVATE SUBOOL
suscan_analyzer_inspector_msg_deserialize_set_watermark(
    grow_buf_t *buffer,
    struct suscan_analyzer_inspector_msg *self)
{
  SUSCAN_UNPACK_BOILERPLATE_START;

  SUSCAN_UNPACK(uint64, self->watermark);

  SUSCAN_UNPACK_BOILERPLATE_END;
}

SUPRIVATE SUBOOL
suscan_analyzer_inspector_msg_serialize_set_tle(
    grow_buf_t *buffer,
    const struct suscan_analyzer_inspector_msg *self)
{
  SUSCAN_PACK_BOILERPLATE_START;

  SUSCAN_PACK(bool, self->tle_enable);

  if (self->tle_enable) {
    SUSCAN_PACK(str,    self->tle_orbit.name);
    SUSCAN_PACK(int,    self->tle_orbit.ep_year);
    SUSCAN_PACK(double, self->tle_orbit.ep_day);
    SUSCAN_PACK(double, self->tle_orbit.rev);
    SUSCAN_PACK(double, self->tle_orbit.drevdt);
    SUSCAN_PACK(double, self->tle_orbit.d2revdt2);
    SUSCAN_PACK(double, self->tle_orbit.bstar);
    SUSCAN_PACK(double, self->tle_orbit.eqinc);
    SUSCAN_PACK(double, self->tle_orbit.ecc);
    SUSCAN_PACK(double, self->tle_orbit.mnan);
    SUSCAN_PACK(double, self->tle_orbit.argp);
    SUSCAN_PACK(double, self->tle_orbit.ascn);
    SUSCAN_PACK(double, self->tle_orbit.smjaxs);
    SUSCAN_PACK(int,    self->tle_orbit.norb);
    SUSCAN_PACK(int,    self->tle_orbit.satno);
  }

  SUSCAN_PACK_BOILERPLATE_END;
}

SUPRIVATE SUBOOL
suscan_analyzer_inspector_msg_deserialize_set_tle(
    grow_buf_t *buffer,
    struct suscan_analyzer_inspector_msg *self)
{
  SUSCAN_UNPACK_BOILERPLATE_START;

  SUSCAN_UNPACK(bool, self->tle_enable);

  if (self->tle_enable) {
    SUSCAN_UNPACK(str,    self->tle_orbit.name);
    SUSCAN_UNPACK(int32,  self->tle_orbit.ep_year);
    SUSCAN_UNPACK(double, self->tle_orbit.ep_day);
    SUSCAN_UNPACK(double, self->tle_orbit.rev);
    SUSCAN_UNPACK(double, self->tle_orbit.drevdt);
    SUSCAN_UNPACK(double, self->tle_orbit.d2revdt2);
    SUSCAN_UNPACK(double, self->tle_orbit.bstar);
    SUSCAN_UNPACK(double, self->tle_orbit.eqinc);
    SUSCAN_UNPACK(double, self->tle_orbit.ecc);
    SUSCAN_UNPACK(double, self->tle_orbit.mnan);
    SUSCAN_UNPACK(double, self->tle_orbit.argp);
    SUSCAN_UNPACK(double, self->tle_orbit.ascn);
    SUSCAN_UNPACK(double, self->tle_orbit.smjaxs);
    SUSCAN_UNPACK(int64,  self->tle_orbit.norb);
    SUSCAN_UNPACK(int32,  self->tle_orbit.satno);
  }
  
  SUSCAN_UNPACK_BOILERPLATE_END;
}

SUPRIVATE SUBOOL
suscan_analyzer_inspector_msg_serialize_orbit_report(
    grow_buf_t *buffer,
    const struct suscan_analyzer_inspector_msg *self)
{
  SUSCAN_PACK_BOILERPLATE_START;

  SUSCAN_PACK(uint, self->orbit_report.rx_time.tv_sec);
  SUSCAN_PACK(uint, self->orbit_report.rx_time.tv_usec);

  SUSCAN_PACK(double, self->orbit_report.satpos.azimuth);
  SUSCAN_PACK(double, self->orbit_report.satpos.elevation);
  SUSCAN_PACK(double, self->orbit_report.satpos.distance);

  SUSCAN_PACK(float, self->orbit_report.freq_corr);
  SUSCAN_PACK(double, self->orbit_report.vlos_vel);

  SUSCAN_PACK_BOILERPLATE_END;
}

SUPRIVATE SUBOOL
suscan_analyzer_inspector_msg_deserialize_orbit_report(
    grow_buf_t *buffer,
    struct suscan_analyzer_inspector_msg *self)
{
  SUSCAN_UNPACK_BOILERPLATE_START;
  uint64_t tv_sec = 0;
  uint32_t tv_usec = 0;

  SUSCAN_UNPACK(uint64, tv_sec);
  SUSCAN_UNPACK(uint32, tv_usec);

  self->orbit_report.rx_time.tv_sec = tv_sec;
  self->orbit_report.rx_time.tv_usec = tv_usec;

  SUSCAN_UNPACK(double, self->orbit_report.satpos.azimuth);
  SUSCAN_UNPACK(double, self->orbit_report.satpos.elevation);
  SUSCAN_UNPACK(double, self->orbit_report.satpos.distance);

  SUSCAN_UNPACK(float, self->orbit_report.freq_corr);
  SUSCAN_UNPACK(double, self->orbit_report.vlos_vel);

  SUSCAN_UNPACK_BOILERPLATE_END;
}

SUPRIVATE SUBOOL
suscan_analyzer_inspector_msg_serialize_signal(
    grow_buf_t *buffer,
    const struct suscan_analyzer_inspector_msg *self)
{
  SUSCAN_PACK_BOILERPLATE_START;

  SUSCAN_PACK(str,    self->signal_name);
  SUSCAN_PACK(double, self->signal_value);

  SUSCAN_PACK_BOILERPLATE_END;
}

SUPRIVATE SUBOOL
suscan_analyzer_inspector_msg_deserialize_signal(
    grow_buf_t *buffer,
    struct suscan_analyzer_inspector_msg *self)
{
  SUSCAN_UNPACK_BOILERPLATE_START;
  
  SUSCAN_UNPACK(str,    self->signal_name);
  SUSCAN_UNPACK(double, self->signal_value);

  SUSCAN_UNPACK_BOILERPLATE_END;
}

SUSCAN_SERIALIZER_PROTO(suscan_analyzer_inspector_msg)
{
  SUSCAN_PACK_BOILERPLATE_START;

  SUSCAN_PACK(int, self->kind);
  SUSCAN_PACK(int, self->inspector_id);
  SUSCAN_PACK(int, self->req_id);
  SUSCAN_PACK(int, self->handle);
  SUSCAN_PACK(int, self->status);

  SUSCAN_PACK(uint, self->rt_time.tv_sec);
  SUSCAN_PACK(uint, self->rt_time.tv_usec);

  switch (self->kind) {
    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_OPEN:
      SU_TRYCATCH(
          suscan_analyzer_inspector_msg_serialize_open(buffer, self),
          goto fail);
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_CONFIG:
      SU_TRYCATCH(
          suscan_analyzer_inspector_msg_serialize_config(buffer, self),
          goto fail);
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_ESTIMATOR:
      SU_TRYCATCH(
          suscan_analyzer_inspector_msg_serialize_estimator(buffer, self),
          goto fail);
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SPECTRUM:
      SU_TRYCATCH(
          suscan_analyzer_inspector_msg_serialize_spectrum(buffer, self),
          goto fail);
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_FREQ:
      SU_TRYCATCH(
          suscan_analyzer_inspector_msg_serialize_set_freq(buffer, self),
          goto fail);
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_BANDWIDTH:
      SU_TRYCATCH(
          suscan_analyzer_inspector_msg_serialize_set_bandwidth(buffer, self),
          goto fail);
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_WATERMARK:
      SU_TRYCATCH(
          suscan_analyzer_inspector_msg_serialize_set_watermark(buffer, self),
          goto fail);
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_TLE:
      SU_TRYCATCH(
          suscan_analyzer_inspector_msg_serialize_set_tle(buffer, self),
          goto fail);
      break;
    
    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_ORBIT_REPORT:
      SU_TRYCATCH(
          suscan_analyzer_inspector_msg_serialize_orbit_report(buffer, self),
          goto fail);
      break;
    
    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SIGNAL:
      SU_TRYCATCH(
          suscan_analyzer_inspector_msg_serialize_signal(buffer, self),
          goto fail);
    
    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_NOOP:
    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_ID:
    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_GET_CONFIG:
    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_RESET_EQUALIZER:
    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_CLOSE:
    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE:
    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_OBJECT:
    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_INVALID_ARGUMENT:
    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_KIND:
    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_INVALID_CHANNEL:
    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_INVALID_CORRECTION:
      /* Empty messages */
      break;

    default:
      SU_ERROR("Inspector message kind=%d is not supported\n", self->kind);
      goto fail;
  }

  SUSCAN_PACK_BOILERPLATE_END;
}

SUSCAN_DESERIALIZER_PROTO(suscan_analyzer_inspector_msg)
{
  SUSCAN_UNPACK_BOILERPLATE_START;
  uint64_t tv_sec = 0;
  uint32_t tv_usec = 0;

  SUSCAN_UNPACK(uint32, self->int32_kind);
  SUSCAN_UNPACK(uint32, self->inspector_id);
  SUSCAN_UNPACK(uint32, self->req_id);
  SUSCAN_UNPACK(uint32, self->handle);
  SUSCAN_UNPACK(int32,  self->status);

  SUSCAN_UNPACK(uint64, tv_sec);
  SUSCAN_UNPACK(uint32, tv_usec);
  self->rt_time.tv_sec  = tv_sec;
  self->rt_time.tv_usec = tv_usec;

  switch (self->kind) {
    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_OPEN:
      SU_TRYCATCH(
          suscan_analyzer_inspector_msg_deserialize_open(buffer, self),
          goto fail);
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_CONFIG:
      SU_TRYCATCH(
          suscan_analyzer_inspector_msg_deserialize_config(buffer, self),
          goto fail);
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_ESTIMATOR:
      SU_TRYCATCH(
          suscan_analyzer_inspector_msg_deserialize_estimator(buffer, self),
          goto fail);
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SPECTRUM:
      SU_TRYCATCH(
          suscan_analyzer_inspector_msg_deserialize_spectrum(buffer, self),
          goto fail);
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_FREQ:
      SU_TRYCATCH(
          suscan_analyzer_inspector_msg_deserialize_set_freq(buffer, self),
          goto fail);
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_BANDWIDTH:
      SU_TRYCATCH(
          suscan_analyzer_inspector_msg_deserialize_set_bandwidth(buffer, self),
          goto fail);
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_WATERMARK:
      SU_TRYCATCH(
          suscan_analyzer_inspector_msg_deserialize_set_watermark(buffer, self),
          goto fail);
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_TLE:
      SU_TRYCATCH(
          suscan_analyzer_inspector_msg_deserialize_set_tle(buffer, self),
          goto fail);
      break;
    
    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_ORBIT_REPORT:
      SU_TRYCATCH(
          suscan_analyzer_inspector_msg_deserialize_orbit_report(buffer, self),
          goto fail);
      break;
    
    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SIGNAL:
      SU_TRYCATCH(
          suscan_analyzer_inspector_msg_deserialize_signal(buffer, self),
          goto fail);
      break;
    
    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_NOOP:
    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_ID:
    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_GET_CONFIG:
    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_RESET_EQUALIZER:
    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_CLOSE:
    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE:
    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_OBJECT:
    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_INVALID_ARGUMENT:
    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_KIND:
    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_INVALID_CHANNEL:
    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_INVALID_CORRECTION:
      /* Empty messages */
      break;

    default:
      SU_ERROR("Inspector message kind = %d is not supported\n", self->kind);
      goto fail;
  }

  SUSCAN_UNPACK_BOILERPLATE_END;
}

struct suscan_analyzer_inspector_msg *
suscan_analyzer_inspector_msg_new(
    enum suscan_analyzer_inspector_msgkind kind,
    uint32_t req_id)
{
  struct suscan_analyzer_inspector_msg *new;

  if ((new = calloc(1, sizeof (struct suscan_analyzer_inspector_msg))) == NULL)
    return NULL;

  new->kind = kind;
  new->req_id = req_id;

  gettimeofday(&new->rt_time, NULL);
  
  return new;
}

SUFLOAT *
suscan_analyzer_inspector_msg_take_spectrum(
    struct suscan_analyzer_inspector_msg *msg)
{
  SUFLOAT *result = msg->spectrum_data;

  msg->spectrum_data = NULL;
  msg->spectrum_size = 0;

  return result;
}

void
suscan_analyzer_inspector_msg_destroy(struct suscan_analyzer_inspector_msg *msg)
{
  unsigned int i = 0;

  switch (msg->kind) {
    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_GET_CONFIG:
    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_CONFIG:
    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_OPEN:
      if (msg->config != NULL)
        suscan_config_destroy(msg->config);

      if (msg->estimator_list != NULL) {
        for (i = 0; i < msg->estimator_count; ++i)
          if (msg->estimator_list[i] != NULL)
            free(msg->estimator_list[i]);
        free(msg->estimator_list);
      }

      if (msg->spectsrc_list != NULL) {
        for (i = 0; i < msg->spectsrc_count; ++i)
          if (msg->spectsrc_list[i] != NULL)
            free(msg->spectsrc_list[i]);
        free(msg->spectsrc_list);
      }

      if (msg->class_name != NULL)
        free(msg->class_name);
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SPECTRUM:
      if (msg->spectrum_data != NULL)
        free(msg->spectrum_data);
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_TLE:
      if (msg->tle_enable)
        orbit_finalize(&msg->tle_orbit);
      break;

    case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SIGNAL:
      if (msg->signal_name != NULL)
        free(msg->signal_name);
      
    default:
      ;
  }

  free(msg);
}

/************************** Sample batch message ******************************/
SUSCAN_SERIALIZER_PROTO(suscan_analyzer_sample_batch_msg)
{
  SUSCAN_PACK_BOILERPLATE_START;

  SUSCAN_PACK(int, self->inspector_id);
  SU_TRYCATCH(
      suscan_pack_compact_complex_array(
          buffer,
          self->samples,
          self->sample_count),
      goto fail);

  SUSCAN_PACK_BOILERPLATE_END;
}

SUSCAN_DESERIALIZER_PROTO(suscan_analyzer_sample_batch_msg)
{
  SUSCAN_UNPACK_BOILERPLATE_START;

  SUSCAN_UNPACK(uint32, self->inspector_id);
  SU_TRYCATCH(
      suscan_unpack_compact_complex_array(
          buffer,
          &self->samples,
          &self->sample_count),
      goto fail);

  SUSCAN_UNPACK_BOILERPLATE_END;
}

struct suscan_analyzer_sample_batch_msg *
suscan_analyzer_sample_batch_msg_new(
    uint32_t inspector_id,
    const SUCOMPLEX *samples,
    SUSCOUNT count)
{
  struct suscan_analyzer_sample_batch_msg *new = NULL;

  SU_TRYCATCH(
      new = calloc(1, sizeof(struct suscan_analyzer_sample_batch_msg)),
      goto fail);

  if (samples != NULL && count > 0) {
    SU_TRYCATCH(
          new->samples = malloc(count * sizeof(SUCOMPLEX)),
          goto fail);

    memcpy(new->samples, samples, count * sizeof(SUCOMPLEX));
  }

  new->sample_count = count;
  new->inspector_id = inspector_id;

  return new;

fail:
  if (new != NULL)
    suscan_analyzer_sample_batch_msg_destroy(new);

  return NULL;
}

void
suscan_analyzer_sample_batch_msg_destroy(
    struct suscan_analyzer_sample_batch_msg *msg)
{
  if (msg->samples != NULL)
    free(msg->samples);

  free(msg);
}


/************************** Throttle message **********************************/
SUSCAN_SERIALIZER_PROTO(suscan_analyzer_throttle_msg)
{
  SUSCAN_PACK_BOILERPLATE_START;

  SUSCAN_PACK(uint, self->samp_rate);

  SUSCAN_PACK_BOILERPLATE_END;
}

SUSCAN_DESERIALIZER_PROTO(suscan_analyzer_throttle_msg)
{
  SUSCAN_UNPACK_BOILERPLATE_START;

  SUSCAN_UNPACK(uint64, self->samp_rate);

  SUSCAN_UNPACK_BOILERPLATE_END;
}

/**************************** Seek message ************************************/
SUSCAN_SERIALIZER_PROTO(suscan_analyzer_seek_msg)
{
  SUSCAN_PACK_BOILERPLATE_START;

  SUSCAN_PACK(uint, self->position.tv_sec);
  SUSCAN_PACK(uint, self->position.tv_usec);

  SUSCAN_PACK_BOILERPLATE_END;
}

SUSCAN_DESERIALIZER_PROTO(suscan_analyzer_seek_msg)
{
  SUSCAN_UNPACK_BOILERPLATE_START;
  uint64_t tv_sec = 0;
  uint32_t tv_usec = 0;

  SUSCAN_UNPACK(uint64, tv_sec);
  SUSCAN_UNPACK(uint32, tv_usec);

  self->position.tv_sec = tv_sec;
  self->position.tv_usec = tv_usec;

  SUSCAN_UNPACK_BOILERPLATE_END;
}

/************************** History size message ******************************/
SUSCAN_SERIALIZER_PROTO(suscan_analyzer_history_size_msg)
{
  SUSCAN_PACK_BOILERPLATE_START;

  SUSCAN_PACK(uint, self->buffer_length);

  SUSCAN_PACK_BOILERPLATE_END;
}

SUSCAN_DESERIALIZER_PROTO(suscan_analyzer_history_size_msg)
{
  SUSCAN_UNPACK_BOILERPLATE_START;

  SUSCAN_UNPACK(uint64, self->buffer_length);

  SUSCAN_UNPACK_BOILERPLATE_END;
}

/************************** History replay message ***************************/
SUSCAN_SERIALIZER_PROTO(suscan_analyzer_replay_msg)
{
  SUSCAN_PACK_BOILERPLATE_START;

  SUSCAN_PACK(bool, self->replay);

  SUSCAN_PACK_BOILERPLATE_END;
}

SUSCAN_DESERIALIZER_PROTO(suscan_analyzer_replay_msg)
{
  SUSCAN_UNPACK_BOILERPLATE_START;

  SUSCAN_UNPACK(bool, self->replay);

  SUSCAN_UNPACK_BOILERPLATE_END;
}

/*********************** Generic message serialization ************************/
SUBOOL
suscan_analyzer_msg_serialize(
    uint32_t type,
    const void *ptr,
    grow_buf_t *buffer)
{
  SUSCAN_PACK_BOILERPLATE_START;

  SUSCAN_PACK(uint, type);

  switch (type) {
    case SUSCAN_ANALYZER_MESSAGE_TYPE_SOURCE_INFO:
      SU_TRY_FAIL(suscan_source_info_serialize(ptr, buffer));
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_READ_ERROR:
    case SUSCAN_ANALYZER_MESSAGE_TYPE_SOURCE_INIT:
    case SUSCAN_ANALYZER_MESSAGE_TYPE_EOS:
      SU_TRY_FAIL(suscan_analyzer_status_msg_serialize(ptr, buffer));
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_CHANNEL:
      SU_WARNING("Channel-type messages are not currently supported\n");
      goto fail;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR:
      SU_TRY_FAIL(suscan_analyzer_inspector_msg_serialize(ptr, buffer));
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_PSD:
      SU_TRY_FAIL(suscan_analyzer_psd_msg_serialize(ptr, buffer));
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_SAMPLES:
      SU_TRY_FAIL(suscan_analyzer_sample_batch_msg_serialize(ptr, buffer));
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_THROTTLE:
      SU_TRY_FAIL(suscan_analyzer_throttle_msg_serialize(ptr, buffer));
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_PARAMS:
      SU_TRY_FAIL(suscan_analyzer_params_serialize(ptr, buffer));
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_SEEK:
      SU_TRY_FAIL(suscan_analyzer_seek_msg_serialize(ptr, buffer));
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_GET_PARAMS:
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_HISTORY_SIZE:
      SU_TRY_FAIL(suscan_analyzer_history_size_msg_serialize(ptr, buffer));
      break;
    
    case SUSCAN_ANALYZER_MESSAGE_TYPE_REPLAY:
      SU_TRY_FAIL(suscan_analyzer_replay_msg_serialize(ptr, buffer));
      break;
    
  }

  SUSCAN_PACK_BOILERPLATE_END;
}

SUBOOL
suscan_analyzer_msg_deserialize_partial(uint32_t *type, grow_buf_t *buffer)
{
  SUSCAN_UNPACK_BOILERPLATE_START;

  SUSCAN_UNPACK(uint32, *type);

  SUSCAN_UNPACK_BOILERPLATE_END;
}

SUBOOL
suscan_analyzer_msg_deserialize(uint32_t *type, void **ptr, grow_buf_t *buffer)
{
  SUSCAN_UNPACK_BOILERPLATE_START;
  void *msgptr = NULL;

  SU_TRY_FAIL(suscan_analyzer_msg_deserialize_partial(type, buffer));

  switch (*type) {
    case SUSCAN_ANALYZER_MESSAGE_TYPE_SOURCE_INFO:
      SU_TRY_FAIL(msgptr = calloc(1, sizeof (struct suscan_source_info)));
      SU_TRY_FAIL(suscan_source_info_deserialize(msgptr, buffer));
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_READ_ERROR:
    case SUSCAN_ANALYZER_MESSAGE_TYPE_SOURCE_INIT:
    case SUSCAN_ANALYZER_MESSAGE_TYPE_EOS:
      SU_TRY_FAIL(msgptr = suscan_analyzer_status_msg_new(0, NULL));
      SU_TRY_FAIL(suscan_analyzer_status_msg_deserialize(msgptr, buffer));
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_CHANNEL:
      SU_WARNING("Channel-type messages are not currently supported\n");
      goto fail;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR:
      SU_TRY_FAIL(msgptr = suscan_analyzer_inspector_msg_new(0, 0));
      SU_TRY_FAIL(suscan_analyzer_inspector_msg_deserialize(msgptr, buffer));
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_PSD:
      SU_TRY_FAIL(msgptr = suscan_analyzer_psd_msg_new(NULL));
      SU_TRY_FAIL(suscan_analyzer_psd_msg_deserialize(msgptr, buffer));
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_SAMPLES:
      SU_TRY_FAIL(msgptr = suscan_analyzer_sample_batch_msg_new(0, NULL, 0));
      SU_TRY_FAIL(suscan_analyzer_sample_batch_msg_deserialize(msgptr, buffer));
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_THROTTLE:
      SU_TRY_FAIL(msgptr = calloc(1, sizeof (struct suscan_analyzer_throttle_msg)));
      SU_TRY_FAIL(suscan_analyzer_throttle_msg_deserialize(msgptr, buffer));
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_PARAMS:
      SU_TRY_FAIL(msgptr = calloc(1, sizeof (struct suscan_analyzer_params)));
      SU_TRY_FAIL(suscan_analyzer_params_deserialize(msgptr, buffer));
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_SEEK:
      SU_TRY_FAIL(msgptr = calloc(1, sizeof (struct suscan_analyzer_seek_msg)));
      SU_TRY_FAIL(suscan_analyzer_seek_msg_deserialize(msgptr, buffer));
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_GET_PARAMS:
      msgptr = "REMOTE";
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_HISTORY_SIZE:
      SU_TRY_FAIL(msgptr = calloc(1, sizeof (struct suscan_analyzer_history_size_msg)));
      SU_TRY_FAIL(suscan_analyzer_history_size_msg_deserialize(msgptr, buffer));
      break;
    
    case SUSCAN_ANALYZER_MESSAGE_TYPE_REPLAY:
      SU_TRY_FAIL(msgptr = calloc(1, sizeof (struct suscan_analyzer_replay_msg)));
      SU_TRY_FAIL(suscan_analyzer_replay_msg_deserialize(msgptr, buffer));
      break;

    default:
      SU_WARNING("Unknown message type `%d'\n", *type);
      goto fail;
  }

  SUSCAN_UNPACK_BOILERPLATE_FINALLY;

  if (ok)
    *ptr = msgptr;
  else if (msgptr != NULL)
    suscan_analyzer_dispose_message(*type, msgptr);

  SUSCAN_UNPACK_BOILERPLATE_RETURN;
}

/************************ Generic message disposal ****************************/
void
suscan_analyzer_dispose_message(uint32_t type, void *ptr)
{
  switch (type) {
    case SUSCAN_ANALYZER_MESSAGE_TYPE_SOURCE_INFO:
      suscan_source_info_finalize(ptr);
      free(ptr);
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_SOURCE_INIT:
    case SUSCAN_ANALYZER_MESSAGE_TYPE_READ_ERROR:
    case SUSCAN_ANALYZER_MESSAGE_TYPE_EOS:
    case SUSCAN_ANALYZER_MESSAGE_TYPE_INTERNAL:
      suscan_analyzer_status_msg_destroy(ptr);
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_CHANNEL:
      suscan_analyzer_channel_msg_destroy(ptr);
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR:
      suscan_analyzer_inspector_msg_destroy(ptr);
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_PSD:
      suscan_analyzer_psd_msg_destroy(ptr);
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_SAMPLES:
      suscan_analyzer_sample_batch_msg_destroy(ptr);
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_PARAMS:
    case SUSCAN_ANALYZER_MESSAGE_TYPE_THROTTLE:
      free(ptr);
      break;
  }
}

/****************************** Sender methods *******************************/
SUBOOL
suscan_analyzer_send_status(
    suscan_analyzer_t *analyzer,
    uint32_t type,
    int code,
    const char *err_msg_fmt, ...)
{
  struct suscan_analyzer_status_msg *msg;
  va_list ap;
  char *err_msg = NULL;
  SUBOOL ok = SU_FALSE;

  va_start(ap, err_msg_fmt);

  if (err_msg_fmt != NULL)
    if ((err_msg = vstrbuild(err_msg_fmt, ap)) == NULL)
      goto done;

  if ((msg = suscan_analyzer_status_msg_new(code, err_msg)) == NULL)
    goto done;

  msg->sender = analyzer;

  if (!suscan_mq_write(analyzer->mq_out, type, msg)) {
    suscan_analyzer_dispose_message(type, msg);
    goto done;
  }

  ok = SU_TRUE;

done:
  if (err_msg != NULL)
    free(err_msg);

  va_end(ap);

  return ok;
}

SUBOOL
suscan_analyzer_send_detector_channels(
    suscan_analyzer_t *analyzer,
    const su_channel_detector_t *detector)
{
  struct suscan_analyzer_channel_msg *msg = NULL;
  struct sigutils_channel **ch_list;
  unsigned int ch_count;
  SUBOOL ok = SU_FALSE;

  su_channel_detector_get_channel_list(detector, &ch_list, &ch_count);

  if ((msg = suscan_analyzer_channel_msg_new(analyzer, ch_list, ch_count))
      == NULL) {
    suscan_analyzer_send_status(
        analyzer,
        SUSCAN_ANALYZER_MESSAGE_TYPE_INTERNAL,
        -1,
        "Cannot create message: %s",
        strerror(errno));
    goto done;
  }

  if (!suscan_mq_write(
      analyzer->mq_out,
      SUSCAN_ANALYZER_MESSAGE_TYPE_CHANNEL,
      msg)) {
    suscan_analyzer_send_status(
        analyzer,
        SUSCAN_ANALYZER_MESSAGE_TYPE_INTERNAL,
        -1,
        "Cannot write message: %s",
        strerror(errno));
    goto done;
  }

  /* Message queued, forget about it */
  msg = NULL;

  ok = SU_TRUE;

done:
  if (msg != NULL)
    suscan_analyzer_dispose_message(SUSCAN_ANALYZER_MESSAGE_TYPE_CHANNEL, msg);
  return ok;
}

SUBOOL
suscan_analyzer_send_source_info(
    suscan_analyzer_t *self,
    const struct suscan_source_info *info)
{
  struct suscan_source_info *copy = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      copy = calloc(1, sizeof(struct suscan_source_info)),
      goto done);

  // XXX: Protect!
  SU_TRYCATCH(suscan_source_info_init_copy(copy, info), goto done);

  /* Send source info */
  suscan_analyzer_get_source_time(self, &copy->source_time);
  
  SU_TRYCATCH(
      suscan_mq_write(
          self->mq_out,
          SUSCAN_ANALYZER_MESSAGE_TYPE_SOURCE_INFO,
          copy),
      goto done);

  copy = NULL;

  ok = SU_TRUE;

done:
  if (copy != NULL) {
    suscan_source_info_finalize(copy);
    free(copy);
  }

  return ok;
}

SUBOOL
suscan_analyzer_send_psd(
    suscan_analyzer_t *self,
    const su_channel_detector_t *detector)
{
  struct suscan_analyzer_psd_msg *msg = NULL;
  SUBOOL ok = SU_FALSE;

  if ((msg = suscan_analyzer_psd_msg_new(detector)) == NULL) {
    suscan_analyzer_send_status(
        self,
        SUSCAN_ANALYZER_MESSAGE_TYPE_INTERNAL,
        -1,
        "Cannot create message: %s",
        strerror(errno));
    goto done;
  }

  /* In wide spectrum mode, frequency is given by curr_freq */
  msg->fc = suscan_analyzer_get_source_info(self)->frequency;
  msg->samp_rate = suscan_analyzer_get_source_info(self)->source_samp_rate;
  msg->measured_samp_rate = suscan_analyzer_get_measured_samp_rate(self);
  suscan_analyzer_get_source_time(self, &msg->timestamp);
  msg->N0 = detector->N0;

  if (!suscan_mq_write(
      self->mq_out,
      SUSCAN_ANALYZER_MESSAGE_TYPE_PSD,
      msg)) {
    suscan_analyzer_send_status(
        self,
        SUSCAN_ANALYZER_MESSAGE_TYPE_INTERNAL,
        -1,
        "Cannot write message: %s",
        strerror(errno));
    goto done;
  }

  /* Message queued, forget about it */
  msg = NULL;

  ok = SU_TRUE;

done:
  if (msg != NULL)
    suscan_analyzer_dispose_message(SUSCAN_ANALYZER_MESSAGE_TYPE_PSD, msg);

  return ok;
}

SUBOOL
suscan_analyzer_send_psd_from_smoothpsd(
    suscan_analyzer_t *self,
    const su_smoothpsd_t *smoothpsd,
    SUBOOL looped,
    SUSCOUNT history_size)
{
  struct suscan_analyzer_psd_msg *msg = NULL;
  SUBOOL ok = SU_FALSE;

  if ((msg = suscan_analyzer_psd_msg_new_from_data(
      suscan_analyzer_get_source_info(self)->source_samp_rate,
      su_smoothpsd_get_last_psd(smoothpsd),
      su_smoothpsd_get_fft_size(smoothpsd))) == NULL) {
    suscan_analyzer_send_status(
        self,
        SUSCAN_ANALYZER_MESSAGE_TYPE_INTERNAL,
        -1,
        "Cannot create message: %s",
        strerror(errno));
    goto done;
  }

  /* In wide spectrum mode, frequency is given by curr_freq */
  msg->fc = suscan_analyzer_get_source_info(self)->frequency;
  msg->measured_samp_rate = suscan_analyzer_get_measured_samp_rate(self);
  suscan_analyzer_get_source_time(self, &msg->timestamp);
  msg->looped = looped;
  msg->history_size = history_size;
  msg->N0 = 0;

  if (!suscan_mq_write(
      self->mq_out,
      SUSCAN_ANALYZER_MESSAGE_TYPE_PSD,
      msg)) {
    suscan_analyzer_send_status(
        self,
        SUSCAN_ANALYZER_MESSAGE_TYPE_INTERNAL,
        -1,
        "Cannot write message: %s",
        strerror(errno));
    goto done;
  }

  /* Message queued, forget about it */
  msg = NULL;

  ok = SU_TRUE;

done:
  if (msg != NULL)
    suscan_analyzer_dispose_message(SUSCAN_ANALYZER_MESSAGE_TYPE_PSD, msg);

  return ok;
}

SUBOOL
suscan_analyzer_message_has_expired(
    suscan_analyzer_t *self,
    void *msg,
    uint32_t type)
{
  struct suscan_analyzer_psd_msg *psd_msg = msg;
  struct suscan_analyzer_inspector_msg *insp_msg = msg;
  SUBOOL timely_msg = SU_FALSE;
  struct timeval rttime, now, diff;
  struct timeval max_delta = {
    SUSCAN_ANALYZER_EXPIRE_DELTA_MS / 1000,
    (SUSCAN_ANALYZER_EXPIRE_DELTA_MS % 1000) * 1000};
  SUBOOL expired = SU_FALSE;

  gettimeofday(&now, NULL);

  switch (type) {
    case SUSCAN_ANALYZER_MESSAGE_TYPE_PSD:
      rttime = psd_msg->rt_time;
      timely_msg = SU_TRUE;
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR:
      if (insp_msg->kind == SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SPECTRUM) {
        rttime = insp_msg->rt_time;
        timely_msg = SU_TRUE;
      }
      break;
  }

  if (timely_msg) {
    if (!self->have_impl_rt) {
      /* Dont' have implementation timestamp */
      timersub(&now, &rttime, &self->impl_rt_delta);
      self->have_impl_rt = SU_TRUE;
    } else {
      /* Calculate difference */
      timersub(&now, &rttime, &diff);

      /* Subtract the intrinsic time delta */
      timersub(&diff, &self->impl_rt_delta, &diff);

      if (timercmp(&diff, &max_delta, >))
        expired = SU_TRUE;
    }
  }

  return expired;
}
