/*
 *
  ***** BEGIN LICENSE BLOCK *****
 
  Copyright (C) 2009-2018 Olof Hagsand and Benny Holmgren

  This file is part of CLIXON.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  Alternatively, the contents of this file may be used under the terms of
  the GNU General Public License Version 3 or later (the "GPL"),
  in which case the provisions of the GPL are applicable instead
  of those above. If you wish to allow use of your version of this file only
  under the terms of the GPL, and not to allow others to
  use your version of this file under the terms of Apache License version 2, 
  indicate your decision by deleting the provisions above and replace them with
  the  notice and other provisions required by the GPL. If you do not delete
  the provisions above, a recipient may use your version of this file under
  the terms of any one of the Apache License version 2 or the GPL.

  ***** END LICENSE BLOCK *****

 *
 * Part of the external API to plugins. Applications should not include
 * this file directly (only via clicon_backend.h).
 * Internal code should include this
 */

#ifndef _CLIXON_BACKEND_HANDLE_H_
#define _CLIXON_BACKEND_HANDLE_H_

/*
 * Types
 */
/* Notification subscription info 
 * @see client_subscription in config_client.h
 */
struct handle_subscription{
    struct handle_subscription *hs_next;
    enum format_enum     hs_format; /*  format (enum format_enum) XXX not needed? */
    char                *hs_stream; /* name of notify stream */
    char                *hs_filter; /* filter, if format=xml: xpath, if text: fnmatch */
    subscription_fn_t    hs_fn;     /* Callback when event occurs */
    void                *hs_arg;    /* Callback argument */
};

/*
 * Prototypes
 */
/* Log for netconf notify function (config_client.c) */
int backend_notify(clicon_handle h, char *stream, int level, char *txt);
int backend_notify_xml(clicon_handle h, char *stream, int level, cxobj *x);


struct handle_subscription *subscription_add(clicon_handle h, char *stream, 
					     enum format_enum format, char *filter, 
					     subscription_fn_t fn, void *arg);

int subscription_delete(clicon_handle h, char *stream, 
			subscription_fn_t fn, void *arg);

struct handle_subscription *subscription_each(clicon_handle h,
				      struct handle_subscription *hprev);

#endif /* _CLIXON_BACKEND_HANDLE_H_ */
