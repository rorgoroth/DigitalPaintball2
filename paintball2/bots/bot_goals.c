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
#include "bot_goals.h"
#include "../game/game.h"
#include "bot_debug.h"


#define MAX_BOT_OBJECTIVES 32 // don't think we'll have more than 32 flags/capture zones/etc. in any game mode.  Increase if needed.


typedef struct {
	bot_objective_type_t	objective_type;
	const edict_t			*ent;
	int						team_index;
	int						player_index; // for player-specific objectives (ex: capturing a flag can only be done by the player carrying the flag).
	float					weight; // temp variable for weighting objectives when bot is deciding what to do.
	qboolean				active;
} bot_objective_t;


bot_objective_t		g_bot_objectives[MAX_BOT_OBJECTIVES];
int					g_bot_num_objectives = 0;


// Called for the game for objective type game modes (flags, bases, etc).
void BotAddObjective (bot_objective_type_t objective_type, int player_index, int team_index, const edict_t *ent)
{
	int i;
	int first_free = MAX_BOT_OBJECTIVES;

	// Don't add an objective if it's already there.
	for (i = 0; i < MAX_BOT_OBJECTIVES; ++i)
	{
		bot_objective_t *objective = g_bot_objectives + i;

		if (objective->active && objective->ent == ent && objective->objective_type == objective_type && objective->team_index == team_index && objective->player_index == objective->player_index)
		{
			return;
		}
	}

	for (i = 0; i < MAX_BOT_OBJECTIVES; ++i)
	{
		bot_objective_t *objective = g_bot_objectives + i;

		if (!objective->active)
		{
			objective->active = true;
			objective->objective_type = objective_type;
			objective->player_index = player_index;
			objective->team_index = team_index;
			objective->ent = ent;
			++g_bot_num_objectives;

			return;
		}
	}

	assert(i < MAX_BOT_OBJECTIVES);
}

// Eample: when flag is picked up, the flag objective is removed.
void BotRemoveObjective (bot_objective_type_t objective_type, const edict_t *ent)
{
	int i;

	for (i = 0; i < MAX_BOT_OBJECTIVES; ++i)
	{
		bot_objective_t *objective = g_bot_objectives + i;

		if (objective->active && objective->ent == ent && objective->objective_type == objective_type)
		{
			objective->active = false;
			--g_bot_num_objectives;
		}
	}
}

// Called from the game at the start of rounds/matches so we know to get a fresh start.
void BotClearObjectives (void)
{
	memset(g_bot_objectives, 0, sizeof(g_bot_objectives)); // I love oldschool C.
	memset(bots.goals, 0, sizeof(bots.goals));
	g_bot_num_objectives = 0;
}


void BotPlayerDie (int player_index, const edict_t *ent)
{
	// Unused for now, but can be called if we need to do something when a player dies (ex: chat messages)
}


// For siege mode -- called on round start
void BotSetDefendingTeam (int defending_team)
{
	bots.defending_team = defending_team;
}


void BotSetGoal (int bot_index, botgoaltype_t goaltype, vec3_t position)
{
	botgoal_t *goal = bots.goals + bot_index;

	goal->type = goaltype;
	goal->active = true;
	goal->changed = true;
	VectorCopy(position, goal->pos);
}


void BotRetryGoal (int bot_index)
{
	bots.goals[bot_index].changed = true;
}


void BotClearGoals (void)
{
	memset(bots.goals, 0, sizeof(bots.goals));
}


void BotPathfindComplete (int bot_index)
{
	botgoal_t *goal = bots.goals + bot_index;

	// TODO: If the goal is to grab the flag, don't deactivate goal until flag is actually grabbed.

	if (goal->type == BOT_GOAL_DEFEND_FLAG)
	{
		BotStopMoving(bot_index);

		if (goal->ent)
		{
			BotAimAtPosition(bot_index, goal->ent->s.origin);
		}
	}
	else
	{
		goal->active = false;

		if (bot_debug->value)
		{
			bi.dprintf("Path find complete. Deactivating goal.\n");
		}
	}
}


void BotGoalWander (int bot_index, int msec)
{
	botgoal_t *goal = bots.goals + bot_index;
	int random_point = (int)nu_rand(g_bot_waypoints.num_points); // todo: pick randomly select several and use the most player-popular one.

	if (bot_debug->value)
		bi.dprintf("Bot %d: Wandering.\n", bot_index);

	goal->active = true;
	goal->type = BOT_GOAL_WANDER;
	goal->changed = true;
	VectorCopy(g_bot_waypoints.positions[random_point], goal->pos);
	goal->timeleft_msec = msec;
}


void BotDefendFlag (int bot_index, const edict_t *flag_ent, int time_msec)
{
	botgoal_t *goal = bots.goals + bot_index;

	if (bot_debug->value)
		bi.dprintf("Bot %d: Defending for %dms.\n", bot_index, time_msec);
	goal->active = true;
	goal->type = BOT_GOAL_DEFEND_FLAG;
	goal->changed = true;
	goal->timeleft_msec = time_msec;
	goal->ent = flag_ent;

	VectorCopy(flag_ent->s.origin, goal->pos);
	// TODO: Find an ideal location near the flag and looking at the flag.
	goal->pos[0] += 100.0 - nu_rand(200.0);
	goal->pos[1] += 100.0 - nu_rand(200.0);
}


const char *BotObjectiveTypeString (int objective_type)
{
	switch (objective_type)
	{
	case BOT_OBJECTIVE_TYPE_UNSET:
		return "UNSET";
	case BOT_OBJECTIVE_TYPE_FLAG:
		return "FLAG";
	case BOT_OBJECTIVE_TYPE_BASE:
		return "BASE";
	}
	return "UNKNOWN";
}


void BotUpdateGoals (int msec)
{
	int bot_index;

	for (bot_index = 0; bot_index < bots.count; ++bot_index)
	{
		botgoal_t *goal = bots.goals + bot_index;
		edict_t *bot_ent = bots.ents[bot_index];

		if (goal->active)
		{
			if (goal->type == BOT_GOAL_DEFEND_FLAG)
			{
				if (goal->ent)
				{
					if (BotHasLineOfSightToEnt(bot_index, goal->ent))
					{
						// TODO: Maybe aim a bit randomly and check angles or something.
						BotAimAtPosition(bot_index, goal->ent->s.origin);
					}
				}
			}

			goal->timeleft_msec -= msec;

			if (goal->timeleft_msec <= 0)
				goal->active = false;
		}

		if (!goal->active)
		{
			qboolean wander = true;
			float wander_chance = 0.5f; // 50% chance to wander by default
			int max_wander_time_ms = 20000; // 20s

			if (goal->has_flag)
			{
				wander_chance = 0.1f; // 90% chance to go for an objective when carrying flag
				max_wander_time_ms = 5000; // occasionally wander up to 5s to mix up where the carrier goes.
			}

			// In siege, make the defending team bots always try to defend the flag so they don't leave the base.
			if (bots.game_mode == CTFTYPE_SIEGE && bi.GetTeam(bot_ent) == bots.defending_team)
			{
				int i;

				if (bot_debug->value)
					bi.dprintf("Bot %d attempting to defend.\n", bot_index);

				for (i = 0; i < MAX_BOT_OBJECTIVES; ++i)
				{
					bot_objective_t *objective = g_bot_objectives + i;
					if (objective->active)
					{
						if (objective->objective_type == BOT_OBJECTIVE_TYPE_FLAG)
						{
							BotDefendFlag(bot_index, objective->ent, (int)nu_rand(30000.0f));
							wander = false;
							break;
						}
					}
				}
			}
			else
			{
				if (nu_rand(1.0f) > wander_chance)
				{
					float total_objective_weights = 0.0f;
					int i;
					int bot_team = bi.GetTeam(bot_ent);

					if (bot_debug->value)
						bi.dprintf("Bot %d: Looking for a goal.\n", bot_index);

					// Weight goals based on current conditions
					for (i = 0; i < MAX_BOT_OBJECTIVES; ++i)
					{
						bot_objective_t *objective = g_bot_objectives + i;
						
						objective->weight = 0.0;
						
						if (objective->active)
						{
							qboolean same_team = (bi.GetTeam(objective->ent) == bot_team);
							if (objective->objective_type == BOT_OBJECTIVE_TYPE_BASE)
							{
								if (goal->has_flag && (bots.game_mode == CTFTYPE_SIEGE || (bots.game_mode == CTFTYPE_1FLAG && !same_team) || same_team))
								{
									objective->weight = 1.0; // todo: Divide by distance?
								}
							}
							else if (objective->objective_type == BOT_OBJECTIVE_TYPE_FLAG)
							{
								if (!same_team)
								{
									objective->weight = 1.0; // todo: Divide by distance?
								}
							}
							
							total_objective_weights += objective->weight;
						}
					}

					if (total_objective_weights > 0.0)
					{
						float random_weight = nu_rand(total_objective_weights);
						total_objective_weights = 0.0;

						for (i = 0; i < MAX_BOT_OBJECTIVES; ++i)
						{
							bot_objective_t *objective = g_bot_objectives + i;
							total_objective_weights += objective->weight;
							if (total_objective_weights >= random_weight)
							{
								if (objective->objective_type == BOT_OBJECTIVE_TYPE_FLAG)
								{
									if (bi.GetTeam(objective->ent) == bot_team)
									{
										BotDefendFlag(bot_index, objective->ent, (int)nu_rand(30000.0f)); // 0-30s
									}
									else
									{
										if (bot_debug->value)
											bi.dprintf("Bot %d: Going for flag.\n", bot_index);
										goal->active = true;
										goal->type = BOT_GOAL_REACH_POSITION; // TODO: Grab flag type
										goal->changed = true;
										goal->timeleft_msec = 10000 + (int)nu_rand(60000.0f); // 10-70 seconds.
										VectorCopy(objective->ent->s.origin, goal->pos);
										goal->pos[2] += objective->ent->mins[2] - bot_ent->mins[2] + 1.0; // flag mins aren't as low as player mins, so we need to make sure this is reachable, otherwise traces will fail.
									}
								}
								else if (objective->objective_type == BOT_OBJECTIVE_TYPE_BASE)
								{
									trace_t trace;
									vec3_t target_center;
									int axis;
									vec3_t temp_mins, temp_maxs;

									// Base might be in some kind of tight location, like under a platform, so make a really small mins/maxs to check for the ground position
									VectorCopy(bot_ent->mins, temp_mins);
									VectorCopy(bot_ent->maxs, temp_maxs);
									temp_mins[2] = 0;
									temp_maxs[2] = 12;

									goal->active = true;
									goal->type = BOT_GOAL_REACH_POSITION;
									goal->changed = true;
									goal->timeleft_msec = (int)nu_rand(70000.0f); // 0-70 seconds.
									VectorCopy(objective->ent->s.origin, goal->pos);

									// Bases have a pos of 0 but mins/maxs define a volume, so randomly pick a location in that volume.  Guess that'll give us some slight variation on flag grabs and such as well.
									for (axis = 0; axis < 3; ++axis)
									{
										float min = objective->ent->mins[axis];
										float max = objective->ent->maxs[axis];
									
										goal->pos[axis] += min + nu_rand(max - min) * 0.5; // only do 50% of the trigger volume to avoid having targets on sloped walls.
										// Center of triggers is not at the entity origin, which is usually 0, 0, 0, but instead at the average of mins + maxs
										target_center[axis] = objective->ent->s.origin[axis] + (objective->ent->mins[axis] + objective->ent->maxs[axis]) * 0.5;
									}

									// Sometimes the base triggers go beyond the playable space, so cast from the center out.
									trace = bi.trace(target_center, temp_mins, temp_maxs, goal->pos, bot_ent, MASK_PLAYERSOLID);

									// Sometimes the base is centered in solid ground (ex: pbcup.bsp) or is right on the ground (routez.bsp), so we need to move up a bit
									if (trace.startsolid)
									{
										target_center[2] += 32.0;
										trace = bi.trace(target_center, temp_mins, temp_maxs, goal->pos, bot_ent, MASK_PLAYERSOLID);

										if (trace.startsolid)
										{
											target_center[2] += 32.0; // move up even more
											trace = bi.trace(target_center, temp_mins, temp_maxs, goal->pos, bot_ent, MASK_PLAYERSOLID);
										}
									}

									// Set our end pos to what it would have been if we used the larger trace bounds.
									trace.endpos[2] += temp_mins[2] - bot_ent->mins[2];

									{
										vec3_t down_pos;
										VectorCopy(trace.endpos, down_pos);
										down_pos[2] -= (objective->ent->maxs[2] - objective->ent->mins[2]);
										trace = bi.trace(trace.endpos, bot_ent->mins, bot_ent->maxs, down_pos, bot_ent, MASK_PLAYERSOLID);
										VectorCopy(trace.endpos, goal->pos);
#ifdef BOT_DEBUG
										bots.goal_debug_spheres[bot_index] = DrawDebugSphere(goal->pos, 18.0f, 1.0f, 0.5f, 0.0f, 20.0f, bots.goal_debug_spheres[bot_index]);
#endif
									}

									if (bot_debug->value)
											bi.dprintf("Bot %d: Going for cap.\n", bot_index);
								}
								wander = false;
								break;
							}
						}
					}
					else
					{
						if (bot_debug->value)
							bi.dprintf("No objectives available.\n");
					}

//					if (g_bot_num_objectives > 0)
//					{
//						int random_objective_index = (int)nu_rand(g_bot_num_objectives);
//						int i;
//						int active_objective_index = 0;
//
//						// some of the objectives might not be active, so we have to do this loop
//						for (i = 0; i < MAX_BOT_OBJECTIVES && active_objective_index <= random_objective_index; ++i)
//						{
//							bot_objective_t *objective = g_bot_objectives + i;
//						
//							if (objective->active)
//							{
//								if (active_objective_index == random_objective_index)
//								{
//									qboolean same_team = bi.GetTeam(objective->ent) == bi.GetTeam(bot_ent);
//
//									// If we have the flag, ignore goals that are not our own base or other enemy flags
//									if (!goal->has_flag || (objective->objective_type == BOT_OBJECTIVE_TYPE_BASE && (same_team || bots.game_mode == CTFTYPE_SIEGE)) || (objective->objective_type == BOT_OBJECTIVE_TYPE_FLAG && !same_team))
//									{
//										int axis;
//
//										if (bot_debug->value)
//											bi.dprintf("Bot %d: Going with objective %d (%d) -- %s.\n", bot_index, active_objective_index, i, BotObjectiveTypeString(objective->objective_type));
//
//										if (objective->objective_type == BOT_OBJECTIVE_TYPE_FLAG && same_team)
//										{
//											BotDefendFlag(bot_index, objective->ent, (int)nu_rand(70000.0f)); // 0-70s
//										}
//										else
//										{
//											trace_t trace;
//											vec3_t target_center;
//
//											goal->active = true;
//											goal->type = BOT_GOAL_REACH_POSITION;
//											goal->changed = true;
//											goal->timeleft_msec = (int)nu_rand(70000.0f); // 0-70 seconds.
//											VectorCopy(objective->ent->s.origin, goal->pos);
//
//											// Bases have a pos of 0 but mins/maxs define a volume, so randomly pick a location in that volume.  Guess that'll give us some slight variation on flag grabs and such as well.
//											for (axis = 0; axis < 3; ++axis)
//											{
//												float min = objective->ent->mins[axis];
//												float max = objective->ent->maxs[axis];
//									
//												goal->pos[axis] += min + nu_rand(max - min) * 0.5; // only do 50% of the trigger volume to avoid having targets on sloped walls.
//												// Center of triggers is not at the entity origin, which is usually 0, 0, 0, but instead at the average of mins + maxs
//												target_center[axis] = objective->ent->s.origin[axis] + (objective->ent->mins[axis] + objective->ent->maxs[axis]) * 0.5;
//											}
//
//											trace = bi.trace(target_center, bot_ent->mins, bot_ent->maxs, goal->pos, bot_ent, MASK_PLAYERSOLID);
//
//											{
//												vec3_t down_pos;
//												VectorCopy(trace.endpos, down_pos);
//												down_pos[2] -= (objective->ent->maxs[2] - objective->ent->mins[2]);
//												trace = bi.trace(trace.endpos, bot_ent->mins, bot_ent->maxs, down_pos, bot_ent, MASK_PLAYERSOLID);
//												VectorCopy(trace.endpos, goal->pos);
//#ifdef BOT_DEBUG
//												bots.goal_debug_spheres[bot_index] = DrawDebugSphere(goal->pos, 18.0f, 1.0f, 0.5f, 0.0f, 20.0f, bots.goal_debug_spheres[bot_index]);
//#endif
//											}
//										}
//
//										wander = false;
//										break;
//									}
//								}
//
//								++active_objective_index;
//							}
//						}
//					}
//					else
//					{
//						if (bot_debug->value)
//							bi.dprintf("No objectives available.\n");
//					}
				}
			}

			if (wander)
			{
				BotGoalWander(bot_index, (int)nu_rand(max_wander_time_ms));
			}
		}

		if (goal->changed)
		{
			botmovedata_t *movement = bots.movement + bot_index;

			// Avoid doing a bunch of pathfinds in one update
			if (movement->time_til_try_path <= 0 && bots.time_since_last_pathfind > 0)
			{
				goal->changed = false;
				movement->waypoint_path.active = AStarFindPathFromEntityToPos(bot_ent, goal->pos, &bots.movement[bot_index].waypoint_path);
				movement->stop = false;
				movement->time_til_try_path = 200 + (int)nu_rand(1000); // force some delay between path finds so bots don't spaz out and try to path find every single update.
				bots.time_since_last_pathfind = 0;

				if (!movement->waypoint_path.active)
					goal->timeleft_msec = 100 + (int)nu_rand(500); // couldn't path find to goal.  Try to find another one quickly.

				if (bot_debug->value)
				{
					bi.dprintf("Bot %d: Pathfinding to goal at %g, %g, %g: %s.\n", bot_index, goal->pos[0], goal->pos[1], goal->pos[2], movement->waypoint_path.active ? "Success" : "Failed");
				}
			}
		}
	}
}
