/*
 * This file is the property of its developers.  See the COPYRIGHT file
 * for more details.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define PURPLE_PLUGINS

#include <glib.h>

#include "debug.h"
#include "plugin.h"
#include "pluginpref.h"
#include "prefs.h"
#include "util.h"
#include "version.h"

static void
rss_got_feed(PurpleUtilFetchUrlData *url_data, gpointer userdata, const gchar *url_text, gsize len, const gchar *error_message)
{
	PurpleConnection *pc = userdata;
	gchar *salvaged;
	time_t last_fetch_time;
	time_t time_of_message;
	time_t newest_message = 0;
	gchar *tmp;
	gchar month_string[4], weekday[4];
	guint year, month, day, hour, minute, second;
	long timezone;
	gchar *subject, *url;
	gchar *title_text;

	month_string[3] = weekday[3] = '\0';
	year = month = day = hour = minute = second = 0;

	if (!url_text || !len)
		return;

	last_fetch_time = purple_account_get_int(pc->account, "last_fetch", 0);

	salvaged = purple_utf8_salvage(url_text);
	xmlnode *rss_root = xmlnode_from_str(salvaged, -1);
	g_free(salvaged);
	if (rss_root == NULL)
	{
		purple_debug_error("rss", "Could not load RSS file\n");
		return;
	}
	xmlnode *channel = xmlnode_get_child(rss_root, "channel");
	if (channel == NULL)
	{
		purple_debug_warning("rss", "Invalid RSS feed\n");
		xmlnode_free(rss_root);
		return;
	}
	
	xmlnode *title = xmlnode_get_child(channel, "title");
	title_text = title ? xmlnode_get_data_unescaped(title) : g_strdup("");
	
	xmlnode *item = xmlnode_get_child(channel, "item");
	if (item == NULL)
	{
		purple_debug_info("facebook", "No new items\n");
	}
	for (; item != NULL; item = xmlnode_get_next_twin(item))
	{
		xmlnode *pubDate = xmlnode_get_child(item, "pubDate");
		if (!pubDate)
			continue;
		tmp = xmlnode_get_data_unescaped(pubDate);
		/* rss times are in Thu, 19 Jun 2008 15:51:25 -1100 format */
		sscanf(tmp, "%3s, %2u %3s %4u %2u:%2u:%2u %5ld", (char*)&weekday, &day, (char*)&month_string, &year, &hour, &minute, &second, &timezone);
		if (g_str_equal(month_string, "Jan")) month = 0;
		else if (g_str_equal(month_string, "Feb")) month = 1;
		else if (g_str_equal(month_string, "Mar")) month = 2;
		else if (g_str_equal(month_string, "Apr")) month = 3;
		else if (g_str_equal(month_string, "May")) month = 4;
		else if (g_str_equal(month_string, "Jun")) month = 5;
		else if (g_str_equal(month_string, "Jul")) month = 6;
		else if (g_str_equal(month_string, "Aug")) month = 7;
		else if (g_str_equal(month_string, "Sep")) month = 8;
		else if (g_str_equal(month_string, "Oct")) month = 9;
		else if (g_str_equal(month_string, "Nov")) month = 10;
		else if (g_str_equal(month_string, "Dec")) month = 11;
		g_free(tmp);

		/* try using pidgin's functions */
		tmp = g_strdup_printf("%04u%02u%02uT%02u%02u%02u%05ld", year, month, day, hour, minute, second, timezone);
		time_of_message = purple_str_to_time(tmp, FALSE, NULL, NULL, NULL);
		g_free(tmp);

		if (time_of_message <= 0)
		{
			/* there's no cross-platform, portable way of converting string to time
			   which doesn't need a new version of glib, so just cheat */
			time_of_message = second + 60*minute + 3600*hour + 86400*day + 2592000*month + 31536000*(year-1970);
		}

		if (time_of_message > newest_message)
		{
			/* we'll keep the newest message to save */
			newest_message = time_of_message;
		}

		if (time_of_message <= last_fetch_time)
		{
			/* fortunatly, rss messages are ordered from newest to oldest */
			/* so if this message is older than the last one, ignore rest */
			break;
		}
		
		xmlnode *link = xmlnode_get_child(item, "link");
		if (link)
		{
			url = xmlnode_get_data_unescaped(link);
		} else {
			url = g_strdup("");
		}
		
		xmlnode *title = xmlnode_get_child(item, "title");
		if (title)
		{
			subject = xmlnode_get_data_unescaped(title);
		} else {
			subject = g_strdup("");
		}
		
		purple_debug_info("rss", "subject '%s' title '%s' url '%s'\n", subject, title_text, url);
		
		purple_notify_email(pc, subject, title_text, pc->account->username, url, NULL, NULL);
		g_free(subject);
		g_free(url);
	}
	g_free(title_text);
	xmlnode_free(rss_root);

	if (newest_message > last_fetch_time)
	{
		/* update the last fetched time if we had newer messages */
		purple_account_set_int(pc->account, "last_fetch", newest_message);
	}
}

static gboolean
rss_get_feeds(gpointer userdata)
{
	PurpleConnection *pc;
	PurpleAccount *account;
	const gchar *feed_url;
	
	pc = userdata;
	account = purple_connection_get_account(pc);
	feed_url = purple_account_get_username(account);
	
	if (feed_url && *feed_url)
		purple_util_fetch_url(feed_url, TRUE, NULL, FALSE, rss_got_feed, pc);
	
	
	return TRUE;
}

static const gchar *
rss_list_icon(PurpleAccount *account, PurpleBuddy *buddy)
{
	return "rss";
}

static GHashTable *rss_account_table(PurpleAccount *account)
{
	GHashTable *table;

	table = g_hash_table_new(g_str_hash, g_str_equal);

	g_hash_table_insert(table, "login_label", (gpointer)("RSS URL..."));

	return table;
}

static void
rss_start(PurpleAccount *account)
{
	PurpleConnection *pc;
	guint fetch_timeout;
	
	pc = purple_account_get_connection(account);
	fetch_timeout = purple_timeout_add_seconds(60, rss_get_feeds, pc);
	rss_get_feeds(pc);
	
	purple_account_set_int(account, "fetch_timeout", fetch_timeout);
	
	purple_connection_set_state(pc, PURPLE_CONNECTED);
}

static void
rss_stop(PurpleConnection *pc)
{
	PurpleAccount *account;
	guint fetch_timeout;
	
	account = purple_connection_get_account(pc);
	fetch_timeout = purple_account_get_int(account, "fetch_timeout", 0);
	
	if (fetch_timeout)
		purple_timeout_remove(fetch_timeout);
}

static GList *
rss_status_types(PurpleAccount *account)
{
	GList *types = NULL;
	PurpleStatusType *status;

	status = purple_status_type_new_full(PURPLE_STATUS_AVAILABLE, NULL, "Online", TRUE, TRUE, FALSE);
	types = g_list_append(types, status);
	
	status = purple_status_type_new_full(PURPLE_STATUS_OFFLINE, NULL, "Offline", TRUE, TRUE, FALSE);
	types = g_list_append(types, status);
	
	return types;
}

static gboolean
plugin_load(PurplePlugin *plugin)
{
	return TRUE;
}

static gboolean
plugin_unload(PurplePlugin *plugin)
{
	return TRUE;
}

static void
plugin_init(PurplePlugin *plugin)
{
	PurplePluginInfo *info = plugin->info;
	PurplePluginProtocolInfo *prpl_info = info->extra_info;
	
	//prpl_info->get_account_text_table = rss_account_table;
	//prpl_info->struct_size = sizeof(PurplePluginProtocolInfo);
}

PurplePluginProtocolInfo prpl_info = {
	OPT_PROTO_NO_PASSWORD,
	NULL,                /* user_splits */
	NULL,                /* protocol_options */
	NO_BUDDY_ICONS,      /* icon_spec */
	rss_list_icon,       /* list_icon */
	NULL,                /* list_emblem */
	NULL,                /* status_text */
	NULL,                /* tooltip_text */
	rss_status_types,    /* status_types */
	NULL,                /* blist_node_menu */
	NULL,                /* chat_info */
	NULL,                /* chat_info_defaults */
	rss_start,           /* login */
	rss_stop,            /* close */
	NULL,                /* send_im */
	NULL,                /* set_info */
	NULL,                /* send_typing */
	NULL,                /* get_info */
	NULL,                /* set_status */
	NULL,                /* set_idle */
	NULL,                /* change_passwd */
	NULL,                /* add_buddy */
	NULL,                /* add_buddies */
	NULL,                /* remove_buddy */
	NULL,                /* remove_buddies */
	NULL,                /* add_permit */
	NULL,                /* add_deny */
	NULL,                /* rem_permit */
	NULL,                /* rem_deny */
	NULL,                /* set_permit_deny */
	NULL,                /* join_chat */
	NULL,                /* reject chat invite */
	NULL,                /* get_chat_name */
	NULL,                /* chat_invite */
	NULL,                /* chat_leave */
	NULL,                /* chat_whisper */
	NULL,                /* chat_send */
	NULL,                /* keepalive */
	NULL,                /* register_user */
	NULL,                /* get_cb_info */
	NULL,                /* get_cb_away */
	NULL,                /* alias_buddy */
	NULL,                /* group_buddy */
	NULL,                /* rename_group */
	NULL,                /* buddy_free */
	NULL,                /* convo_closed */
	NULL,                /* normalize */
	NULL,                /* set_buddy_icon */
	NULL,                /* remove_group */
	NULL,                /* get_cb_real_name */
	NULL,                /* set_chat_topic */
	NULL,				 /* find_blist_chat */
	NULL,                /* roomlist_get_list */
	NULL,                /* roomlist_cancel */
	NULL,                /* roomlist_expand_category */
	NULL,                /* can_receive_file */
	NULL,                /* send_file */
	NULL,                /* new_xfer */
	NULL,                /* offline_message */
	NULL,                /* whiteboard_prpl_ops */
	NULL,                /* send_raw */
	NULL,                /* roomlist_room_serialize */
	NULL,                /* unregister_user */
	NULL,                /* send_attention */
	NULL,                /* attention_types */
#if PURPLE_MAJOR_VERSION == 2 && PURPLE_MINOR_VERSION == 1
	(gpointer)
#endif
	sizeof(PurplePluginProtocolInfo), /* struct_size */
	rss_account_table,     /* get_account_text_table */
	NULL,                /* initiate_media */
	NULL,                /* can_do_media */
	NULL,                /* get_moods */
	NULL,                /* set_public_alias */
	NULL                 /* get_public_alias */
#if PURPLE_MAJOR_VERSION == 2 && PURPLE_MINOR_VERSION >= 8
,	NULL,                /* add_buddy_with_invite */
	NULL                 /* add_buddies_with_invite */
#endif
};

static PurplePluginInfo info = {
	PURPLE_PLUGIN_MAGIC,
/*	PURPLE_MAJOR_VERSION,
	PURPLE_MINOR_VERSION,
*/
	2, 1,
	PURPLE_PLUGIN_PROTOCOL, /* type */
	NULL, /* ui_requirement */
	0, /* flags */
	NULL, /* dependencies */
	PURPLE_PRIORITY_DEFAULT, /* priority */
	"prpl-eionrobb-rssreader", /* id */
	"RSS Feed", /* name */
	"1.0", /* version */
	"RSS Feed Reader", /* summary */
	"Displays RSS feeds in the Notifications area at the top of the buddy list", /* description */
	"Eion Robb <eion@robbmob.com>", /* author */
	"", /* homepage */
	plugin_load, /* load */
	plugin_unload, /* unload */
	NULL, /* destroy */
	NULL, /* ui_info */
	&prpl_info, /* extra_info */
	NULL, /* prefs_info */
	NULL, /* actions */
	NULL, /* padding */
	NULL,
	NULL,
	NULL
};

PURPLE_INIT_PLUGIN(rssreader, plugin_init, info);