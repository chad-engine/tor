/* Copyright 2004 Roger Dingledine */
/* See LICENSE for licensing information */
/* $Id$ */

/**
 * \file rephist.c
 * \brief Basic history functionality for reputation module.
 **/

#include "or.h"

/** History of an OR-\>OR link. */
typedef struct link_history_t {
  /** When did we start tracking this list? */
  time_t since;
  /** How many times did extending from OR1 to OR2 succeeed? */
  unsigned long n_extend_ok;
  /** How many times did extending from OR1 to OR2 fail? */
  unsigned long n_extend_fail;
} link_history_t;

/** History of an OR. */
typedef struct or_history_t {
  /** When did we start tracking this OR? */
  time_t since;
  /** How many times did we successfully connect? */
  unsigned long n_conn_ok;
  /** How many times did we try to connect and fail?*/
  unsigned long n_conn_fail;
  /** How many seconds have we been connected to this OR before
   * 'up_since'? */
  unsigned long uptime;
  /** How many seconds have we been unable to connect to this OR before
   * 'down_since'? */
  unsigned long downtime;
  /** If nonzero, we have been connected since this time. */
  time_t up_since;
  /** If nonzero, we have been unable to connect since this time. */
  time_t down_since;
  /** Map from lowercased OR2 name to a link_history_t for the link
   * from this OR to OR2. */
  strmap_t *link_history_map;
} or_history_t;

/** Map from lowercased OR nickname to or_history_t. */
static strmap_t *history_map = NULL;

/** Return the or_history_t for the named OR, creating it if necessary.
 */
static or_history_t *get_or_history(const char* nickname)
{
  or_history_t *hist;
  hist = (or_history_t*) strmap_get(history_map, nickname);
  if (!hist) {
    hist = tor_malloc_zero(sizeof(or_history_t));
    hist->link_history_map = strmap_new();
    hist->since = time(NULL);
    strmap_set(history_map, nickname, hist);
  }
  return hist;
}

/** Return the link_history_t for the link from the first named OR to
 * the second, creating it if necessary.
 */
static link_history_t *get_link_history(const char *from_name,
                                        const char *to_name)
{
  or_history_t *orhist;
  link_history_t *lhist;
  orhist = get_or_history(from_name);
  lhist = (link_history_t*) strmap_get(orhist->link_history_map, to_name);
  if (!lhist) {
    lhist = tor_malloc_zero(sizeof(link_history_t));
    lhist->since = time(NULL);
    strmap_set(orhist->link_history_map, to_name, lhist);
  }
  return lhist;
}

/** Update an or_history_t object <b>hist</b> so that its uptime/downtime
 * count is up-to-date as of <b>when</b>.
 */
static void update_or_history(or_history_t *hist, time_t when)
{
  tor_assert(hist);
  if (hist->up_since) {
    tor_assert(!hist->down_since);
    hist->uptime += (when - hist->up_since);
    hist->up_since = when;
  } else if (hist->down_since) {
    hist->downtime += (when - hist->down_since);
    hist->down_since = when;
  }
}

/** Initialize the static data structures for tracking history.
 */
void rep_hist_init(void)
{
  history_map = strmap_new();
}

/** Remember that an attempt to connect to the OR <b>nickname</b> failed at
 * <b>when</b>.
 */
void rep_hist_note_connect_failed(const char* nickname, time_t when)
{
  or_history_t *hist;
  hist = get_or_history(nickname);
  ++hist->n_conn_fail;
  if (hist->up_since) {
    hist->uptime += (when - hist->up_since);
    hist->up_since = 0;
  }
  if (!hist->down_since)
    hist->down_since = when;
}

/** Remember that an attempt to connect to the OR <b>nickname</b> succeeded
 * at <b>when</b>.
 */
void rep_hist_note_connect_succeeded(const char* nickname, time_t when)
{
  or_history_t *hist;
  hist = get_or_history(nickname);
  ++hist->n_conn_ok;
  if (hist->down_since) {
    hist->downtime += (when - hist->down_since);
    hist->down_since = 0;
  }
  if (!hist->up_since)
    hist->up_since = when;
}

/** Remember that we intentionally closed our connection to the OR
 * <b>nickname</b> at <b>when</b>.
 */
void rep_hist_note_disconnect(const char* nickname, time_t when)
{
  or_history_t *hist;
  hist = get_or_history(nickname);
  ++hist->n_conn_ok;
  if (hist->up_since) {
    hist->uptime += (when - hist->up_since);
    hist->up_since = 0;
  }
}

/** Remember that our connection to the OR <b>nickname</b> had an error and
 * stopped working at <b>when</b>.
 */
void rep_hist_note_connection_died(const char* nickname, time_t when)
{
  or_history_t *hist;
  if(!nickname) {
    /* If conn has no nickname, it's either an OP, or it is an OR
     * which didn't complete its handshake (or did and was unapproved).
     * Ignore it.
     */
    return;
  }
  hist = get_or_history(nickname);
  if (hist->up_since) {
    hist->uptime += (when - hist->up_since);
    hist->up_since = 0;
  }
  if (!hist->down_since)
    hist->down_since = when;
}

/** Remember that we successfully extended from the OR <b>from_name</b> to
 * the OR <b>to_name</b>.
 */
void rep_hist_note_extend_succeeded(const char *from_name,
                                    const char *to_name)
{
  link_history_t *hist;
  /* log_fn(LOG_WARN, "EXTEND SUCCEEDED: %s->%s",from_name,to_name); */
  hist = get_link_history(from_name, to_name);
  ++hist->n_extend_ok;
}

/** Remember that we tried to extend from the OR <b>from_name</b> to the OR
 * <b>to_name</b>, but failed.
 */
void rep_hist_note_extend_failed(const char *from_name, const char *to_name)
{
  link_history_t *hist;
  /* log_fn(LOG_WARN, "EXTEND FAILED: %s->%s",from_name,to_name); */
  hist = get_link_history(from_name, to_name);
  ++hist->n_extend_fail;
}

/** Log all the reliability data we have rememberred, with the chosen
 * severity.
 */
void rep_hist_dump_stats(time_t now, int severity)
{
  strmap_iter_t *lhist_it;
  strmap_iter_t *orhist_it;
  const char *name1, *name2;
  or_history_t *or_history;
  link_history_t *link_history;
  void *or_history_p, *link_history_p;
  double uptime;
  char buffer[2048];
  int len;
  unsigned long upt, downt;

  log(severity, "--------------- Dumping history information:");

  for (orhist_it = strmap_iter_init(history_map); !strmap_iter_done(orhist_it);
       orhist_it = strmap_iter_next(history_map,orhist_it)) {
    strmap_iter_get(orhist_it, &name1, &or_history_p);
    or_history = (or_history_t*) or_history_p;

    update_or_history(or_history, now);
    upt = or_history->uptime;
    downt = or_history->downtime;
    if (upt+downt) {
      uptime = ((double)upt) / (upt+downt);
    } else {
      uptime=1.0;
    }
    log(severity,
        "OR %s: %ld/%ld good connections; uptime %ld/%ld sec (%.2f%%)",
        name1,
        or_history->n_conn_ok, or_history->n_conn_fail+or_history->n_conn_ok,
        upt, upt+downt, uptime*100.0);

    strcpy(buffer, "    Good extend attempts: ");
    len = strlen(buffer);
    for (lhist_it = strmap_iter_init(or_history->link_history_map);
         !strmap_iter_done(lhist_it);
         lhist_it = strmap_iter_next(or_history->link_history_map, lhist_it)) {
      strmap_iter_get(lhist_it, &name2, &link_history_p);
      link_history = (link_history_t*) link_history_p;
      len += snprintf(buffer+len, 2048-len, "%s(%ld/%ld); ", name2,
                      link_history->n_extend_ok,
                      link_history->n_extend_ok+link_history->n_extend_fail);
      if (len >= 2048) {
        buffer[2047]='\0';
        break;
      }
    }
    log(severity, buffer);
  }
}

/*
  Local Variables:
  mode:c
  indent-tabs-mode:nil
  c-basic-offset:2
  End:
*/
