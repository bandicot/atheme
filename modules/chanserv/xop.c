/*
 * Copyright (c) 2005-2007 Atheme Development Group
 * Rights to this code are as documented in doc/LICENSE.
 *
 * This file contains code for the CService XOP functions.
 *
 * $Id: xop.c 8309 2007-05-22 20:37:44Z nenolod $
 */

#include "atheme.h"
#include "template.h"

DECLARE_MODULE_V1
(
	"chanserv/xop", false, _modinit, _moddeinit,
	"$Id: xop.c 8309 2007-05-22 20:37:44Z nenolod $",
	"Atheme Development Group <http://www.atheme.org>"
);

/* the individual command stuff, now that we've reworked, hardcode ;) --w00t */
static void cs_xop_do_add(sourceinfo_t *si, mychan_t *mc, myuser_t *mu, char *target, unsigned int level, const char *leveldesc, unsigned int restrictflags);
static void cs_xop_do_del(sourceinfo_t *si, mychan_t *mc, myuser_t *mu, char *target, unsigned int level, const char *leveldesc);
static void cs_xop_do_list(sourceinfo_t *si, mychan_t *mc, unsigned int level, const char *leveldesc, int operoverride);

static void cs_cmd_sop(sourceinfo_t *si, int parc, char *parv[]);
static void cs_cmd_aop(sourceinfo_t *si, int parc, char *parv[]);
static void cs_cmd_hop(sourceinfo_t *si, int parc, char *parv[]);
static void cs_cmd_vop(sourceinfo_t *si, int parc, char *parv[]);
static void cs_cmd_forcexop(sourceinfo_t *si, int parc, char *parv[]);

command_t cs_sop = { "SOP", N_("Manipulates a channel SOP list."),
                        AC_NONE, 3, cs_cmd_sop };
command_t cs_aop = { "AOP", N_("Manipulates a channel AOP list."),
                        AC_NONE, 3, cs_cmd_aop };
command_t cs_hop = { "HOP", N_("Manipulates a channel HOP list."),
			AC_NONE, 3, cs_cmd_hop };
command_t cs_vop = { "VOP", N_("Manipulates a channel VOP list."),
                        AC_NONE, 3, cs_cmd_vop };
command_t cs_forcexop = { "FORCEXOP", N_("Forces access levels to xOP levels."),
                         AC_NONE, 1, cs_cmd_forcexop };

list_t *cs_cmdtree, *cs_helptree;

void _modinit(module_t *m)
{
	MODULE_USE_SYMBOL(cs_cmdtree, "chanserv/main", "cs_cmdtree");
	MODULE_USE_SYMBOL(cs_helptree, "chanserv/main", "cs_helptree");

        command_add(&cs_aop, cs_cmdtree);
        command_add(&cs_sop, cs_cmdtree);
	if (ircd != NULL && ircd->uses_halfops)
		command_add(&cs_hop, cs_cmdtree);
        command_add(&cs_vop, cs_cmdtree);
	command_add(&cs_forcexop, cs_cmdtree);

	help_addentry(cs_helptree, "SOP", "help/cservice/xop", NULL);
	help_addentry(cs_helptree, "AOP", "help/cservice/xop", NULL);
	help_addentry(cs_helptree, "VOP", "help/cservice/xop", NULL);
	if (ircd != NULL && ircd->uses_halfops)
		help_addentry(cs_helptree, "HOP", "help/cservice/xop", NULL);
	help_addentry(cs_helptree, "FORCEXOP", "help/cservice/forcexop", NULL);
}

void _moddeinit()
{
	command_delete(&cs_aop, cs_cmdtree);
	command_delete(&cs_sop, cs_cmdtree);
	command_delete(&cs_hop, cs_cmdtree);
	command_delete(&cs_vop, cs_cmdtree);
	command_delete(&cs_forcexop, cs_cmdtree);

	help_delentry(cs_helptree, "SOP");
	help_delentry(cs_helptree, "AOP");
	help_delentry(cs_helptree, "VOP");
	help_delentry(cs_helptree, "HOP");
	help_delentry(cs_helptree, "FORCEXOP");
}

static void cs_xop(sourceinfo_t *si, int parc, char *parv[], const char *leveldesc)
{
	myuser_t *mu;
	mychan_t *mc;
	int operoverride = 0;
	unsigned int restrictflags;
	const char *chan = parv[0];
	const char *cmd = parv[1];
	char *uname = parv[2];
	unsigned int level;

	if (!cmd || !chan)
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "xOP");
		command_fail(si, fault_needmoreparams, _("Syntax: SOP|AOP|HOP|VOP <#channel> ADD|DEL|LIST <nickname>"));
		return;
	}

	if ((strcasecmp("LIST", cmd)) && (!uname))
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "xOP");
		command_fail(si, fault_needmoreparams, _("Syntax: SOP|AOP|HOP|VOP <#channel> ADD|DEL|LIST <nickname>"));
		return;
	}

	/* make sure they're registered, logged in
	 * and the founder of the channel before
	 * we go any further.
	 */
	if (!si->smu)
	{
		/* if they're opers and just want to LIST, they don't have to log in */
		if (!(has_priv(si, PRIV_CHAN_AUSPEX) && !strcasecmp("LIST", cmd)))
		{
			command_fail(si, fault_noprivs, _("You are not logged in."));
			return;
		}
	}

	mc = mychan_find(chan);
	if (!mc)
	{
		command_fail(si, fault_nosuch_target, _("Channel \2%s\2 is not registered."), chan);
		return;
	}
	
	if (metadata_find(mc, "private:close:closer") && (!has_priv(si, PRIV_CHAN_AUSPEX) || strcasecmp("LIST", cmd)))
	{
		command_fail(si, fault_noprivs, _("\2%s\2 is closed."), chan);
		return;
	}

	level = get_template_flags(mc, leveldesc);
	if (level & CA_FOUNDER)
	{
		command_fail(si, fault_noprivs, _("\2%s\2 %s template has founder flag, not allowing xOP command."), chan, leveldesc);
		return;
	}

	/* ADD */
	if (!strcasecmp("ADD", cmd))
	{
		mu = myuser_find_ext(uname);

		/* As in /cs flags, allow founder to do anything */
		restrictflags = chanacs_source_flags(mc, si);
		if (restrictflags & CA_FOUNDER)
			restrictflags = ca_all;
		/* The following is a bit complicated, to allow for
		 * possible future denial of granting +f */
		if (!(restrictflags & CA_FLAGS))
		{
			command_fail(si, fault_noprivs, _("You are not authorized to perform this operation."));
			return;
		}
		restrictflags = allow_flags(mc, restrictflags);
		if ((restrictflags & level) != level)
		{
			command_fail(si, fault_noprivs, _("You are not authorized to perform this operation."));
			return;
		}
		cs_xop_do_add(si, mc, mu, uname, level, leveldesc, restrictflags);
	}

	else if (!strcasecmp("DEL", cmd))
	{
		mu = myuser_find_ext(uname);

		/* As in /cs flags, allow founder to do anything -- fix for #64: allow self removal. */
		restrictflags = chanacs_source_flags(mc, si);
		if (restrictflags & CA_FOUNDER || mu == si->smu)
			restrictflags = ca_all;
		/* The following is a bit complicated, to allow for
		 * possible future denial of granting +f */
		if (!(restrictflags & CA_FLAGS))
		{
			command_fail(si, fault_noprivs, _("You are not authorized to perform this operation."));
			return;
		}
		restrictflags = allow_flags(mc, restrictflags);
		if ((restrictflags & level) != level)
		{
			command_fail(si, fault_noprivs, _("You are not authorized to perform this operation."));
			return;
		}
		cs_xop_do_del(si, mc, mu, uname, level, leveldesc);
	}

	else if (!strcasecmp("LIST", cmd))
	{
		if (!chanacs_source_has_flag(mc, si, CA_ACLVIEW))
		{
			if (has_priv(si, PRIV_CHAN_AUSPEX))
				operoverride = 1;
			else
			{
				command_fail(si, fault_noprivs, _("You are not authorized to perform this operation."));
				return;
			}
		}
		cs_xop_do_list(si, mc, level, leveldesc, operoverride);
	}
}

static void cs_cmd_sop(sourceinfo_t *si, int parc, char *parv[])
{
	cs_xop(si, parc, parv, "SOP");
}

static void cs_cmd_aop(sourceinfo_t *si, int parc, char *parv[])
{
	cs_xop(si, parc, parv, "AOP");
}

static void cs_cmd_vop(sourceinfo_t *si, int parc, char *parv[])
{
	cs_xop(si, parc, parv, "VOP");
}

static void cs_cmd_hop(sourceinfo_t *si, int parc, char *parv[])
{
	/* Don't reject the command. This helps the rare case where
	 * a network switches to a non-halfop ircd: users can still
	 * remove pre-transition HOP entries.
	 */
	if (!ircd->uses_halfops && si->su != NULL)
		notice(chansvs.nick, si->su->nick, "Warning: Your IRC server does not support halfops.");

	cs_xop(si, parc, parv, "HOP");
}


static void cs_xop_do_add(sourceinfo_t *si, mychan_t *mc, myuser_t *mu, char *target, unsigned int level, const char *leveldesc, unsigned int restrictflags)
{
	char hostbuf[BUFSIZE];
	chanuser_t *cu;
	chanacs_t *ca;
	node_t *n;
	unsigned int addflags = level, removeflags = ~level;
	bool isnew;

	if (!mu)
	{
		/* we might be adding a hostmask */
		if (!validhostmask(target))
		{
			command_fail(si, fault_badparams, _("\2%s\2 is neither a registered account nor a hostmask."), target);
			return;
		}

		target = collapse(target);
		ca = chanacs_open(mc, NULL, target, true);
		if (ca->level == level)
		{
			command_fail(si, fault_nochange, _("\2%s\2 is already on the %s list for \2%s\2"), target, leveldesc, mc->name);
			return;
		}
		isnew = ca->level == 0;

		if (isnew && chanacs_is_table_full(ca))
		{
			command_fail(si, fault_toomany, _("Channel %s access list is full."), mc->name);
			chanacs_close(ca);
			return;
		}

		if (!chanacs_modify(ca, &addflags, &removeflags, restrictflags))
		{
			command_fail(si, fault_noprivs, _("You are not authorized to modify the access entry for \2%s\2 on \2%s\2."), target, mc->name);
			chanacs_close(ca);
			return;
		}
		chanacs_close(ca);
		if (!isnew)
		{
			/* they have access? change it! */
			logcommand(si, CMDLOG_SET, "ADD: \2%s\2 \2%s\2 on \2%s\2 (changed access)", mc->name, leveldesc, target);
			command_success_nodata(si, _("\2%s\2's access on \2%s\2 has been changed to \2%s\2."), target, mc->name, leveldesc);
			verbose(mc, "\2%s\2 changed \2%s\2's access to \2%s\2.", get_source_name(si), target, leveldesc);
		}
		else
		{
			logcommand(si, CMDLOG_SET, "ADD: \2%s\2 \2%s\2 on \2%s\2", mc->name, leveldesc, target);
			command_success_nodata(si, _("\2%s\2 has been added to the %s list for \2%s\2."), target, leveldesc, mc->name);
			verbose(mc, "\2%s\2 added \2%s\2 to the %s list.", get_source_name(si), target, leveldesc);
		}

		/* run through the channel's user list and do it */
		/* make sure the channel exists */
		if (mc->chan == NULL)
			return;
		LIST_FOREACH(n, mc->chan->members.head)
		{
			cu = (chanuser_t *)n->data;

			strlcpy(hostbuf, cu->user->nick, BUFSIZE);
			strlcat(hostbuf, "!", BUFSIZE);
			strlcat(hostbuf, cu->user->user, BUFSIZE);
			strlcat(hostbuf, "@", BUFSIZE);
			strlcat(hostbuf, cu->user->vhost, BUFSIZE);

			if (match(target, hostbuf))
				continue;

			if (ircd->uses_owner && level & CA_USEOWNER)
			{
				if (level & CA_AUTOOP && !(cu->modes & CSTATUS_OWNER))
				{
					modestack_mode_param(chansvs.nick, mc->chan, MTYPE_ADD, ircd->owner_mchar[1], CLIENT_NAME(cu->user));
					cu->modes |= CSTATUS_OWNER;
				}
			}
			if (ircd->uses_protect && level & CA_USEPROTECT)
			{
				if (level & CA_AUTOOP && !(cu->modes & CSTATUS_PROTECT) && !(ircd->uses_owner && cu->modes & CSTATUS_OWNER))
				{
					modestack_mode_param(chansvs.nick, mc->chan, MTYPE_ADD, ircd->protect_mchar[1], CLIENT_NAME(cu->user));
					cu->modes |= CSTATUS_PROTECT;
				}
			}
			if (level & CA_AUTOOP)
			{
				if (!(cu->modes & CSTATUS_OP))
				{
					modestack_mode_param(chansvs.nick, mc->chan, MTYPE_ADD, 'o', CLIENT_NAME(cu->user));
					cu->modes |= CSTATUS_OP;
				}
			}
			else if (ircd->uses_halfops && level & CA_AUTOHALFOP)
			{
				if (!(cu->modes & (CSTATUS_OP | ircd->halfops_mode)))
				{
					modestack_mode_param(chansvs.nick, mc->chan, MTYPE_ADD, ircd->halfops_mchar[1], CLIENT_NAME(cu->user));
					cu->modes |= ircd->halfops_mode;
				}
			}
			else if (level & (CA_AUTOVOICE | CA_AUTOHALFOP))
			{
				/* XXX HOP should have +V */
				if (!(cu->modes & (CSTATUS_OP | ircd->halfops_mode | CSTATUS_VOICE)))
				{
					modestack_mode_param(chansvs.nick, mc->chan, MTYPE_ADD, 'v', CLIENT_NAME(cu->user));
					cu->modes |= CSTATUS_VOICE;
				}
			}
		}
		return;
	}

	ca = chanacs_open(mc, mu, NULL, true);

	if (ca->level & CA_FOUNDER)
	{
		command_fail(si, fault_noprivs, _("\2%s\2 is the founder for \2%s\2 and may not be added to the %s list."), mu->name, mc->name, leveldesc);
		return;
	}

	if (ca->level == level)
	{
		command_fail(si, fault_nochange, _("\2%s\2 is already on the %s list for \2%s\2."), mu->name, leveldesc, mc->name);
		return;
	}

	/* NEVEROP logic moved here
	 * Allow changing access level, but not adding
	 * -- jilles */
	if (MU_NEVEROP & mu->flags && (ca->level == 0 || ca->level == CA_AKICK))
	{
		command_fail(si, fault_noprivs, _("\2%s\2 does not wish to be added to access lists (NEVEROP set)."), mu->name);
		chanacs_close(ca);
		return;
	}

	/*
	 * this is a little more cryptic than it used to be, but much cleaner. Functionally should be
	 * the same, with the exception that if they had access before, now it doesn't tell what it got
	 * changed from (I considered the effort to put an extra lookup in not worth it. --w00t
	 */
	/* just assume there's just one entry for that user -- jilles */

	isnew = ca->level == 0;
	if (isnew && chanacs_is_table_full(ca))
	{
		command_fail(si, fault_toomany, _("Channel %s access list is full."), mc->name);
		chanacs_close(ca);
		return;
	}

	if (!chanacs_modify(ca, &addflags, &removeflags, restrictflags))
	{
		command_fail(si, fault_noprivs, _("You are not authorized to modify the access entry for \2%s\2 on \2%s\2."), mu->name, mc->name);
		chanacs_close(ca);
		return;
	}
	chanacs_close(ca);

	if (!isnew)
	{
		/* they have access? change it! */
		logcommand(si, CMDLOG_SET, "ADD: \2%s\2 \2%s\2 on \2%s\2 (changed access)", mc->name, leveldesc, mu->name);
		command_success_nodata(si, _("\2%s\2's access on \2%s\2 has been changed to \2%s\2."), mu->name, mc->name, leveldesc);
		verbose(mc, "\2%s\2 changed \2%s\2's access to \2%s\2.", get_source_name(si), mu->name, leveldesc);
	}
	else
	{
		/* they have no access, add */
		logcommand(si, CMDLOG_SET, "ADD: \2%s\2 \2%s\2 on \2%s\2", mc->name, leveldesc, mu->name);
		command_success_nodata(si, _("\2%s\2 has been added to the %s list for \2%s\2."), mu->name, leveldesc, mc->name);
		verbose(mc, "\2%s\2 added \2%s\2 to the %s list.", get_source_name(si), mu->name, leveldesc);
	}

	/* run through the channel's user list and do it */
	/* make sure the channel exists */
	if (mc->chan == NULL)
		return;
	LIST_FOREACH(n, mc->chan->members.head)
	{
		cu = (chanuser_t *)n->data;

		if (cu->user->myuser != mu)
			continue;

		if (ircd->uses_owner && level & CA_USEOWNER)
		{
			if (level & CA_AUTOOP && !(cu->modes & CSTATUS_OWNER))
			{
				modestack_mode_param(chansvs.nick, mc->chan, MTYPE_ADD, ircd->owner_mchar[1], CLIENT_NAME(cu->user));
				cu->modes |= CSTATUS_OWNER;
			}
		}
		if (ircd->uses_protect && level & CA_USEPROTECT)
		{
			if (level & CA_AUTOOP && !(cu->modes & CSTATUS_PROTECT) && !(ircd->uses_owner && cu->modes & CSTATUS_OWNER))
			{
				modestack_mode_param(chansvs.nick, mc->chan, MTYPE_ADD, ircd->protect_mchar[1], CLIENT_NAME(cu->user));
				cu->modes |= CSTATUS_PROTECT;
			}
		}
		if (level & CA_AUTOOP)
		{
			if (!(cu->modes & CSTATUS_OP))
			{
				modestack_mode_param(chansvs.nick, mc->chan, MTYPE_ADD, 'o', CLIENT_NAME(cu->user));
				cu->modes |= CSTATUS_OP;
			}
		}
		else if (ircd->uses_halfops && level & CA_AUTOHALFOP)
		{
			if (!(cu->modes & (CSTATUS_OP | ircd->halfops_mode)))
			{
				modestack_mode_param(chansvs.nick, mc->chan, MTYPE_ADD, ircd->halfops_mchar[1], CLIENT_NAME(cu->user));
				cu->modes |= ircd->halfops_mode;
			}
		}
		else if (level & (CA_AUTOVOICE | CA_AUTOHALFOP))
		{
			/* XXX HOP should have +V */
			if (!(cu->modes & (CSTATUS_OP | ircd->halfops_mode | CSTATUS_VOICE)))
			{
				modestack_mode_param(chansvs.nick, mc->chan, MTYPE_ADD, 'v', CLIENT_NAME(cu->user));
				cu->modes |= CSTATUS_VOICE;
			}
		}
	}
}

static void cs_xop_do_del(sourceinfo_t *si, mychan_t *mc, myuser_t *mu, char *target, unsigned int level, const char *leveldesc)
{
	chanacs_t *ca;
	
	/* let's finally make this sane.. --w00t */
	if (!mu)
	{
		/* we might be deleting a hostmask */
		if (!validhostmask(target))
		{
			command_fail(si, fault_badparams, _("\2%s\2 is neither a nickname nor a hostmask."), target);
			return;
		}

		ca = chanacs_find_host_literal(mc, target, level);
		if (ca == NULL || ca->level != level)
		{
			command_fail(si, fault_nochange, _("\2%s\2 is not on the %s list for \2%s\2."), target, leveldesc, mc->name);
			return;
		}

		object_unref(ca);
		verbose(mc, "\2%s\2 removed \2%s\2 from the %s list.", get_source_name(si), target, leveldesc);
		logcommand(si, CMDLOG_SET, "DEL: \2%s\2 \2%s\2 from \2%s\2", mc->name, leveldesc, target);
		command_success_nodata(si, _("\2%s\2 has been removed from the %s list for \2%s\2."), target, leveldesc, mc->name);
		return;
	}

	if (!(ca = chanacs_find(mc, mu, level)) || ca->level != level)
	{
		command_fail(si, fault_nochange, _("\2%s\2 is not on the %s list for \2%s\2."), mu->name, leveldesc, mc->name);
		return;
	}

	object_unref(ca);
	command_success_nodata(si, _("\2%s\2 has been removed from the %s list for \2%s\2."), mu->name, leveldesc, mc->name);
	logcommand(si, CMDLOG_SET, "DEL: \2%s\2 \2%s\2 from \2%s\2", mc->name, leveldesc, mu->name);
	verbose(mc, "\2%s\2 removed \2%s\2 from the %s list.", get_source_name(si), mu->name, leveldesc);
}


static void cs_xop_do_list(sourceinfo_t *si, mychan_t *mc, unsigned int level, const char *leveldesc, int operoverride)
{
	chanacs_t *ca;
	int i = 0;
	node_t *n;

	command_success_nodata(si, _("%s list for \2%s\2:"), leveldesc ,mc->name);
	LIST_FOREACH(n, mc->chanacs.head)
	{
		ca = (chanacs_t *)n->data;
		if (ca->level == level)
		{
			if (!ca->myuser)
				command_success_nodata(si, "%d: \2%s\2", ++i, ca->host);
			else if (LIST_LENGTH(&ca->myuser->logins))
				command_success_nodata(si, _("%d: \2%s\2 (logged in)"), ++i, ca->myuser->name);
			else
				command_success_nodata(si, _("%d: \2%s\2 (not logged in)"), ++i, ca->myuser->name);
		}
	}
	/* XXX */
	command_success_nodata(si, _("Total of \2%d\2 %s in %s list of \2%s\2."), i, (i == 1) ? "entry" : "entries", leveldesc, mc->name);
	if (operoverride)
		logcommand(si, CMDLOG_ADMIN, "LIST: \2%s\2 \2%s\2 (oper override)", mc->name, leveldesc);
	else
		logcommand(si, CMDLOG_GET, "LIST: \2%s\2 \2%s\2", mc->name, leveldesc);
}

static void cs_cmd_forcexop(sourceinfo_t *si, int parc, char *parv[])
{
	char *chan = parv[0];
	chanacs_t *ca;
	mychan_t *mc = mychan_find(chan);
	node_t *n;
	int changes;
	unsigned int newlevel;
	const char *desc;
	unsigned int ca_sop, ca_aop, ca_hop, ca_vop;

	if (!chan)
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "FORCEXOP");
		command_fail(si, fault_needmoreparams, _("Syntax: FORCEXOP <#channel>"));
		return;
	}

	if (!mc)
	{
		command_fail(si, fault_nosuch_target, _("Channel \2%s\2 is not registered."), chan);
		return;
	}

	if (metadata_find(mc, "private:close:closer"))
	{
		command_fail(si, fault_noprivs, _("\2%s\2 is closed."), chan);
		return;
	}

	if (!is_founder(mc, si->smu))
	{
		command_fail(si, fault_noprivs, _("You are not authorized to perform this operation."));
		return;
	}

	ca_sop = get_template_flags(mc, "SOP");
	ca_aop = get_template_flags(mc, "AOP");
	ca_hop = get_template_flags(mc, "HOP");
	ca_vop = get_template_flags(mc, "VOP");

	changes = 0;
	LIST_FOREACH(n, mc->chanacs.head)
	{
		ca = (chanacs_t *)n->data;

		if (ca->level & CA_AKICK)
			continue;
		if (ca->level & CA_FOUNDER)
			newlevel = CA_INITIAL & ca_all, desc = "Founder";
		else if (!(~ca->level & ca_sop))
			newlevel = ca_sop, desc = "SOP";
		else if (ca->level == ca_aop)
			newlevel = ca_aop, desc = "AOP";
		else if (ca->level == ca_hop)
			newlevel = ca_hop, desc = "HOP";
		else if (ca->level == ca_vop)
			newlevel = ca_vop, desc = "VOP";
		else if (ca->level & (CA_SET | CA_RECOVER | CA_FLAGS))
			newlevel = ca_sop, desc = "SOP";
		else if (ca->level & (CA_OP | CA_AUTOOP | CA_REMOVE))
			newlevel = ca_aop, desc = "AOP";
		else if (ca->level & (CA_HALFOP | CA_AUTOHALFOP | CA_TOPIC))
		{
			if (ca_hop == ca_vop)
				newlevel = ca_aop, desc = "AOP";
			else
				newlevel = ca_hop, desc = "HOP";
		}
		else /*if (ca->level & CA_AUTOVOICE)*/
			newlevel = ca_vop, desc = "VOP";
#if 0
		else
			newlevel = 0;
#endif
		if (newlevel == ca->level)
			continue;
		changes++;
		command_success_nodata(si, "%s: %s -> %s", ca->myuser ? ca->myuser->name : ca->host, bitmask_to_flags(ca->level, chanacs_flags), desc);
		chanacs_modify_simple(ca, newlevel, ~newlevel);
	}
	command_success_nodata(si, _("FORCEXOP \2%s\2 done (\2%d\2 changes)"), mc->name, changes);
	if (changes > 0)
		verbose(mc, "\2%s\2 reset access levels to xOP (\2%d\2 changes)", get_source_name(si), changes);
	logcommand(si, CMDLOG_SET, "FORCEXOP: \2%s\2 (\2%d\2 changes)", mc->name, changes);
}

/* vim:cinoptions=>s,e0,n0,f0,{0,}0,^0,=s,ps,t0,c3,+s,(2s,us,)20,*30,gs,hs
 * vim:ts=8
 * vim:sw=8
 * vim:noexpandtab
 */
