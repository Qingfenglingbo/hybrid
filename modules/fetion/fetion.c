#include <glib.h>
#include "util.h"
#include "account.h"
#include "module.h"
#include "info.h"
#include "blist.h"
#include "notify.h"

#include "fetion.h"
#include "fx_trans.h"
#include "fx_login.h"
#include "fx_account.h"
#include "fx_group.h"
#include "fx_buddy.h"
#include "fx_msg.h"
#include "fx_util.h"

fetion_account *ac;

/**
 * Process "presence changed" message.
 */
static void
process_presence(fetion_account *ac, const gchar *sipmsg)
{
	GSList *list;
	GSList *pos;
	fetion_buddy *buddy;
	HybridBuddy *imbuddy;

	list = sip_parse_presence(ac, sipmsg);

	for (pos = list; pos; pos = pos->next) {

		buddy = (fetion_buddy*)pos->data;
		imbuddy = hybrid_blist_find_buddy(ac->account, buddy->userid);

		if (!buddy->localname || *(buddy->localname) == '\0') {
			hybrid_blist_set_buddy_name(imbuddy, buddy->nickname);
		}
		hybrid_blist_set_buddy_mood(imbuddy, buddy->mood_phrase);

		switch (buddy->state) {
			case P_ONLINE:
				hybrid_blist_set_buddy_state(imbuddy, HYBRID_STATE_ONLINE);
				break;
			case P_OFFLINE:
				hybrid_blist_set_buddy_state(imbuddy, HYBRID_STATE_OFFLINE);
				break;
			case P_INVISIBLE:
				hybrid_blist_set_buddy_state(imbuddy, HYBRID_STATE_OFFLINE);
				break;
			case P_AWAY:
				hybrid_blist_set_buddy_state(imbuddy, HYBRID_STATE_AWAY);
				break;
			case P_BUSY:
				hybrid_blist_set_buddy_state(imbuddy, HYBRID_STATE_BUSY);
				break;
			default:
				hybrid_blist_set_buddy_state(imbuddy, HYBRID_STATE_AWAY);
				break;
		}

		fetion_update_portrait(ac, buddy);
	}
}

/**
 * Process deregister message. When the same account logins
 * at somewhere else, this message will be received.
 */
static void
process_dereg_cb(fetion_account *ac, const gchar *sipmsg)
{
	hybrid_account_error_reason(ac->account, 
			_("Your account has logined elsewhere. You are forced to quit."));
}

/**
 * Process the user entered message. The fetion prococol dont allow us
 * to send messages to an online buddy who's conversation channel is not
 * ready, so before chating with an online buddy, we should first start
 * a new conversation channel, and invite the buddy to the conversation,
 * but when the channel is ready? yes, when we got the User-Entered 
 * message through the new channel established, the channel is ready!
 */
static void
process_enter_cb(fetion_account *ac, const gchar *sipmsg)
{
	GSList *pos;
	fetion_transaction *trans;

	g_return_if_fail(ac != NULL);
	g_return_if_fail(sipmsg != NULL);

	hybrid_debug_info("fetion", "user entered:\n%s", sipmsg);

	/* Set the channel's ready flag. */
	ac->channel_ready = TRUE;

	/* Check the transaction waiting list, wakeup the sleeping transaction,
	 * the transaction is either sending a SMS or sending a nudge, we got
	 * the information from the transaction context, and restore the transaction. */
	while (ac->trans_wait_list) {
		pos = ac->trans_wait_list;
		trans = (fetion_transaction*)pos->data;

		if (trans->msg && *(trans->msg) != '\0') {
			fetion_message_send(ac, trans->userid, trans->msg);
		}

		transaction_wakeup(ac, trans);

	}
}

/**
 * Process notification routine.
 */
static void
process_notify_cb(fetion_account *ac, const gchar *sipmsg)
{
	gint notify_type;
	gint event_type;

	sip_parse_notify(sipmsg, &notify_type, &event_type);

	if (notify_type == NOTIFICATION_TYPE_UNKNOWN ||
			event_type == NOTIFICATION_EVENT_UNKNOWN) {

		hybrid_debug_info("fetion", "recv unknown notification:\n%s", sipmsg);
		return;
	}

	switch (notify_type) {

		case NOTIFICATION_TYPE_PRESENCE:
			if (event_type == NOTIFICATION_EVENT_PRESENCECHANGED) {
				process_presence(ac, sipmsg);
			}
			break;

		case NOTIFICATION_TYPE_CONVERSATION :
			if (event_type == NOTIFICATION_EVENT_USERLEFT) {
			//	process_left_cb(ac, sipmsg);
				break;

			} else 	if (event_type == NOTIFICATION_EVENT_USERENTER) {
				process_enter_cb(ac, sipmsg);
				break;
			}
			break;

		case NOTIFICATION_TYPE_REGISTRATION :
			if (event_type == NOTIFICATION_EVENT_DEREGISTRATION) {
				process_dereg_cb(ac, sipmsg);
			}
			break;

		case NOTIFICATION_TYPE_SYNCUSERINFO :
			if (event_type == NOTIFICATION_EVENT_SYNCUSERINFO) {
			//	process_sync_info(ac, sipmsg);
			}
			break;

		case NOTIFICATION_TYPE_CONTACT :
			if (event_type == NOTIFICATION_EVENT_ADDBUDDYAPPLICATION) {
			//	process_add_buddy(ac, sipmsg);
			}
			break;
#if 0
		case NOTIFICATION_TYPE_PGGROUP :
			break;
#endif
		default:
			break;
	}
}

/**
 * Process the sip response message.
 */
static void
process_sipc_cb(fetion_account *ac, const gchar *sipmsg)
{
	gchar *callid;
	gint callid0;
	fetion_transaction *trans;
	GSList *trans_cur;

	if (!(callid = sip_header_get_attr(sipmsg, "I"))) {
		hybrid_debug_error("fetion", "invalid sipc message received\n%s",
				sipmsg);
		g_free(callid);
		return;
	}
	
	callid0 = atoi(callid);

	trans_cur = ac->trans_list;

	while(trans_cur) {
		trans = (fetion_transaction*)(trans_cur->data);

		if (trans->callid == callid0) {

			if (trans->callback) {
				(trans->callback)(ac, sipmsg, trans);
			}

			transaction_remove(ac, trans);

			break;
		}

		trans_cur = g_slist_next(trans_cur);
	}
}

/**
 * Process the message sip message.
 */
static void
process_message_cb(fetion_account *ac, const gchar *sipmsg)
{
	gchar *event;
	gchar *sysmsg_text;
	gchar *sysmsg_url;
	HybridNotify *notify;

	if ((event = sip_header_get_attr(sipmsg, "N")) &&
			g_strcmp0(event, "system-message") == 0) {
		if (fetion_message_parse_sysmsg(sipmsg, &sysmsg_text,
					&sysmsg_url) != HYBRID_OK) {
			return;
		}

		notify = hybrid_notify_create(ac->account, _("System Message"));
		hybrid_notify_set_text(notify, sysmsg_text);

		g_free(sysmsg_text);
		g_free(sysmsg_url);
	}

	hybrid_debug_info("fetion", "received message:\n%s", sipmsg);

	fetion_process_message(ac, sipmsg);

	g_free(event);
}

/**
 * Process the pushed message.
 */
void
process_pushed(fetion_account *ac, const gchar *sipmsg)
{
	gint type;
	
	type = fetion_sip_get_msg_type(sipmsg);

	switch (type) {
		case SIP_NOTIFICATION :	
			process_notify_cb(ac, sipmsg);
			break;
		case SIP_MESSAGE:
			process_message_cb(ac, sipmsg);
			break;
		case SIP_INVITATION:
			//process_invite_cb(ac, sipmsg);
			break;
		case SIP_INFO:
			//process_info_cb(ac, sipmsg);		
			break;
		case SIP_SIPC_4_0:
			process_sipc_cb(ac, sipmsg);	
			break;
		default:
			hybrid_debug_info("fetion", "recevie unknown msg:\n%s", sipmsg);
			break;
	}
}

static gboolean
fetion_login(HybridAccount *imac)
{
	HybridSslConnection *conn;

	hybrid_debug_info("fetion", "fetion is now logining...");

	ac = fetion_account_create(imac, imac->username, imac->password);

	hybrid_account_set_protocol_data(imac, ac);

	conn = hybrid_ssl_connect(SSI_SERVER, 443, ssi_auth_action, ac);

	return TRUE;
}

/**
 * Callback function for the get_info transaction.
 */
static gint 
get_info_cb(fetion_account *ac, const gchar *sipmsg, fetion_transaction *trans)
{
	HybridInfo *info;
	fetion_buddy *buddy;
	gchar *province;
	gchar *city;

	info = (HybridInfo*)trans->data;

	if (!(buddy = fetion_buddy_parse_info(ac, trans->userid, sipmsg))) {
		/* TODO show an error msg in the get-info box. */
		return HYBRID_ERROR;
	}

	province = buddy->province && *(buddy->province) != '\0' ?
				get_province_name(buddy->province) : g_strdup(_("Unknown"));

	city = buddy->city && *(buddy->city) != '\0' ?
				get_city_name(buddy->province, buddy->city) :
				g_strdup(_("Unknown"));

	hybrid_info_add_pair(info, _("Nickname"), buddy->nickname);
	hybrid_info_add_pair(info, _("Localname"), buddy->localname);
	hybrid_info_add_pair(info, _("Fetion-no"), buddy->sid);
	hybrid_info_add_pair(info, _("Mobile-no"), buddy->mobileno);
	hybrid_info_add_pair(info, _("Gender"), 
		buddy->gender == 1 ? _("Male") :
		(buddy->gender == 2 ? _("Female") : _("Secrecy")));
	hybrid_info_add_pair(info, _("Mood"), buddy->mood_phrase);

	hybrid_info_add_pair(info, _("Country"),
			g_strcmp0(buddy->country, "CN") == 0 ? "China" : buddy->country);
	hybrid_info_add_pair(info, _("Province"), province);
	hybrid_info_add_pair(info, _("City"), city);

	g_free(province);
	g_free(city);

	return HYBRID_OK;
}

static gboolean
fetion_change_state(HybridAccount *account, gint state)
{
	fetion_account *ac;
	gint fetion_state;

	ac = hybrid_account_get_protocol_data(account);

	switch(state) {
		case HYBRID_STATE_ONLINE:
			fetion_state = P_ONLINE;
			break;
		case HYBRID_STATE_AWAY:
			fetion_state = P_AWAY;
			break;
		case HYBRID_STATE_BUSY:
			fetion_state = P_BUSY;
			break;
		case HYBRID_STATE_INVISIBLE:
			fetion_state = P_INVISIBLE;
			break;
		default:
			fetion_state = P_ONLINE;
			break;
	}

	if (fetion_account_update_state(ac, fetion_state) != HYBRID_OK) {
		return FALSE;
	}

	return TRUE;
}

static gboolean
fetion_keep_alive(HybridAccount *account)
{
	fetion_account *ac;

	ac = hybrid_account_get_protocol_data(account);

	if (fetion_account_keep_alive(ac) != HYBRID_OK) {
		return FALSE;
	}

	return TRUE;
}

static gboolean 
fetion_buddy_move(HybridAccount *account, HybridBuddy *buddy,
		HybridGroup *new_group)
{
	fetion_account *ac;

	ac = hybrid_account_get_protocol_data(account);

	fetion_buddy_move_to(ac, buddy->id, new_group->id);
	return TRUE;
}

static void
fetion_get_info(HybridAccount *account, HybridBuddy *buddy)
{
	HybridInfo *info;
	fetion_account *ac;

	info = hybrid_info_create(buddy);

	ac = hybrid_account_get_protocol_data(account);

	fetion_buddy_get_info(ac, buddy->id, get_info_cb, info);
}

static gboolean
fetion_remove(HybridAccount *account, HybridBuddy *buddy)
{
	fetion_account *ac;

	ac = hybrid_account_get_protocol_data(account);

	if (fetion_buddy_remove(ac, buddy->id) != HYBRID_OK) {
		return FALSE;
	}

	return TRUE;
}

static gboolean
fetion_rename(HybridAccount *account, HybridBuddy *buddy, const gchar *text)
{
	fetion_account *ac;

	ac = hybrid_account_get_protocol_data(account);

	if (fetion_buddy_rename(ac, buddy->id, text) != HYBRID_OK) {
		return FALSE;
	}

	return TRUE;
}

static void
fetion_chat_send(HybridAccount *account, HybridBuddy *buddy, const gchar *text)
{
	fetion_account *ac;
	extern GSList *channel_list;
	GSList *pos;

	ac = hybrid_account_get_protocol_data(account);

	if (BUDDY_IS_OFFLINE(buddy) || BUDDY_IS_INVISIBLE(buddy)) {
		fetion_message_send(ac, buddy->id, text);

	} else {
		/*
		 * If the buddy's state is greater than 0, then we should 
		 * invite the buddy first to start a new socket channel,
		 * then we can send the message through the new channel.
		 * Now, we check whether a channel related to this buddy has
		 * been established, if so, just send the message through
		 * the existing one, otherwise, start a new channel.
		 */
		for (pos = channel_list; pos; pos = pos->next) {
			ac = (fetion_account*)pos->data;

			printf("###################333333333 %s, %s\n", ac->who, buddy->id);

			if (g_strcmp0(ac->who, buddy->id) == 0) {
				/* yes, we got one. */
				fetion_message_send(ac, ac->who, text);

				return;
			}
		}

		fetion_message_new_chat(ac, buddy->id, text);
	}
}

static void
fetion_close(HybridAccount *account)
{
	GSList *pos;
	fetion_account *ac;
	fetion_buddy *buddy;
	fetion_group *group;

	ac = hybrid_account_get_protocol_data(account);

	/* close the socket */
	hybrid_event_remove(ac->source);
	close(ac->sk);

	/* destroy the group list */
	while (ac->groups) {
		pos = ac->groups;
		group = (fetion_group*)pos->data;
		ac->groups = g_slist_remove(ac->groups, group);
		fetion_group_destroy(group);
	}

	/* destroy the buddy list */
	while (ac->buddies) {
		pos = ac->buddies;
		buddy = (fetion_buddy*)pos->data;
		ac->buddies = g_slist_remove(ac->buddies, buddy);
		fetion_buddy_destroy(buddy);
	}
}

HybridModuleInfo module_info = {
	"fetion",                     /**< name */
	"levin108",                   /**< author */
	N_("fetion client"),          /**< summary */
	/* description */
	N_("hybrid plugin implementing Fetion Protocol version 4"), 
	"http://basiccoder.com",      /**< homepage */
	"0","1",                      /**< major version, minor version */
	"fetion",                     /**< icon name */

	fetion_login,                 /**< login */
	fetion_get_info,              /**< get_info */
	fetion_change_state,          /**< change_state */
	fetion_keep_alive,            /**< keep_alive */
	fetion_buddy_move,            /**< buddy_move */
	fetion_remove,                /**< buddy_remove */
	fetion_rename,                /**< buddy_rename */
	fetion_chat_send,             /**< chat_send */
	fetion_close,                 /**< close */
};

void 
fetion_module_init(HybridModule *module)
{

}

HYBRID_MODULE_INIT(fetion_module_init, &module_info);
