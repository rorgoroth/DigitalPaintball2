/*
Copyright (c) 2014 Nathan "jitspoe" Wulf, Digital Paint

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "bot_main.h"
#include "bot_manager.h"


static void BotHandleDisconnect (const edict_t *ent)
{
	int i;
	int count = bots.count;
	qboolean removed = false;

	// Check to see if the entity that disconnected was one of the bots.  If so, remove it from the manager.
	for (i = 0; i < count; ++i)
	{
		// Shift all entries over 1 since a previous entry was removed.
		if (removed)
		{
			bots.ents[i - 1] = bots.ents[i];
			bots.movement[i - 1] = bots.movement[i];
			bots.goals[i - 1] = bots.goals[i];
			bots.goal_debug_spheres[i - 1] = bots.goal_debug_spheres[i];;
		}
		else if (bots.ents[i] == ent)
		{
			--bots.count;
			removed = true;
		}
	}

	BotCancelObservation(ent);
	// todo: BotUpdateGoals(); // Index may be offset for some bots, so we need to update all bots
}


void BotHandleFlagGrab (edict_t *flag, edict_t *player_ent)
{
	int bot_index;

	for (bot_index = 0; bot_index < bots.count; ++bot_index)
	{
		if (bots.ents[bot_index] == player_ent)
		{
			bots.goals[bot_index].has_flag = true;
			BotGoalWander(bot_index, (int)(nu_rand(4.0) * 1000.0)); // wander for a few seconds so the returning path isn't super predictable (also clear out current goal).
		}
	}
}


void BotHandleRoundStart (void)
{
	int bot_index;
	bots.between_rounds = false;
	bots.round_start_time = bots.level_time;

	// Bots don't have a flag when the round starts.
	for (bot_index = 0; bot_index < bots.count; ++bot_index)
	{
		bots.movement[bot_index].last_target_msec = 99999; // Clear out the target so bots don't wander around facing backward on new rounds (hopefully)
		bots.movement[bot_index].aim_target = NULL;
		bots.goals[bot_index].has_flag = false;
	}
}


void BotHandleRoundOver (void)
{
	int bot_index;
	bots.between_rounds = true;

	for (bot_index = 0; bot_index < bots.count; ++bot_index)
	{
		bots.goals[bot_index].timeleft_msec = nu_rand(2000) + 200; // Round just ended, so stop going for current goals over the next couple seconds.
	}
}


void BotHandleRespawn (edict_t *ent)
{
	int bot_index;

	for (bot_index = 0; bot_index < bots.count; ++bot_index)
	{
		if (bots.ents[bot_index] == ent)
		{
			bots.goals[bot_index].has_flag = false; // Double make sure the bot doesn't think it has a flag, since it just respawned. (though there is a thing where you can grab a flag while in jail, but that's not normal gameplay...)
		}
	}
}


void BotHandleCap (edict_t *ent, edict_t *flag)
{
	int bot_index;

	for (bot_index = 0; bot_index < bots.count; ++bot_index)
	{
		if (bots.ents[bot_index] == ent)
		{
			bots.goals[bot_index].has_flag = false;
		}
	}
}


void BotHandleKill (edict_t *victim, edict_t *inflictor, edict_t *killer)
{
	BotCancelObservation(victim);
}


void BotHandleGameEvent (game_event_t event, edict_t *ent, void *data1, void *data2)
{
	switch (event)
	{
	case EVENT_DISCONNECT:
		BotHandleDisconnect(ent);
		break;
	case EVENT_GRAB:
		BotHandleFlagGrab(ent, (edict_t *)data1);
		break;
	case EVENT_ROUNDSTART:
		BotHandleRoundStart();
		break;
	case EVENT_RESPAWN:
		BotHandleRespawn(ent);
		break;
	case EVENT_CAP:
		BotHandleCap(ent, (edict_t *)data1);
		break;
	case EVENT_DROPFLAG:
		//BotHandleFlagDrop(ent); // Ent is the carrier.  We don't know the flag...
		break;
	case EVENT_ROUNDOVER:
		BotHandleRoundOver();
		break;
	case EVENT_KILL:
		BotHandleKill(ent, (edict_t *)data1, (edict_t *)data2); // victim, inflictor, killer
		break;
	}
	// Events like round starts, flag grabs, eliminations, etc. will be passed in here
}


// todo: check if a bot has been removed or replaced.  Update bot manager

