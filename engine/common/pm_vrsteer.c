/*
pm_vrsteer.c - VR walking: the direction of the walk follows the stick at once
Cry of Fear VR project.
Copyright (C) 2026 contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Why: the game's walking (PM_Accelerate / PM_Friction in the game's own pm_shared) only adds speed along the direction you
ask for. The speed you already have in another direction is not taken away, only worn down by friction, slowly. So when the
direction of the stick changes (turning the stick around, sweeping it through a circle, letting go and pushing again) you
keep sliding the old way for a moment: the walk is late, and it trails behind the stick by 45-90 degrees during a fast
circle. Raising friction cures that, but it slows you down, most when crouched.

What this does: right before the game's own walking runs, the speed you have across the new direction is let go of
quickly (time constant vr_move_turn), and when the stick is let go the walk stops quickly (vr_move_brake). The game's
walking then runs as always on top of that: top speed, crouch speed, sprint, stairs and slopes are all the game's own.

It runs in exactly the same way in the client's prediction and on the server, from the same command, so the two agree.
*/
#include "common.h"
#include "const.h"
#include "pm_local.h"

void PM_VRSteer( playermove_t *pm )
{
	static convar_t *cv_active, *cv_turn, *cv_brake;
	float turn, brake, dt, fmove, smove, wishlen;
	float vx, vy, speed;

	if( !pm )
		return;

	if( !cv_active ) cv_active = Cvar_FindVar( "vr_in_active" );
	if( !cv_turn ) cv_turn = Cvar_FindVar( "vr_move_turn" );
	if( !cv_brake ) cv_brake = Cvar_FindVar( "vr_move_brake" );

	if( !cv_active || !cv_turn || !cv_brake || cv_active->value <= 0.0f )
		return;

	turn = cv_turn->value;
	brake = cv_brake->value;

	if( turn <= 0.0f && brake <= 0.0f )
		return;

	// only the plain walk on the ground: not ladders, water, air, trains, the dead, spectators, noclip or frozen
	if( pm->movetype != MOVETYPE_WALK || pm->onground == -1 || pm->deadflag != 0 || pm->spectator || pm->waterlevel >= 2 )
		return;

	if( FBitSet( pm->flags, FL_FROZEN|FL_ONTRAIN ))
		return;

	dt = pm->frametime;

	if( dt <= 0.0f || dt > 0.25f )
		return;

	vx = pm->velocity[0];
	vy = pm->velocity[1];
	speed = sqrt( vx * vx + vy * vy );

	if( speed < 1.0f )
		return;

	fmove = pm->cmd.forwardmove;
	smove = pm->cmd.sidemove;
	wishlen = sqrt( fmove * fmove + smove * smove );

	if( wishlen < 2.0f )
	{
		// let go of the stick: stop quickly. (Pushed by a conveyor or something moving: leave it to the game)
		if( brake > 0.0f && VectorIsNull( pm->basevelocity ))
		{
			float k = exp( -dt / brake );

			pm->velocity[0] = vx * k;
			pm->velocity[1] = vy * k;

			if( speed * k < 3.0f )
				pm->velocity[0] = pm->velocity[1] = 0.0f;
		}

		return;
	}

	if( turn > 0.0f )
	{
		// the direction the game will walk along: the command, turned by the view yaw (the game ignores the pitch when walking)
		float yaw = DEG2RAD( pm->cmd.viewangles[YAW] );
		float fx = cos( yaw ), fy = sin( yaw );
		float rx = fy, ry = -fx;
		float wx = fx * fmove + rx * smove;
		float wy = fy * fmove + ry * smove;
		float wl = sqrt( wx * wx + wy * wy ), par, k;

		if( wl < 0.001f )
			return;

		wx /= wl;
		wy /= wl;

		// the part of the speed along that direction stays (when it is forwards), everything across it, and speed
		// backwards, is let go of
		par = vx * wx + vy * wy;
		k = exp( -dt / turn );

		{
			float perp_x = vx - par * wx, perp_y = vy - par * wy;
			float keep = ( par > 0.0f ) ? par : par * k;

			pm->velocity[0] = keep * wx + perp_x * k;
			pm->velocity[1] = keep * wy + perp_y * k;
		}
	}
}
