/*
 * Copyright © 2013-2016 Quentin “Sardem FF7” Glidic
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the copyright holders not be used in
 * advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  The copyright holders make
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <wayland-util.h>
#include <wlc/wlc.h>
#include <wlc/wlc-wayland.h>
#include "notification-area-unstable-v1-server-protocol.h"

#include "wlc-notification-area.h"

struct wlc_notification_area {
    struct wl_global *global;
    struct wl_resource *binding;
    struct wl_list notifications;
    wlc_handle output;
};

struct wlc_notification_area_notification {
    struct wlc_notification_area *na;
    struct wl_list link;
    wlc_handle view;
};

static void
_wlc_notification_area_request_destroy(struct wl_client *client, struct wl_resource *resource)
{
    (void) client;
    wl_resource_destroy(resource);
}

static void
_wlc_notification_area_notification_set_output(struct wlc_notification_area_notification *notification, wlc_handle output)
{
    if ( output == 0 )
    {
        wlc_view_set_mask(notification->view, 0);
        return;
    }

    wlc_view_set_output(notification->view, output);
    wlc_view_set_mask(notification->view, wlc_output_get_mask(output));
    wlc_view_bring_to_front(notification->view);
}

static void
_wlc_notification_area_notification_request_move(struct wl_client *client, struct wl_resource *resource, int32_t x, int32_t y)
{
    (void) client;
    struct wlc_notification_area_notification *notification = wl_resource_get_user_data(resource);
    struct wlc_geometry geometry;

    geometry = *wlc_view_get_geometry(notification->view);
    geometry.origin.x = x;
    geometry.origin.y = y;
    wlc_view_set_geometry(notification->view, WLC_RESIZE_EDGE_NONE, &geometry);
    if ( wlc_view_get_mask(notification->view) == 0 )
        _wlc_notification_area_notification_set_output(notification, notification->na->output);
}

static const struct zwna_notification_v1_interface wlc_notification_area_notification_implementation = {
    .destroy = _wlc_notification_area_request_destroy,
    .move = _wlc_notification_area_notification_request_move,
};

static void
_wlc_notification_area_destroy(struct wl_client *client, struct wl_resource *resource)
{
    (void) client;
    wl_resource_destroy(resource);
}

static void
_wlc_notification_area_create_notification(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *surface_resource)
{
    struct wlc_notification_area *self = wl_resource_get_user_data(resource);

    struct wlc_notification_area_notification *notification;
    notification = calloc(1, sizeof(struct wlc_notification_area_notification));
    if ( notification == NULL )
    {
        wl_resource_post_no_memory(surface_resource);
        return;
    }
    notification->na = self;
    wl_list_insert(&self->notifications, &notification->link);

    notification->view = wlc_view_from_surface(wlc_resource_from_wl_surface_resource(surface_resource), client, &zwna_notification_v1_interface, &wlc_notification_area_notification_implementation, 1, id, notification);
    if ( notification->view == 0 )
        return;
    wlc_view_set_type(notification->view, WLC_BIT_OVERRIDE_REDIRECT, true);
    wlc_view_set_type(notification->view, WLC_BIT_UNMANAGED, true);
    wlc_view_set_mask(notification->view, 0);
}

static const struct zwna_notification_area_v1_interface wlc_notification_area_implementation = {
    .destroy = _wlc_notification_area_destroy,
    .create_notification = _wlc_notification_area_create_notification,
};

static void
_wlc_notification_area_unbind(struct wl_resource *resource)
{
    struct wlc_notification_area *self = resource->data;

    self->binding = NULL;
}

static void
_wlc_notification_area_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    struct wlc_notification_area *self = data;
    struct wl_resource *resource;

    resource = wl_resource_create(client, &zwna_notification_area_v1_interface, version, id);
    wl_resource_set_implementation(resource, &wlc_notification_area_implementation, self, _wlc_notification_area_unbind);

    if ( self->binding != NULL )
    {
        wl_resource_post_error(resource, ZWNA_NOTIFICATION_AREA_V1_ERROR_BOUND, "interface object already bound");
        wl_resource_destroy(resource);
        return;
    }

    self->binding = resource;

    const struct wlc_size *size;
    if ( self->output == 0 )
        size = &wlc_size_zero;
    else
        size = wlc_output_get_resolution(self->output);
    zwna_notification_area_v1_send_geometry(self->binding, size->w, size->h);
}

struct wlc_notification_area *
wlc_notification_area_init(void)
{
    struct wlc_notification_area *self;

    self = calloc(1, sizeof(struct wlc_notification_area));

    wl_list_init(&self->notifications);

    if ( ( self->global = wl_global_create(wlc_get_wl_display(), &zwna_notification_area_v1_interface, 1, self, _wlc_notification_area_bind) ) == NULL )
    {
        free(self);
        return NULL;
    }
    wl_global_destroy(self->global);
    if ( ( self->global = wl_global_create(wlc_get_wl_display(), &zwna_notification_area_v1_interface, 1, self, _wlc_notification_area_bind) ) == NULL )
    {
        free(self);
        return NULL;
    }

    return self;
}

static void
_wlc_notification_area_view_destroy_internal(struct wlc_notification_area_notification *self)
{
    //wlc_view_close(notification->view);

    wl_list_remove(&self->link);

    free(self);
}

void
wlc_notification_area_uninit(struct wlc_notification_area *self)
{
    struct wlc_notification_area_notification *notification, *tmp;
    wl_list_for_each_safe(notification, tmp, &self->notifications, link)
        _wlc_notification_area_view_destroy_internal(notification);
    wl_global_destroy(self->global);

    free(self);
}

wlc_handle
wlc_notification_area_get_output(struct wlc_notification_area *self)
{
    return self->output;
}

void
wlc_notification_area_set_output(struct wlc_notification_area *self, wlc_handle output)
{
    if ( self->binding != NULL )
    {
        const struct wlc_size *size;
        if ( output == 0 )
            size = &wlc_size_zero;
        else
            size = wlc_output_get_resolution(output);
        zwna_notification_area_v1_send_geometry(self->binding, size->w, size->h);
    }

    if ( self->output == output )
        return;
    self->output = output;

    struct wlc_notification_area_notification *notification;
    wl_list_for_each(notification, &self->notifications, link)
    {
        if ( wlc_view_get_mask(notification->view) == 0 )
            continue;

        _wlc_notification_area_notification_set_output(notification, output);
    }
}

void
wlc_notification_area_view_destroy(struct wlc_notification_area *self, wlc_handle view)
{
    struct wlc_notification_area_notification *notification, *tmp;
    wl_list_for_each_safe(notification, tmp, &self->notifications, link)
    {
        if ( notification->view != view )
            continue;

        _wlc_notification_area_view_destroy_internal(notification);
        return;
    }
}
