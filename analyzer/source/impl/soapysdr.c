/*

  Copyright (C) 2023 Gonzalo José Carracedo Carballal

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

#include "soapysdr.h"
#include <analyzer/source.h>
#include <analyzer/device/spec.h>
#include <analyzer/device/properties.h>
#include <analyzer/device/facade.h>
#include <sys/time.h>

#ifdef _SU_SINGLE_PRECISION
#  define SUSCAN_SOAPY_SAMPFMT SOAPY_SDR_CF32
#else
#  define SUSCAN_SOAPY_SAMPFMT SOAPY_SDR_CF64
#endif

SUPRIVATE SoapySDRKwargs *
strmap_to_SoapySDRKwargs(const strmap_t *map)
{
  SoapySDRKwargs *args = NULL;
  strmap_iterator_t it;

  SU_ALLOCATE_FAIL(args, SoapySDRKwargs);
  
  it = strmap_begin(map);
  while (!strmap_iterator_end(&it)) {
    if (it.value != NULL)
      SU_TRYZ_FAIL(SoapySDRKwargs_set(args, it.name, it.value));

    strmap_iterator_advance(&it);
  }

  return args;

fail:
  if (args != NULL) {
    SoapySDRKwargs_clear(args);
    free(args);
  }

  return NULL;
}

SUPRIVATE SoapySDRArgInfo *
suscan_source_soapysdr_find_setting(
  const struct suscan_source_soapysdr *source,
  const char *name)
{
  size_t i;

  for (i = 0; i < source->settings_count; ++i) {
    if (strcmp(source->settings[i].key, name) == 0)
      return source->settings + i;
  }

  return NULL;
}

SUPRIVATE SoapySDRArgInfo *
suscan_source_soapysdr_find_stream_arg(
  const struct suscan_source_soapysdr *source,
  const char *name)
{
  size_t i;

  for (i = 0; i < source->stream_args_count; ++i) {
    if (strcmp(source->stream_args[i].key, name) == 0)
      return source->stream_args + i;
  }

  return NULL;
}

SUPRIVATE void
suscan_source_soapysdr_debug_clocks(struct suscan_source_soapysdr *self)
{
  size_t i = 0;
  char *ref_string = NULL;
  char *tmp = NULL;

  for (i = 0; i < self->clock_sources_count; ++i) {
    if (ref_string == NULL) {
      SU_TRY(tmp = strdup(self->clock_sources[i]));
    } else {
      SU_TRY(tmp = strbuild("%s, %s", ref_string, self->clock_sources[i]));
      free(ref_string);
    }

    ref_string = tmp;
  }

  if (ref_string ==  NULL)
    SU_INFO("Device does not external clock reference\n");
  else
    SU_INFO("Device supports the following clock references: %s\n", ref_string);
  
done:
  if (ref_string != NULL)
    free(ref_string);
}

SUPRIVATE SUBOOL
suscan_source_soapysdr_init_sdr(struct suscan_source_soapysdr *self)
{
  suscan_source_config_t *config = self->config;
  unsigned int i;
  char *antenna = NULL;
  const char *key, *desc, *val;
  strmap_t *all_params = NULL;
  SoapySDRArgInfo *arg;
  SUBOOL ok = SU_FALSE;

  SU_TRY(all_params = suscan_device_spec_get_all(config->device_spec));
  SU_TRY(self->sdr_args = strmap_to_SoapySDRKwargs(all_params));

  if ((self->sdr = SoapySDRDevice_make(self->sdr_args)) == NULL) {
    SU_ERROR("Failed to open SDR device: %s\n", SoapySDRDevice_lastError());
    goto done;
  }

  if (self->config->antenna != NULL)
    if (SoapySDRDevice_setAntenna(
        self->sdr,
        SOAPY_SDR_RX,
        config->channel,
        config->antenna) != 0) {
      SU_ERROR("Failed to set SDR antenna: %s\n", SoapySDRDevice_lastError());
      goto done;
    }

  /* Disable AGC to prevent eccentric receivers from ignoring gain settings */
  if (SoapySDRDevice_setGainMode(self->sdr, SOAPY_SDR_RX, 0, false) != 0) {
    SU_ERROR("Failed to disable AGC. This is most likely a driver issue.\n");
    goto done;
  }

  for (i = 0; i < config->gain_count; ++i)
    if (SoapySDRDevice_setGainElement(
        self->sdr,
        SOAPY_SDR_RX,
        config->channel,
        config->gain_list[i]->name,
        config->gain_list[i]->val) != 0)
      SU_WARNING(
          "Failed to set gain `%s' to %gdB, ignoring silently\n",
          config->gain_list[i]->name,
          config->gain_list[i]->val);


  if (SoapySDRDevice_setFrequency(
      self->sdr,
      SOAPY_SDR_RX,
      config->channel,
      config->freq - config->lnb_freq,
      NULL) != 0) {
    SU_ERROR("Failed to set SDR frequency: %s\n", SoapySDRDevice_lastError());
    goto done;
  }

  if (SoapySDRDevice_setSampleRate(
      self->sdr,
      SOAPY_SDR_RX,
      config->channel,
      config->samp_rate) != 0) {
    SU_ERROR("Failed to set sample rate: %s\n", SoapySDRDevice_lastError());
    goto done;
  }

  if (SoapySDRDevice_setBandwidth(
      self->sdr,
      SOAPY_SDR_RX,
      config->channel,
      config->bandwidth) != 0) {
    SU_ERROR("Failed to set SDR IF bandwidth: %s\n", SoapySDRDevice_lastError());
    goto done;
  }

  if (SoapySDRDevice_setClockSource(self->sdr, "external") != 0)
    SU_WARNING("Failed to switch to external clock\n");
  
#if SOAPY_SDR_API_VERSION >= 0x00060000
  if (SoapySDRDevice_setFrequencyCorrection(
      self->sdr,
      SOAPY_SDR_RX,
      config->channel,
      config->ppm) != 0) {
    SU_ERROR(
        "Failed to set SDR frequency correction: %s\n",
        SoapySDRDevice_lastError());
    goto done;
  }
#else
  SU_WARNING(
      "SoapySDR "
      SOAPY_SDR_ABI_VERSION
      " does not support frequency correction\n");
#endif /* SOAPY_SDR_API_VERSION >= 0x00060000 */

  /* TODO: Implement IQ balance*/
  self->have_dc = SoapySDRDevice_hasDCOffsetMode(
      self->sdr,
      SOAPY_SDR_RX,
      config->channel);

  if (self->have_dc) {
    if (SoapySDRDevice_setDCOffsetMode(
        self->sdr,
        SOAPY_SDR_RX,
        config->channel,
        config->dc_remove) != 0) {
      SU_ERROR(
          "Failed to set DC offset correction: %s\n",
          SoapySDRDevice_lastError());
      goto done;
    }
  }

  /* All set: open SoapySDR stream */
  self->chan_array[0] = config->channel;

  /* Set up stream arguments */
  self->stream_args = SoapySDRDevice_getStreamArgsInfo(
    self->sdr,
    SOAPY_SDR_RX,
    config->channel,
    &self->stream_args_count);

  if (self->stream_args_count != 0 && self->stream_args == NULL) {
    SU_ERROR(
        "Failed to retrieve stream argumentss: %s\n",
        SoapySDRDevice_lastError());
    goto done;
  }

  SoapySDRKwargs stream_args_to_set = {};
  for (i = 0; i < self->sdr_args->size; ++i) {
    if (strncmp(
      self->sdr_args->keys[i],
      SUSCAN_STREAM_SETTING_PREFIX,
      SUSCAN_STREAM_SETTING_PFXLEN) == 0) {

      key = self->sdr_args->keys[i] + SUSCAN_STREAM_SETTING_PFXLEN;
      arg = suscan_source_soapysdr_find_stream_arg(self, key);

      if (arg != NULL) {
        desc = arg->description == NULL ? arg->key : arg->description;
        SU_INFO(
          "Stream setting `%s': set to %s\n",
          desc,
          self->sdr_args->vals[i]);
      } else {
        SU_WARNING(
          "Stream setting `%s': not supported by device. Setting anyways.\n",
          key);
      }

      SoapySDRKwargs_set(&stream_args_to_set, key, self->sdr_args->vals[i]);
    }
  }

#if SOAPY_SDR_API_VERSION < 0x00080000
  if (SoapySDRDevice_setupStream(
      self->sdr,
      &self->rx_stream,
      SOAPY_SDR_RX,
      SUSCAN_SOAPY_SAMPFMT,
      self->chan_array,
      1,
      &stream_args_to_set) != 0) {
#else
  if ((self->rx_stream = SoapySDRDevice_setupStream(
      self->sdr,
      SOAPY_SDR_RX,
      SUSCAN_SOAPY_SAMPFMT,
      self->chan_array,
      1,
      &stream_args_to_set)) == NULL) {
#endif
    SU_ERROR(
        "Failed to open RX stream on SDR device: %s\n",
        SoapySDRDevice_lastError());
    goto done;
  }

  SoapySDRKwargs_clear(&stream_args_to_set);

  /* Set up device settings */
  self->settings = SoapySDRDevice_getSettingInfo(self->sdr, &self->settings_count);

  if (self->settings_count != 0 && self->settings == NULL) {
    SU_ERROR(
        "Failed to retrieve device settings: %s\n",
        SoapySDRDevice_lastError());
    goto done;
  }

  self->clock_sources = SoapySDRDevice_listClockSources(self->sdr, &self->clock_sources_count);

  if (self->clock_sources_count != 0) {
    if (self->clock_sources == NULL) {
      SU_ERROR(
        "Failed to retrieve clock source list: %s\n",
        SoapySDRDevice_lastError());
      goto done;
    }

    suscan_source_soapysdr_debug_clocks(self);
  }
  
  for (i = 0; i < self->sdr_args->size; ++i) {
    if (strncmp(
      self->sdr_args->keys[i],
      SUSCAN_SOURCE_SETTING_PREFIX,
      SUSCAN_SOURCE_SETTING_PFXLEN) == 0) {

      key = self->sdr_args->keys[i] + SUSCAN_SOURCE_SETTING_PFXLEN;
      arg = suscan_source_soapysdr_find_setting(self, key);

      if (arg != NULL) {
        desc = arg->description == NULL ? arg->key : arg->description;
        SU_INFO(
          "Device setting `%s': set to %s\n",
          desc,
          self->sdr_args->vals[i]);
      } else {
        SU_WARNING(
          "Device setting `%s': not supported by device. Setting anyways.\n",
          key);
      }

      SoapySDRDevice_writeSetting(
        self->sdr,
        key,
        self->sdr_args->vals[i]);
    } else if (strncmp(
      self->sdr_args->keys[i],
      SUSCAN_SOAPY_SETTING_PREFIX,
      SUSCAN_SOAPY_SETTING_PFXLEN) == 0) {
      key = self->sdr_args->keys[i] + SUSCAN_SOAPY_SETTING_PFXLEN;
      val = self->sdr_args->vals[i];

      if (strcmp(key, "clock") == 0) {
        if (SoapySDRDevice_setClockSource(self->sdr, val) != 0) {
          SU_ERROR(
            "Cannot set clock source to %s: %s\n",
            val,
            SoapySDRDevice_lastError());
          goto done;
        }
      } else {
        SU_ERROR("Unknown SoapySDR-specific tweak `%s'\n", key);
        goto done;
      }
    }
  }

  self->mtu = SoapySDRDevice_getStreamMTU(self->sdr, self->rx_stream);
  self->samp_rate = SoapySDRDevice_getSampleRate(self->sdr, SOAPY_SDR_RX, config->channel);

  if ((antenna = SoapySDRDevice_getAntenna(
    self->sdr,
    SOAPY_SDR_RX,
    config->channel)) != NULL) {
    (void) suscan_source_config_set_antenna(config, antenna);
    free(antenna);
  }

  ok = SU_TRUE;

done:
  if (all_params != NULL)
    SU_DISPOSE(strmap, all_params);
  
  return ok;
}

/****************************** Implementation ********************************/
SUPRIVATE void
suscan_source_soapysdr_close(void *ptr)
{
  struct suscan_source_soapysdr *self = (struct suscan_source_soapysdr *) ptr;

  if (self->rx_stream != NULL)
    SoapySDRDevice_closeStream(self->sdr, self->rx_stream);

  if (self->settings != NULL)
    SoapySDRArgInfoList_clear(self->settings, self->settings_count);

  if (self->clock_sources != NULL)
    SoapySDRStrings_clear(&self->clock_sources, self->clock_sources_count);

  if (self->stream_args != NULL)
    SoapySDRArgInfoList_clear(self->stream_args, self->stream_args_count);

  if (self->sdr_args != NULL) {
    SoapySDRKwargs_clear(self->sdr_args);
    free(self->sdr_args);
    self->sdr_args = NULL;
  }

  if (self->sdr != NULL)
    SoapySDRDevice_unmake(self->sdr);

  free(self);
}

SUPRIVATE SUBOOL
suscan_source_soapysdr_populate_device_specific(
  struct suscan_source_soapysdr *self,
  struct suscan_source_info *info)
{
  char **gain_list = NULL;
  SoapySDRRange range;
  size_t i, gain_count = 0;
  struct suscan_source_gain_info *ginfo = NULL;
  struct suscan_device_gain_desc desc;
  size_t range_count;
  SUFREQ freq_min = INFINITY;
  SUFREQ freq_max = -INFINITY;
  char *dup = NULL;
  char **antenna_list = NULL;
  size_t antenna_count = 0;
  SoapySDRRange *freqRanges = NULL;
  SUBOOL ok = SU_FALSE;

  /* Populate frequency limits */
  if ((freqRanges = SoapySDRDevice_getFrequencyRange(
      self->sdr,
      SOAPY_SDR_RX,
      0,
      &range_count)) != NULL) {
    for (i = 0; i < range_count; ++i) {
      if (freqRanges[i].minimum < freq_min)
        freq_min = freqRanges[i].minimum;
      if (freqRanges[i].maximum > freq_max)
        freq_max = freqRanges[i].maximum;
    }

    if (isinf(freq_min) || isinf(freq_max))
      freq_min = freq_max = info->frequency;
  } else {
    freq_min = freq_max = info->frequency;
  }

  info->freq_max = freq_max;
  info->freq_min = freq_min;

  /* Populate antenna list */
  if ((antenna_list = SoapySDRDevice_listAntennas(
          self->sdr,
          SOAPY_SDR_RX,
          0,
          &antenna_count)) != NULL) {
    for (i = 0; i < antenna_count; ++i) {
      SU_TRY(dup = strdup(antenna_list[i]));
      SU_TRYC(PTR_LIST_APPEND_CHECK(info->antenna, dup));
      dup = NULL;
    }
  }

  /* Populate gains */
  if ((gain_list = SoapySDRDevice_listGains(
          self->sdr,
          SOAPY_SDR_RX,
          0,
          &gain_count)) != NULL) {
    for (i = 0; i < gain_count; ++i) {
      range = SoapySDRDevice_getGainElementRange(
        self->sdr,
        SOAPY_SDR_RX,
        0,
        gain_list[i]);

      desc.name = gain_list[i];
      desc.min  = range.minimum;
      desc.max  = range.maximum;
      desc.step = range.step;
      
      desc.def = SoapySDRDevice_getGainElement(
          self->sdr,
          SOAPY_SDR_RX,
          0,
          gain_list[i]);
      
      SU_TRY(ginfo = suscan_source_gain_info_new(&desc, desc.def));
      SU_TRYC(PTR_LIST_APPEND_CHECK(info->gain, ginfo));
    }
  }

  ok = SU_TRUE;

done:
  SoapySDRStrings_clear(&gain_list, gain_count);
  SoapySDRStrings_clear(&antenna_list, antenna_count);

  if (freqRanges != NULL)
    free(freqRanges);

  if (dup != NULL)
    free(dup);

  return ok;
}

SUPRIVATE SUBOOL
suscan_source_soapysdr_populate_source_info(
  struct suscan_source_soapysdr *self,
  struct suscan_source_info *info,
  const suscan_source_config_t *config)
{
  SUBOOL ok = SU_FALSE;

  info->realtime = SU_TRUE;
  
  /* Adjust permissions */
  info->permissions = SUSCAN_ANALYZER_ALL_SDR_PERMISSIONS;
  if (!self->have_dc)
    info->permissions &= ~SUSCAN_ANALYZER_PERM_SET_DC_REMOVE;

  /* Set sample rate */
  info->source_samp_rate    = self->samp_rate;
  info->effective_samp_rate = self->samp_rate;
  info->measured_samp_rate  = self->samp_rate;

  /* Get current source time */
  gettimeofday(&info->source_time, NULL);
  gettimeofday(&info->source_start, NULL);

  SU_TRY(suscan_source_soapysdr_populate_device_specific(self, info));

  ok = SU_TRUE;

done:
  return ok;
}

SUPRIVATE void *
suscan_source_soapysdr_open(
  suscan_source_t *source,
  suscan_source_config_t *config,
  struct suscan_source_info *info)
{
  struct suscan_source_soapysdr *new = NULL;

  SU_ALLOCATE_FAIL(new, struct suscan_source_soapysdr);

  new->config = config;

  SU_TRY_FAIL(suscan_source_soapysdr_init_sdr(new));

  /* Initialize source info */
  suscan_source_soapysdr_populate_source_info(new, info, config);
  
  return new;

fail:
  if (new != NULL)
    suscan_source_soapysdr_close(new);

  return NULL;
}

SUPRIVATE SUBOOL
suscan_source_soapysdr_start(void *userdata)
{
  struct suscan_source_soapysdr *self = (struct suscan_source_soapysdr *) userdata;

  if (SoapySDRDevice_activateStream(
      self->sdr,
      self->rx_stream,
      0,
      0,
      0) != 0) {
    SU_ERROR("Failed to activate stream: %s\n", SoapySDRDevice_lastError());
    return SU_FALSE;
  }

  return SU_TRUE;
}

SUPRIVATE SUSDIFF
suscan_source_soapysdr_read(
  void *userdata,
  SUCOMPLEX *buf,
  SUSCOUNT max)
{
  struct suscan_source_soapysdr *self = (struct suscan_source_soapysdr *) userdata;
  int result;
  int flags = 0;
  long long timeNs = 0;
  SUBOOL retry;

  do {
    retry = SU_FALSE;
    if (self->force_eos)
      result = 0;
    else
      result = SoapySDRDevice_readStream(
          self->sdr,
          self->rx_stream,
          (void * const*) &buf,
          max,
          &flags,
          &timeNs,
          SUSCAN_SOURCE_DEFAULT_READ_TIMEOUT); /* Setting this to 0 caused extreme CPU usage in MacOS */

    if (result == SOAPY_SDR_TIMEOUT
        || result == SOAPY_SDR_OVERFLOW
        || result == SOAPY_SDR_UNDERFLOW) {
      /* We should use this statuses as quality indicators */
      retry = SU_TRUE;
    }
  } while (retry);

  if (result < 0) {
    SU_ERROR(
        "Failed to read samples from stream: %s (result %d)\n",
        SoapySDR_errToStr(result),
        result);
    return SU_BLOCK_PORT_READ_ERROR_ACQUIRE;
  }

  return result;
}

SUPRIVATE void
suscan_source_soapysdr_get_time(void *userdata, struct timeval *tv)
{
  gettimeofday(tv, NULL);
}


SUPRIVATE SUBOOL
suscan_source_soapysdr_cancel(void *userdata)
{
  struct suscan_source_soapysdr *self = (struct suscan_source_soapysdr *) userdata;

  self->force_eos = SU_TRUE;

  if (SoapySDRDevice_deactivateStream(
      self->sdr,
      self->rx_stream,
      0,
      0) != 0) {
    SU_ERROR("Failed to deactivate stream: %s\n", SoapySDRDevice_lastError());
    return SU_FALSE;
  }

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_source_soapysdr_set_frequency(void *userdata, SUFREQ freq)
{
  struct suscan_source_soapysdr *self = (struct suscan_source_soapysdr *) userdata;

  /* Set device frequency */
  if (SoapySDRDevice_setFrequency(
      self->sdr,
      SOAPY_SDR_RX,
      self->config->channel,
      freq,
      NULL) != 0) {
    SU_ERROR("Failed to set SDR frequency: %s\n", SoapySDRDevice_lastError());
    return SU_FALSE;
  }

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_source_soapysdr_set_gain(void *userdata, const char *name, SUFLOAT gain)
{
  struct suscan_source_soapysdr *self = (struct suscan_source_soapysdr *) userdata;

  /* Set gain element */
  if (SoapySDRDevice_setGainElement(
      self->sdr,
      SOAPY_SDR_RX,
      self->config->channel,
      name,
      gain) != 0) {
    SU_ERROR(
        "Failed to set SDR gain `%s': %s\n",
        name,
        SoapySDRDevice_lastError());
    return SU_FALSE;
  }

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_source_soapysdr_set_antenna(void *userdata, const char *name)
{
  struct suscan_source_soapysdr *self = (struct suscan_source_soapysdr *) userdata;

  /* Set antenna */
  if (SoapySDRDevice_setAntenna(
      self->sdr,
      SOAPY_SDR_RX,
      self->config->channel,
      name) != 0) {
    SU_ERROR(
        "Failed to set SDR antenna `%s': %s\n",
        name,
        SoapySDRDevice_lastError());
    return SU_FALSE;
  }

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_source_soapysdr_set_bandwidth(void *userdata, SUFLOAT bw)
{
  struct suscan_source_soapysdr *self = (struct suscan_source_soapysdr *) userdata;

  /* Set device bandwidth */
  if (SoapySDRDevice_setBandwidth(
      self->sdr,
      SOAPY_SDR_RX,
      self->config->channel,
      bw) != 0) {
    SU_ERROR(
        "Failed to set SDR bandwidth: %s\n",
        SoapySDRDevice_lastError());
    return SU_FALSE;
  }

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_source_soapysdr_set_ppm(void *userdata, SUFLOAT ppm)
{
  struct suscan_source_soapysdr *self = (struct suscan_source_soapysdr *) userdata;

  /* Set ppm correction */
#if SOAPY_SDR_API_VERSION >= 0x00060000
  if (SoapySDRDevice_setFrequencyCorrection(
      self->sdr,
      SOAPY_SDR_RX,
      self->config->channel,
      ppm) != 0) {
    SU_ERROR(
        "Failed to set SDR frequency correction: %s\n",
        SoapySDRDevice_lastError());
    return SU_FALSE;
  }
#else
  SU_WARNING(
      "SoapySDR "
      SOAPY_SDR_ABI_VERSION
      " does not support frequency correction\n");
#endif /* SOAPY_SDR_API_VERSION >= 0x00060000 */

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_source_soapysdr_set_dc_remove(void *userdata, SUBOOL remove)
{
  struct suscan_source_soapysdr *self = (struct suscan_source_soapysdr *) userdata;

  if (SoapySDRDevice_setDCOffsetMode(
      self->sdr,
      SOAPY_SDR_RX,
      self->config->channel,
      remove ? true : false)
      != 0) {
    SU_ERROR("Failed to set DC mode\n");
    return SU_FALSE;
  }

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_source_soapysdr_set_agc(void *userdata, SUBOOL set)
{
  struct suscan_source_soapysdr *self = (struct suscan_source_soapysdr *) userdata;

  if (SoapySDRDevice_setGainMode(
      self->sdr,
      SOAPY_SDR_RX,
      self->config->channel,
      set ? true : false)
      != 0) {
    SU_ERROR("Failed to set AGC\n");
    return SU_FALSE;
  }

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_source_soapysdr_get_freq_limits(
  const suscan_source_config_t *self,
  SUFREQ *min,
  SUFREQ *max)
{
  struct suscan_device_properties *prop = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRY(prop = suscan_device_spec_properties(self->device_spec));

  *min = prop->freq_min;
  *max = prop->freq_max;

  ok = SU_TRUE;

done:
  return ok;
}

SUPRIVATE struct suscan_source_interface g_soapysdr_source =
{
  .name            = "soapysdr",
  .analyzer        = "local",
  .desc            = "SoapySDR (ABI " SOAPY_SDR_ABI_VERSION ")",
  .realtime        = SU_TRUE,
  
  .open            = suscan_source_soapysdr_open,
  .close           = suscan_source_soapysdr_close,
  .start           = suscan_source_soapysdr_start,
  .cancel          = suscan_source_soapysdr_cancel,
  .read            = suscan_source_soapysdr_read,
  .set_frequency   = suscan_source_soapysdr_set_frequency,
  .set_gain        = suscan_source_soapysdr_set_gain,
  .set_antenna     = suscan_source_soapysdr_set_antenna,
  .set_bandwidth   = suscan_source_soapysdr_set_bandwidth,
  .set_ppm         = suscan_source_soapysdr_set_ppm,
  .set_dc_remove   = suscan_source_soapysdr_set_dc_remove,
  .set_agc         = suscan_source_soapysdr_set_agc,
  .get_time        = suscan_source_soapysdr_get_time,
  .get_freq_limits = suscan_source_soapysdr_get_freq_limits,

  /* Unset members */
  .seek           = NULL,
  .max_size       = NULL,
  .is_real_time   = NULL,
  .estimate_size  = NULL,
  .guess_metadata = NULL,
};

SUBOOL
suscan_source_register_soapysdr(void)
{
  SUBOOL ok = SU_FALSE;

  SU_TRY(suscan_source_register(&g_soapysdr_source));

  ok = SU_TRUE;

done:
  return ok;
}
