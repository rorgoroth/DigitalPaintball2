/*
Copyright (c) 2015-2020 Nathan "jitspoe" Wulf, Digital Paint

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

#include "../game/q_shared.h"
#include "../game/game.h"
#include "bot_main.h"
#include "bot_manager.h"
#include "bot_debug.h"


#define MAX_PLAYER_POS_HISTORY 4
static const vec3_t vec_zero = { 0.0f, 0.0f, 0.0f };
static vec3_t player_pos_history[MAX_PLAYER_POS_HISTORY][MAX_CLIENTS];
static int maxclients = 256;


void BotInitAim (void)
{
	int size_test2 = sizeof(player_pos_history[0]);
	memset(player_pos_history, 0, sizeof(player_pos_history));
	maxclients = (int)(bi.cvar("maxclients", "256", 0)->value);
}


void VecToAngles (vec3_t value1, vec3_t angles)
{
	float	forward;
	float	yaw, pitch;
	
	if (value1[1] == 0 && value1[0] == 0)
	{
		yaw = 0;
		if (value1[2] > 0)
			pitch = 90;
		else
			pitch = 270;
	}
	else
	{
		yaw = atan2(value1[1], value1[0]) * 180 / M_PI;

		if (yaw < 0)
			yaw += 360;

		forward = sqrt (value1[0]*value1[0] + value1[1]*value1[1]);
		pitch = atan2(value1[2], forward) * 180 / M_PI;

		if (pitch < 0)
			pitch += 360;
	}

	angles[PITCH] = -pitch;
	angles[YAW] = yaw;
	angles[ROLL] = 0;
}


void VecToAnglesShort (vec3_t vec_val, short *angles_short)
{
	vec3_t angles;
	VecToAngles(vec_val, angles);
	angles_short[YAW] = ANGLE2SHORT(angles[YAW]);
	angles_short[PITCH] = ANGLE2SHORT(angles[PITCH]);
}


void BotSetDesiredAimAnglesFromPoint (int botindex, const vec3_t point)
{
	botmovedata_t *movement = bots.movement + botindex;
	edict_t *bot = bots.ents[botindex];
	vec3_t to_target;

	VectorSubtract(point, bot->s.origin, to_target);
	VectorNormalize(to_target);
	VecToAngles(to_target, movement->desired_angles);

	if (movement->desired_angles[PITCH] < -180.0f)
		movement->desired_angles[PITCH] += 360.0f;

	if (movement->desired_angles[PITCH] > 180.0f)
		movement->desired_angles[PITCH] -= 360.0f;

	//bi.bprintf(PRINT_HIGH, "Y: %f, P: %f\n", movement->desired_yaw, movement->desired_pitch);
}


// Makes sure angle is +/- 180 degrees (only works for angles with +/- 360 degrees)
float AngleTo180 (float angle)
{
	if (angle > 180.0f)
		angle -= 360.0f;
	else if (angle < -180.0f)
		angle += 360.0f;

	return angle;
}


void BotAimTowardDesiredAngles (int botindex, int msec)
{
	botmovedata_t *movement = bots.movement + botindex;
	float dt = (float)msec / 1000.0f;

	movement->aimtimeleft -= dt;

	if (movement->aimtimeleft <= 0.0001f)
	{
		float delta_yaw, delta_pitch;
		edict_t *bot = bots.ents[botindex];
		float old_yawspeed = movement->yawspeed;
		float old_pitchspeed = movement->pitchspeed;
		float new_yawspeed, new_pitchspeed;
		int additional_frames = (int)nu_rand(3.0f);
		float aim_time = dt + dt * additional_frames;
		float aim_time_rand = aim_time + nu_rand(aim_time * 0.3f) - nu_rand(aim_time * 0.3f); // +/- 30% time estimation, so bots "mess up" their aim a little.
		float current_pitch = bot->s.angles[PITCH] * 3.0f; // entity pitch is 1/3 actual aim pitch.

		delta_yaw = AngleTo180(movement->desired_angles[YAW] - bot->s.angles[YAW]);
		delta_pitch = AngleTo180(movement->desired_angles[PITCH] - current_pitch);

		// Calculate turn speed
		new_yawspeed = delta_yaw / aim_time_rand;
		new_pitchspeed = delta_pitch / aim_time_rand;

		// Damp yaw speed to make it less twitchy
		movement->yawspeed = DampIIR(old_yawspeed, new_yawspeed, 0.1f, aim_time);
		movement->pitchspeed = DampIIR(old_pitchspeed, new_pitchspeed, 0.1f, aim_time);
		movement->aimtimeleft = aim_time;
	}
}

// Enforce latency on player positions so the bots don't have inhuman reaction time.
void GetOldPlayerPosition (const edict_t *ent, vec_t *vec_out)
{
	int playerindex = bi.GetPlayerIndexFromEnt(ent);
	assert(playerindex >= 0 && playerindex < maxclients);
	VectorCopy(player_pos_history[MAX_PLAYER_POS_HISTORY - 1][playerindex], vec_out);
}


qboolean BotHasLineOfSightToEnt (int bot_index, const edict_t *target)
{
	edict_t *self = bots.ents[bot_index];
	vec3_t viewpos;
	trace_t trace;


	// Trace from the bot's eye position to the enemy's position + some randomization (so if only part of the enemy is showing, the bot might see it sometimes, but not all the time).
	VectorCopy(self->s.origin, viewpos);
	viewpos[2] += bi.GetViewHeight(self);
	/*targetpos[2] += bi.GetViewHeight(target) + nu_rand(8) - nu_rand(16);
	targetpos[0] += nu_rand(14) - nu_rand(14);
	targetpos[1] += nu_rand(14) - nu_rand(14);*/
	trace = bi.trace(viewpos, vec_zero, vec_zero, target->s.origin, self, MASK_SOLID);

	// TODO: Add smoke grenade checks here.

	if (trace.fraction == 1.0f || trace.ent == target)
		return true;
	else
		return false;
}


// Called every frame -- acquire and shoot at targets.
void BotAimAndShoot (int botindex, int msec)
{
	botmovedata_t *movement = bots.movement + botindex;
	edict_t *target = bi.GetNextLivePlayerEnt(NULL, false);
	edict_t *self = bots.ents[botindex];
	edict_t *best_target = NULL;
	float best_dist_sq = 4096.0f * 4096.0f;

	while (target)
	{
		if (target != self)
		{
			vec3_t diff;
			vec3_t targetpos;
			float dist_sq;
			
			GetOldPlayerPosition(target, targetpos);
			VectorSubtract(self->s.origin, targetpos, diff);
			dist_sq = VectorLengthSquared(diff);

			if (dist_sq < best_dist_sq)
			{
				if (bi.IsEnemy(self, target))
				{
					vec3_t viewpos;
					trace_t trace;

					// Trace from the bot's eye position to the enemy's position + some randomization (so if only part of the enemy is showing, the bot might see it sometimes, but not all the time).
					VectorCopy(self->s.origin, viewpos);
					viewpos[2] += bi.GetViewHeight(self);
					targetpos[2] += bi.GetViewHeight(target) + nu_rand(8) - nu_rand(16);
					targetpos[0] += nu_rand(14) - nu_rand(14);
					targetpos[1] += nu_rand(14) - nu_rand(14);
					trace = bi.trace(viewpos, vec_zero, vec_zero, targetpos, self, MASK_SOLID);

					// TODO: Add smoke grenade checks here.

					if (trace.fraction == 1.0f || trace.ent == target)
					{
						best_dist_sq = dist_sq;
						best_target = target;
					}
				}
			}
		}

		target = bi.GetNextLivePlayerEnt(target, false);
	}
	
	// By default, don't shoot.
	movement->shooting = false;

	if (best_target)
	{
		float dist_to_target;
		vec3_t target_pos;
		vec3_t to_target;
		vec3_t best_target_predicted_location;
		float rand_time, rand_min, rand_max;

		// Distance to target
		GetOldPlayerPosition(best_target, target_pos);
		VectorSubtract(self->s.origin, target_pos, to_target);
		dist_to_target = VectorLength(to_target);
		rand_max = 0.2f + dist_to_target / 3000.0f;
		rand_min = 80.0f / dist_to_target;

		if (rand_max < 0.2f)
			rand_max = 0.2f;

		if (rand_min > 0.2f || dist_to_target < 0.01f)
			rand_min = 0.2f;

		rand_time = nu_rand(rand_max) - nu_rand(rand_min) + nu_rand(0.5); // randomly lead targets a bit, based on distance. + compensate for latency.
		//bi.dprintf("r: %g, min: %g, max: %g\n", rand_time, rand_min, rand_max);

		if (movement->aim_target == best_target)
		{
			// same target as last update.  Calculate velocity between old and new position.
			vec3_t pos_diff;
			vec3_t predicted_velocity;

			VectorSubtract(target_pos, movement->last_target_pos, pos_diff);
			VectorScale(pos_diff, 1000.0f / (float)msec, predicted_velocity); // TODO: compensate for bot's own velocity as well.
			VectorMA(target_pos, rand_time, predicted_velocity, best_target_predicted_location);
		}
		else
		{
			// New target, just aim directly toward it
			VectorCopy(target_pos, best_target_predicted_location);
			movement->aim_target = best_target;
		}

		best_target_predicted_location[2] += nu_rand(48.0f) - nu_rand(48.0f); // add some randomization to the verticality to maybe hit crouching/jumping targets by chance.
		BotSetDesiredAimAnglesFromPoint(botindex, best_target_predicted_location);
		DrawDebugSphere(best_target_predicted_location, 20.0f, 1.0f, 0.5f, 0.1f, 0.2f, -1);
		VectorCopy(target_pos, movement->last_target_pos);
		movement->last_target_msec = 0;

		// Shoot if we're aiming close enough to our desired target.
		if (movement->shooting_commit_time <= 0.0)
		{
			if (skill->value > -1)
			{
				float r;
				float r_max = 180.0f;
				float yaw_diff, pitch_diff;
			
				// Find difference between desired aim and current aim.
				yaw_diff = fabs(AngleTo180(self->s.angles[YAW] - movement->desired_angles[YAW]));
				pitch_diff = fabs(AngleTo180(self->s.angles[PITCH] * 3.0f - movement->desired_angles[PITCH]));

				// The further away something is, the more precise we want to be.  Somewhat arbitrary -- just tweaked until it felt about right.
				if (dist_to_target > 32.0f)
					r_max /= (dist_to_target / 32.0f);

				// Pick a random value within r_max degrees.  If the bot is aiming within that threshold, shoot.
				r = nu_rand(r_max);

				if (yaw_diff < r)
				{
					movement->shooting_commit_time = nu_rand(2.0);
					movement->shooting = true;
				}

				//bi.dprintf("%s d: %3.3f, rm: %3.3f, r: %3.3f\n", movement->shooting ? "S" : " ", yaw_diff, r_max, r);
			}
		}
	}
	else if (movement->aim_target)
	{
		movement->last_target_msec += msec;

		// Aim at the last known enemy position for a while, so the bot doesn't instantly turn his back when line of sight is obscurred.
		if (nu_rand(movement->last_target_msec) < 2000.0f)
		{
			BotSetDesiredAimAnglesFromPoint(botindex, movement->last_target_pos);
		}
		else
		{
			movement->aim_target = NULL;
		}
	}

	movement->shooting_commit_time -= msec * 0.001f;

	if (movement->shooting_commit_time > 0.0)
	{
		movement->shooting = true;
	}

	BotAimTowardDesiredAngles(botindex, msec);
}


// Aim at a target position (ex: flag for defensive position).  Note this will be overridden if enemies are in line of sight.
void BotAimAtPosition (int bot_index, const vec3_t target_pos)
{
	botmovedata_t *movement = bots.movement + bot_index;

	VectorCopy(target_pos, movement->last_target_pos);
	BotSetDesiredAimAnglesFromPoint(bot_index, movement->last_target_pos);
}


void BotStopMoving (int bot_index)
{
	botmovedata_t *movement = bots.movement + bot_index;
	movement->stop = true;
	movement->waypoint_path.active = false;
}


// Called every server frame (.1s)
void UpdatePlayerPosHistory (int msec)
{
	int clientindex;
	int historycopyindex;

	// shift everything over
	for (historycopyindex = MAX_PLAYER_POS_HISTORY - 1; historycopyindex > 0; --historycopyindex)
	{
		memcpy(player_pos_history[historycopyindex], player_pos_history[historycopyindex - 1], sizeof(vec3_t) * maxclients);
	}

	// Put the new
	for (clientindex = 0; clientindex < maxclients; ++clientindex)
	{
		edict_t *ent = bi.GetPlayerEntity(clientindex);

		if (ent && bi.CanInteract(ent))
		{
			VectorCopy(ent->s.origin, player_pos_history[0][clientindex]);
		}
	}
}
