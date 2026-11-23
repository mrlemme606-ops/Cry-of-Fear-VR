/*
in_vr_melee.c - VR melee: what the engine does with the events of the weapon colliders
Cry of Fear VR project.
Copyright (C) 2026 contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

The renderer (ref/gl/gl_vr_melee.c) sweeps the weapon colliders and tells the engine what happened with the command

	vr_melee_event <type> <hand> <class> <entity> <x> <y> <z> <dx> <dy> <dz> <speed>

type 0: the weapon hit an entity (d = direction of the hit, speed = m/s into the surface)
type 1: a swing (the tip of the weapon is fast), for the swing sound
type 2: the weapon hit the world (d = surface normal)

A hit hurts exactly that entity: the damage is the one the game has in its skill.cfg for the weapon (sk_cof_axe1..4 and the like,
the same numbers the game's own melee uses), scaled by how fast the weapon was moving. The hit itself is made by
SV_VRMeleeHit (server/sv_vrmelee.c), which calls the game's own TraceAttack and TakeDamage of the entity.
*/
#include "common.h"
#include "client.h"
#include "const.h"

extern int SV_VRMeleeHit( int entnum, const vec3_t point, const vec3_t dir, float damage, int bits, qboolean effects );

static CVAR_DEFINE_AUTO( vr_melee_enable_dmg, "1", FCVAR_ARCHIVE, "1 = a hit of a weapon collider hurts what it hits (0 = the weapons only collide and make sounds)" );
static CVAR_DEFINE_AUTO( vr_melee_fx, "1", FCVAR_ARCHIVE, "1 = hits go through the game's TraceAttack (blood, hit groups, breakable sounds). 0 = plain TakeDamage" );
static CVAR_DEFINE_AUTO( vr_melee_dmg_scale, "1", FCVAR_ARCHIVE, "damage of all hits of the weapon colliders, as a multiplier" );
static CVAR_DEFINE_AUTO( vr_melee_debug_events, "0", 0, "1 = print every hit and swing of the weapon colliders" );

// the classes of gl_vr_melee.c: 1 axe, 2 branch, 3 nightstick, 4 sledgehammer, 5 switchblade, 6 fists, 7 pistol whip, 8 other melee
// damage: the skill.cfg variable of the game, with the skill (1..4) added; fallback when the game does not have it.
// bits: the damage types the game's own code of that weapon uses
typedef struct
{
	const char	*skill_var;
	float		fallback;
	int		bits;
	const char	*swing;
	const char	*body[2];
	const char	*wall[2];
} vr_melee_class_t;

#define DMG_STD		67112960
#define DMG_SLEDGE	218107904

static const vr_melee_class_t vr_melee_classes[] =
{
	{ NULL, 0, 0, NULL, { NULL, NULL }, { NULL, NULL } },	// 0: none
	{ "sk_cof_axe", 50.0f, DMG_STD, "weapons/axe/axe_swing.wav", { "weapons/axe/axe_hitbody.wav", NULL }, { "weapons/axe/axe_hit.wav", NULL } },
	{ "sk_cof_branch", 25.0f, DMG_STD, "weapons/branch/branch_swing.wav", { "weapons/branch/branch_hit.wav", NULL }, { "weapons/branch/branch_hit.wav", NULL } },
	{ "sk_cof_nightstick", 25.0f, DMG_STD, "weapons/nightstick/nightstick_swing.wav", { "weapons/nightstick/nightstick_hitbody1.wav", "weapons/nightstick/nightstick_hitbody2.wav" }, { "weapons/nightstick/nightstick_hitwall1.wav", "weapons/nightstick/nightstick_hitwall2.wav" } },
	{ "sk_cof_sledgehammer", 80.0f, DMG_SLEDGE, "weapons/axe/axe_swing.wav", { "weapons/sledgehammer/sledgehammer_hitbody.wav", NULL }, { "weapons/sledgehammer/sledgehammer_hit.wav", NULL } },
	{ "sk_cof_switchblade", 20.0f, DMG_STD, "weapons/switchblade/switchblade_swing.wav", { "weapons/switchblade/switchblade_hitbody1.wav", "weapons/switchblade/switchblade_hitbody2.wav" }, { "weapons/switchblade/switchblade_hitwall1.wav", "weapons/switchblade/switchblade_hitwall2.wav" } },
	{ "sk_cof_punch", 10.0f, DMG_STD, "weapons/mobile/punch_swing.wav", { "weapons/mobile/punch_smack.wav", NULL }, { "weapons/mobile/punch_smack.wav", NULL } },
	{ "sk_cof_weapon_melee", 10.0f, DMG_STD, "weapons/mobile/punch_swing.wav", { "weapons/mobile/punch_smack.wav", NULL }, { "weapons/mobile/punch_smack.wav", NULL } },
	{ "sk_cof_weapon_melee", 15.0f, DMG_STD, "weapons/axe/axe_swing.wav", { "weapons/axe/axe_hitbody.wav", NULL }, { "weapons/axe/axe_hit.wav", NULL } },
};

static float IN_VRMeleeBaseDamage( const vr_melee_class_t *c )
{
	char name[64];
	int level = (int)Cvar_VariableValue( "difficulty" );

	// the same clamp the game does (skill 1..4)
	level = bound( 1, level, 4 );
	Q_snprintf( name, sizeof( name ), "%s%d", c->skill_var, level );

	if( Cvar_FindVar( name ))
		return Cvar_VariableValue( name );

	return c->fallback;
}

static void IN_VRMeleePlay( const char *const list[2], const vec3_t pos, int ent )
{
	const char *name = list[0];

	if( list[1] && COM_RandomLong( 0, 1 ))
		name = list[1];

	if( !name )
		return;

	S_StartSound( pos, ent, CHAN_BODY, S_RegisterSound( name ), 1.0f, ATTN_NORM, 96 + COM_RandomLong( 0, 8 ), 0 );
}

static void IN_VRMeleeEvent_f( void )
{
	int type, hand, kind, ent;
	vec3_t p, d;
	float speed, need, full, t, damage;
	const vr_melee_class_t *c;
	int result;

	if( Cmd_Argc() < 12 )
		return;

	if( cls.state != ca_active || cl.paused || cls.key_dest != key_game || !Cvar_VariableValue( "vr_melee_enable" ))
		return;

	type = Q_atoi( Cmd_Argv( 1 ));
	hand = Q_atoi( Cmd_Argv( 2 ));
	kind = Q_atoi( Cmd_Argv( 3 ));
	ent = Q_atoi( Cmd_Argv( 4 ));
	p[0] = Q_atof( Cmd_Argv( 5 )); p[1] = Q_atof( Cmd_Argv( 6 )); p[2] = Q_atof( Cmd_Argv( 7 ));
	d[0] = Q_atof( Cmd_Argv( 8 )); d[1] = Q_atof( Cmd_Argv( 9 )); d[2] = Q_atof( Cmd_Argv( 10 ));
	speed = Q_atof( Cmd_Argv( 11 ));

	if( kind < 1 || kind >= (int)ARRAYSIZE( vr_melee_classes ))
		return;

	c = &vr_melee_classes[kind];

	if( vr_melee_debug_events.value )
		Con_Printf( "VR melee: type %d, hand %d, class %d, entity %d, %.1f m/s\n", type, hand, kind, ent, speed );

	// no hitting and no swing sounds while you reload by hand
	if( Cvar_VariableValue( "vr_in_noswing" ) > 0.5f )
		return;

	if( type == 1 )
	{
		if( c->swing )
			S_StartLocalSound( c->swing, 0.9f, false );
		return;
	}

	if( type == 2 )
	{
		IN_VRMeleePlay( c->wall, p, 0 );
		return;
	}

	// a hit on an entity: the damage of the weapon, scaled by the speed of the hit
	need = ( kind == 7 ) ? Cvar_VariableValue( "vr_hb_whipspeed" ) : Cvar_VariableValue( "vr_hb_minspeed" );
	full = Q_max( need + 0.1f, Cvar_VariableValue( "vr_hb_fullspeed" ));
	t = bound( 0.0f, ( speed - need ) / ( full - need ), 1.0f );
	damage = IN_VRMeleeBaseDamage( c ) * ( Cvar_VariableValue( "vr_hb_mindmg" ) + ( 1.0f - Cvar_VariableValue( "vr_hb_mindmg" )) * t );
	damage *= vr_melee_dmg_scale.value;

	if( hand == 1 )
		damage *= Cvar_VariableValue( "vr_hb_dmg" );

	if( !vr_melee_enable_dmg.value || damage <= 0.0f )
	{
		IN_VRMeleePlay( c->wall, p, ent );
		return;
	}

	result = SV_VRMeleeHit( ent, p, d, damage, c->bits, vr_melee_fx.value != 0.0f );

	if( vr_melee_debug_events.value )
		Con_Printf( "VR melee: hit entity %d for %.1f damage, result %d\n", ent, damage, result );

	if( result == 1 )
		IN_VRMeleePlay( c->body, p, ent );
	else if( result >= 0 )
		IN_VRMeleePlay( c->wall, p, ent );
}

void IN_VRMeleeInit( void )
{
	Cvar_RegisterVariable( &vr_melee_enable_dmg );
	Cvar_RegisterVariable( &vr_melee_fx );
	Cvar_RegisterVariable( &vr_melee_dmg_scale );
	Cvar_RegisterVariable( &vr_melee_debug_events );
	Cmd_AddCommand( "vr_melee_event", IN_VRMeleeEvent_f, "(internal) a hit or a swing of a weapon collider" );
}
