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

#ifndef _BOT_GOALS_H_
#define _BOT_GOALS_H_

#include "../game/q_shared.h"

typedef enum {
	BOT_GOAL_WANDER = 0,
	BOT_GOAL_REACH_POSITION,
	BOT_GOAL_DEFEND_FLAG,
	BOT_GOAL_STOP_AND_WAIT, // for between rounds, to make them look more human
	BOT_GOAL_MAX_COUNT
} botgoaltype_t;


typedef struct {
	botgoaltype_t	type;
	vec3_t			pos;
	const edict_t	*ent;
	qboolean		changed;
	qboolean		active;
	int				timeleft_msec;
	qboolean		has_flag;
	qboolean		end_on_path_complete; // If true, select a new goal when reaching the location.  If not, stop at location until goal time is complete.
} botgoal_t;


void BotAddObjective (bot_objective_type_t objective_type, int player_index, int team_index, const edict_t *ent);
void BotRemoveObjective (bot_objective_type_t objective_type, const edict_t *ent);
void BotClearObjectives (void);
void BotSetGoal (int bot_index, botgoaltype_t goaltype, const vec3_t position, const edict_t *target_ent, qboolean end_on_path_complete, int time_msec);
void BotRetryGoal (int bot_index);
void BotClearGoals (void);
void BotPathfindComplete (int bot_index);
void BotGoalWander (int bot_index, int time_ms);
void BotSetDefendingTeam (int defending_team);

#endif // _BOT_GOALS_H_
