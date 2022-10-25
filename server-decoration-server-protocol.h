/* Generated by wayland-scanner 1.20.0 */

#ifndef SERVER_DECORATION_SERVER_PROTOCOL_H
#define SERVER_DECORATION_SERVER_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include "wayland-server.h"

#ifdef  __cplusplus
extern "C" {
#endif

struct wl_client;
struct wl_resource;

/**
 * @page page_server_decoration The server_decoration protocol
 * @section page_ifaces_server_decoration Interfaces
 * - @subpage page_iface_org_kde_kwin_server_decoration_manager - Server side window decoration manager
 * - @subpage page_iface_org_kde_kwin_server_decoration - 
 * @section page_copyright_server_decoration Copyright
 * <pre>
 *
 * SPDX-FileCopyrightText: 2015 Martin Gräßlin
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * </pre>
 */
struct org_kde_kwin_server_decoration;
struct org_kde_kwin_server_decoration_manager;
struct wl_surface;

#ifndef ORG_KDE_KWIN_SERVER_DECORATION_MANAGER_INTERFACE
#define ORG_KDE_KWIN_SERVER_DECORATION_MANAGER_INTERFACE
/**
 * @page page_iface_org_kde_kwin_server_decoration_manager org_kde_kwin_server_decoration_manager
 * @section page_iface_org_kde_kwin_server_decoration_manager_desc Description
 *
 * This interface allows to coordinate whether the server should create
 * a server-side window decoration around a wl_surface representing a
 * shell surface (wl_shell_surface or similar). By announcing support
 * for this interface the server indicates that it supports server
 * side decorations.
 *
 * Use in conjunction with zxdg_decoration_manager_v1 is undefined.
 * @section page_iface_org_kde_kwin_server_decoration_manager_api API
 * See @ref iface_org_kde_kwin_server_decoration_manager.
 */
/**
 * @defgroup iface_org_kde_kwin_server_decoration_manager The org_kde_kwin_server_decoration_manager interface
 *
 * This interface allows to coordinate whether the server should create
 * a server-side window decoration around a wl_surface representing a
 * shell surface (wl_shell_surface or similar). By announcing support
 * for this interface the server indicates that it supports server
 * side decorations.
 *
 * Use in conjunction with zxdg_decoration_manager_v1 is undefined.
 */
extern const struct wl_interface org_kde_kwin_server_decoration_manager_interface;
#endif
#ifndef ORG_KDE_KWIN_SERVER_DECORATION_INTERFACE
#define ORG_KDE_KWIN_SERVER_DECORATION_INTERFACE
/**
 * @page page_iface_org_kde_kwin_server_decoration org_kde_kwin_server_decoration
 * @section page_iface_org_kde_kwin_server_decoration_api API
 * See @ref iface_org_kde_kwin_server_decoration.
 */
/**
 * @defgroup iface_org_kde_kwin_server_decoration The org_kde_kwin_server_decoration interface
 */
extern const struct wl_interface org_kde_kwin_server_decoration_interface;
#endif

#ifndef ORG_KDE_KWIN_SERVER_DECORATION_MANAGER_MODE_ENUM
#define ORG_KDE_KWIN_SERVER_DECORATION_MANAGER_MODE_ENUM
/**
 * @ingroup iface_org_kde_kwin_server_decoration_manager
 * Possible values to use in request_mode and the event mode.
 */
enum org_kde_kwin_server_decoration_manager_mode {
	/**
	 * Undecorated: The surface is not decorated at all, neither server nor client-side. An example is a popup surface which should not be decorated.
	 */
	ORG_KDE_KWIN_SERVER_DECORATION_MANAGER_MODE_NONE = 0,
	/**
	 * Client-side decoration: The decoration is part of the surface and the client.
	 */
	ORG_KDE_KWIN_SERVER_DECORATION_MANAGER_MODE_CLIENT = 1,
	/**
	 * Server-side decoration: The server embeds the surface into a decoration frame.
	 */
	ORG_KDE_KWIN_SERVER_DECORATION_MANAGER_MODE_SERVER = 2,
};
#endif /* ORG_KDE_KWIN_SERVER_DECORATION_MANAGER_MODE_ENUM */

/**
 * @ingroup iface_org_kde_kwin_server_decoration_manager
 * @struct org_kde_kwin_server_decoration_manager_interface
 */
struct org_kde_kwin_server_decoration_manager_interface {
	/**
	 * Create a server-side decoration object for a given surface
	 *
	 * When a client creates a server-side decoration object it
	 * indicates that it supports the protocol. The client is supposed
	 * to tell the server whether it wants server-side decorations or
	 * will provide client-side decorations.
	 *
	 * If the client does not create a server-side decoration object
	 * for a surface the server interprets this as lack of support for
	 * this protocol and considers it as client-side decorated.
	 * Nevertheless a client-side decorated surface should use this
	 * protocol to indicate to the server that it does not want a
	 * server-side deco.
	 */
	void (*create)(struct wl_client *client,
		       struct wl_resource *resource,
		       uint32_t id,
		       struct wl_resource *surface);
};

#define ORG_KDE_KWIN_SERVER_DECORATION_MANAGER_DEFAULT_MODE 0

/**
 * @ingroup iface_org_kde_kwin_server_decoration_manager
 */
#define ORG_KDE_KWIN_SERVER_DECORATION_MANAGER_DEFAULT_MODE_SINCE_VERSION 1

/**
 * @ingroup iface_org_kde_kwin_server_decoration_manager
 */
#define ORG_KDE_KWIN_SERVER_DECORATION_MANAGER_CREATE_SINCE_VERSION 1

/**
 * @ingroup iface_org_kde_kwin_server_decoration_manager
 * Sends an default_mode event to the client owning the resource.
 * @param resource_ The client's resource
 * @param mode The default decoration mode applied to newly created server decorations.
 */
static inline void
org_kde_kwin_server_decoration_manager_send_default_mode(struct wl_resource *resource_, uint32_t mode)
{
	wl_resource_post_event(resource_, ORG_KDE_KWIN_SERVER_DECORATION_MANAGER_DEFAULT_MODE, mode);
}

#ifndef ORG_KDE_KWIN_SERVER_DECORATION_MODE_ENUM
#define ORG_KDE_KWIN_SERVER_DECORATION_MODE_ENUM
/**
 * @ingroup iface_org_kde_kwin_server_decoration
 * Possible values to use in request_mode and the event mode.
 */
enum org_kde_kwin_server_decoration_mode {
	/**
	 * Undecorated: The surface is not decorated at all, neither server nor client-side. An example is a popup surface which should not be decorated.
	 */
	ORG_KDE_KWIN_SERVER_DECORATION_MODE_NONE = 0,
	/**
	 * Client-side decoration: The decoration is part of the surface and the client.
	 */
	ORG_KDE_KWIN_SERVER_DECORATION_MODE_CLIENT = 1,
	/**
	 * Server-side decoration: The server embeds the surface into a decoration frame.
	 */
	ORG_KDE_KWIN_SERVER_DECORATION_MODE_SERVER = 2,
};
#endif /* ORG_KDE_KWIN_SERVER_DECORATION_MODE_ENUM */

/**
 * @ingroup iface_org_kde_kwin_server_decoration
 * @struct org_kde_kwin_server_decoration_interface
 */
struct org_kde_kwin_server_decoration_interface {
	/**
	 * release the server decoration object
	 *
	 * 
	 */
	void (*release)(struct wl_client *client,
			struct wl_resource *resource);
	/**
	 * The decoration mode the surface wants to use.
	 *
	 * 
	 * @param mode The mode this surface wants to use.
	 */
	void (*request_mode)(struct wl_client *client,
			     struct wl_resource *resource,
			     uint32_t mode);
};

#define ORG_KDE_KWIN_SERVER_DECORATION_MODE 0

/**
 * @ingroup iface_org_kde_kwin_server_decoration
 */
#define ORG_KDE_KWIN_SERVER_DECORATION_MODE_SINCE_VERSION 1

/**
 * @ingroup iface_org_kde_kwin_server_decoration
 */
#define ORG_KDE_KWIN_SERVER_DECORATION_RELEASE_SINCE_VERSION 1
/**
 * @ingroup iface_org_kde_kwin_server_decoration
 */
#define ORG_KDE_KWIN_SERVER_DECORATION_REQUEST_MODE_SINCE_VERSION 1

/**
 * @ingroup iface_org_kde_kwin_server_decoration
 * Sends an mode event to the client owning the resource.
 * @param resource_ The client's resource
 * @param mode The decoration mode applied to the surface by the server.
 */
static inline void
org_kde_kwin_server_decoration_send_mode(struct wl_resource *resource_, uint32_t mode)
{
	wl_resource_post_event(resource_, ORG_KDE_KWIN_SERVER_DECORATION_MODE, mode);
}

#ifdef  __cplusplus
}
#endif

#endif
