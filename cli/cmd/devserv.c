/*

  Copyright (C) 2020 Gonzalo José Carracedo Carballal

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

#define SU_LOG_DOMAIN "cli-devserv"

#include <sys/types.h>
#include <sigutils/util/compat-socket.h>
#include <sigutils/util/compat-inet.h>
#include <sigutils/util/compat-in.h>
#include <sigutils/util/compat-time.h>
#include <util/confdb.h>
#include <sigutils/log.h>
#include <analyzer/analyzer.h>
#include <analyzer/version.h>
#include <analyzer/device/impl/multicast.h>
#include <string.h>
#include <pthread.h>

#include <cli/devserv/devserv.h>
#include <cli/cli.h>
#include <cli/cmds.h>

#define SUSCLI_DEVSERV_DEFAULT_PORT_BASE 28000
SUPRIVATE SUBOOL su_log_cr = SU_TRUE;

SUPRIVATE void
print_date(void)
{
  time_t t;
  struct tm tm;
  char mytime[50];

  time(&t);
  localtime_r(&t, &tm);

  strftime(mytime, sizeof(mytime), "%d %b %Y - %H:%M:%S", &tm);

  printf("%s", mytime);
}

SUPRIVATE void
su_log_func(void *private, const struct sigutils_log_message *msg)
{
  SUBOOL *cr = (SUBOOL *) private;
  SUBOOL is_except;
  size_t msglen;

  if (*cr) {
    switch (msg->severity) {
      case SU_LOG_SEVERITY_DEBUG:
        printf("\033[1;30m");
        print_date();
        printf(" - debug: ");
        break;

      case SU_LOG_SEVERITY_INFO:
        print_date();
        printf(" - ");
        break;

      case SU_LOG_SEVERITY_WARNING:
        print_date();
        printf(" - \033[1;33mwarning [%s]\033[0m: ", msg->domain);
        break;

      case SU_LOG_SEVERITY_ERROR:
        print_date();

        is_except = 
             strstr(msg->message, "exception in \"") != NULL
          || strstr(msg->message, "failed to create instance") != NULL;

        if (is_except)
          printf("\033[1;30m   ");
        else
          printf(" - \033[1;31merror   [%s]\033[0;1m: ", msg->domain);
        
        break;

      case SU_LOG_SEVERITY_CRITICAL:
        print_date();
        printf(
            " - \033[1;37;41mcritical[%s] in %s:%u\033[0m: ",
            msg->domain,
            msg->function,
            msg->line);
        break;
    }
  }

  msglen = strlen(msg->message);

  *cr = msg->message[msglen - 1] == '\n' || msg->message[msglen - 1] == '\r';

  fputs(msg->message, stdout);

  if (*cr)
    fputs("\033[0m", stdout);
}

/* Log config */
SUPRIVATE struct sigutils_log_config log_config =
{
  &su_log_cr, /* private */
  SU_TRUE, /* exclusive */
  su_log_func, /* log_func */
};

struct suscli_devserv_ctx {
  int fd;
  SUBOOL halting;
  uint16_t port_base;
  struct sockaddr_in mc_addr;

  union {
    struct suscan_device_net_discovery_pdu *pdu;
    void *alloc_buf;
  };

  PTR_LIST(suscli_analyzer_server_t, server);
  size_t alloc_size;
  size_t pdu_size;
};

SUPRIVATE void
suscli_devserv_ctx_destroy(
    struct suscli_devserv_ctx *self)
{
  unsigned int i;

  for (i = 0; i < self->server_count; ++i)
    suscli_analyzer_server_destroy(self->server_list[i]);

  if (self->server_list != NULL)
    free(self->server_list);

  if (self->fd != -1)
    close(self->fd);

  if (self->alloc_buf != NULL)
    free(self->alloc_buf);

  free(self);
}

struct suscan_device_net_discovery_pdu *
suscli_devserv_ctx_alloc_pdu(struct suscli_devserv_ctx *self, size_t size)
{
  void *tmp;

  if (size > self->alloc_size) {
    SU_TRYCATCH(tmp = realloc(self->alloc_buf, size), return SU_FALSE);
    self->alloc_buf = tmp;
    self->alloc_size = size;
  }

  self->pdu_size = size;

  return self->alloc_buf;
}

SUPRIVATE struct suscli_devserv_ctx *
suscli_devserv_ctx_new(
    const char *iface,
    const char *mcaddr,
    size_t compress_threshold)
{
  struct suscli_devserv_ctx *new = NULL;
  suscan_source_config_t *cfg;
  suscli_analyzer_server_t *server = NULL;
  struct suscli_analyzer_server_params params =
    suscli_analyzer_server_params_INITIALIZER;
  int i;
  char loopch = 0;
  struct in_addr mc_if;

  SU_TRYCATCH(
      new = calloc(1, sizeof(struct suscli_devserv_ctx)),
      goto fail);

  SU_TRYCATCH((new->fd = socket(AF_INET, SOCK_DGRAM, 0)) != -1, goto fail);

  new->port_base = SUSCLI_DEVSERV_DEFAULT_PORT_BASE;

  SU_TRYCATCH(
      setsockopt(
          new->fd,
          IPPROTO_IP,
          IP_MULTICAST_LOOP,
          (char *) &loopch,
          sizeof(loopch)) != -1,
      goto fail);

  mc_if.s_addr = suscan_ifdesc_to_addr(iface);

  /* Not necessary, but coherent. */
  if (ntohl(mc_if.s_addr) == 0xffffffff) {
    SU_ERROR(
        "Invalid network interface `%s'\n",
        iface);
    goto fail;
  }

  if ((ntohl(mc_if.s_addr) & 0xf0000000) == 0xe0000000) {
    SU_ERROR("Invalid interface address. Please note that if= expects the "
        "IP address of a configured local network interface, not a multicast "
        "group.\n");

    goto fail;
  }

  if (setsockopt(
          new->fd,
          IPPROTO_IP,
          IP_MULTICAST_IF,
          (char *) &mc_if,
          sizeof (struct in_addr)) == -1) {
    if (errno == EADDRNOTAVAIL) {
      SU_ERROR(
        "Invalid interface address. Please verify that there is a "
        "local network interface with IP `%s'\n",
        inet_ntoa(mc_if));
    } else {
      SU_ERROR(
          "failed to set network interface for multicast: %s\n",
          strerror(errno));
    }

    goto fail;
  }


  memset(&new->mc_addr, 0, sizeof(struct sockaddr_in));
  new->mc_addr.sin_family = AF_INET;
  new->mc_addr.sin_addr.s_addr = inet_addr(mcaddr);
  new->mc_addr.sin_port = htons(SURPC_DISCOVERY_PROTOCOL_PORT);

  params.compress_threshold = compress_threshold;
  params.ifname             = iface;

  /* Populate servers */
  for (i = 1; i <= suscli_get_source_count(); ++i) {
    cfg = suscli_get_source(i);

    if (cfg != NULL && !suscan_source_config_is_remote(cfg)) {
      params.profile = cfg;
      params.port    = new->port_base + i;
      SU_TRY_FAIL(server = suscli_analyzer_server_new_with_params(&params));
      SU_TRY_FAIL(suscli_analyzer_server_add_all_users(server));
      SU_TRYC_FAIL(PTR_LIST_APPEND_CHECK(new->server, server));
      SU_INFO(
          "  Port %d: server `%s'\n",
          params.port,
          suscan_source_config_get_label(cfg));

      server = NULL;
    }
  }

  return new;

fail:
  if (server != NULL)
    suscli_analyzer_server_destroy(server);

  if (new != NULL)
    suscli_devserv_ctx_destroy(new);

  return NULL;
}

SUPRIVATE void *
suscli_devserv_announce_thread(void *ptr)
{
  unsigned int i;
  struct suscli_devserv_ctx *ctx = (struct suscli_devserv_ctx *) ptr;
  suscan_device_spec_t *spec = NULL;
  grow_buf_t *pdu = NULL;
  PTR_LIST_LOCAL(grow_buf_t, pdu);
  strmap_t *traits = NULL;
  suscan_source_config_t *cfg = NULL;
 
  SU_MAKE(spec, suscan_device_spec);
  SU_TRY(suscan_device_spec_set_analyzer(spec, "remote"));
  
  SU_MAKE(traits, strmap);
  SU_TRY(strmap_set(traits, "host", inet_ntoa(ctx->mc_addr.sin_addr)));
  SU_TRY(strmap_set(traits, "transport", "tcp"));

  /* Compose announcement PDUs */
  for (i = 0; i < ctx->server_count; ++i) {
    SU_ALLOCATE(pdu, grow_buf_t);
    SU_TRY(cfg = suscan_source_config_clone(ctx->server_list[i]->config));
    
    SU_TRY(strmap_set_uint(traits, "port", ctx->server_list[i]->listen_port));
    
    SU_TRY(suscan_device_spec_set_traits(spec, traits));
    SU_TRY(suscan_source_config_set_device_spec(cfg, spec));

    SU_TRY(suscan_source_config_serialize(cfg, pdu));

    SU_TRYC(PTR_LIST_APPEND_CHECK(pdu, pdu));

    suscan_source_config_destroy(cfg);
    cfg = NULL;
  }

  SU_INFO("Announce server start: %d profiles\n", pdu_count);

  while (!ctx->halting) {
    for (i = 0; i < pdu_count; ++i) {
      if (sendto(
          ctx->fd,
          pdu_list[i]->buffer,
          pdu_list[i]->size,
          0,
          (struct sockaddr *) &ctx->mc_addr,
          sizeof (struct sockaddr_in)) != pdu_list[i]->size) {
        SU_ERROR("sendto() failed: %s\n", strerror(errno));
      }
    }

    sleep(1);
  }

done:
  if (cfg != NULL)
    suscan_source_config_destroy(cfg);

  if (pdu != NULL) {
    grow_buf_finalize(pdu);
    free(pdu);
  }

  for (i = 0; i < pdu_count; ++i) {
    grow_buf_finalize(pdu_list[i]);
    free(pdu_list[i]);
  }

  if (pdu_list != NULL)
    free(pdu_list);

  if (spec != NULL)
    SU_DISPOSE(suscan_device_spec, spec);

  if (traits != NULL)
    SU_DISPOSE(strmap, traits);
  
  return NULL;
}

SUBOOL
suscli_devserv_cb(const hashlist_t *params)
{
  struct suscli_devserv_ctx *ctx = NULL;
  const char *iface, *mc;
  int threshold = 0;

  pthread_t thread;
  SUBOOL thread_running = SU_FALSE;
  SUBOOL ok = SU_FALSE;

  su_log_init(&log_config);

  SU_TRYCATCH(
      suscli_param_read_string(params, "if", &iface, NULL),
      goto done);

  SU_TRYCATCH(
      suscli_param_read_int(
        params, 
        "compress_threshold", 
        &threshold, 
        0),
      goto done);

  if (iface == NULL) {
    fprintf(
        stderr,
        "devserv: need to specify a multicast interface address with if=\n");
    goto done;
  }

  SU_TRY(suscan_confdb_use("users"));

  if (!suscli_devserv_load_users()) {
    fprintf(stderr, "devserv: no default users found\n");
    fprintf(stderr, "\033[1mPlease note that default anonymous user support has been deprecated.\n");
    fprintf(stderr, "User lists must be defined in ~/.suscan/config/users.yaml explicitly\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "A good starting point (for testing purposes) is the following\n");
    fprintf(stderr, "user list, containing two users: a full-access root user and\n");
    fprintf(stderr, "a password-less view-only anonymous user. Save this list\n");
    fprintf(stderr, "as ~/.suscan/config/users.yaml and run suscli devserv again:\033[0m\n\n");
    fprintf(stderr, 
      "%%TAG ! tag:actinid.org,2022:suscan:\n"
      "---\n"
      "- !UserEntry\n"
      "  user: root\n"
      "  password: '\033[1;31mSetAGoodRootPasswordHere!123\033[0m'\n"
      "  default_access: allow\n"
      "\n"
      "- !UserEntry\n"
      "  user: anonymous\n"
      "  password:\n"
      "  default_access: deny\n"
      "  exceptions:\n"
      "    - inspector.open.audio\n\n");
    goto done;
  }

  SU_TRYCATCH(
      suscli_param_read_string(
          params,
          "group",
          &mc,
          SURPC_DISCOVERY_MULTICAST_ADDR),
      goto done);

  SU_INFO("Suscan device server %s\n", SUSCAN_VERSION_STRING);
  SU_INFO(
    "SuRPC protocol version: %d.%d\n",
    SUSCAN_REMOTE_PROTOCOL_MAJOR_VERSION,
    SUSCAN_REMOTE_PROTOCOL_MINOR_VERSION);

  SU_TRYCATCH(
      ctx = suscli_devserv_ctx_new(
        iface, 
        mc, 
        threshold),
      goto done);

  SU_TRYCATCH(
      pthread_create(&thread, NULL, suscli_devserv_announce_thread, ctx) != -1,
      goto done);
  thread_running = SU_TRUE;

  for (;;)
    sleep(1);

  ok = SU_TRUE;

done:
  if (thread_running) {
    ctx->halting = SU_TRUE;
    pthread_join(thread, NULL);
  }

  if (ctx != NULL)
    suscli_devserv_ctx_destroy(ctx);

  return ok;
}
