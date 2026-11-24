/*
gl_vr_melee.c - VR native melee: weapon colliders that live in the world
Cry of Fear VR project.
Copyright (C) 2026 contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

How it works (this replaces the old "fast hand = press the attack key" melee):

- Every weapon in a hand has a collider: an oriented box (vr_hb_* for the right hand, vr_hbl_* for the left hand weapon of
  a dual wield). The box is placed in the weapon's own axes, measured from the point where your hand holds it, so it moves
  and turns with the weapon exactly as the weapon is drawn. Fists use a small box at the hand. Every weapon (guns too,
  for pistol whipping) has its own box, saved with vr_weapon_save / vr_weapon_save_left.
- Every frame the box is swept from where it was a frame ago to where it is now (a lattice of sample points on the box, each
  one traced along its own path), so nothing can be skipped, however fast the swing is. The first thing the box runs
  into is the hit: a monster, a plank, a breakable, a door, or the world.
- A hit on an entity is sent to the engine (command vr_melee_event). The engine makes the game's own TraceAttack / TakeDamage
  call on exactly that entity, with the weapon's own damage from skill.cfg, scaled by how fast the box was moving.
  Nothing that is not touched takes damage, and a touch that is too slow does nothing but bounce.
- The weapon cannot go through walls, doors, planks or monsters: the samples are traced from your hand, and what would be
  inside something is pushed back. Only what you see moves (the controller goes on, and so does the aim of a gun).
- The first time a weapon is held without a saved box, the box is fitted to the weapon's own mesh. Tune it with the cvars
  and look at it with vr_hb_debug 1, then vr_weapon_save keeps it.
*/
#include "gl_vr.h"
#include "xash3d_mathlib.h"
#include "pm_defs.h"

// -------- cvars: the box of the weapon in the right hand (saved per weapon) --------
// centre of the box, measured from your hand along the weapon's own forward / left / up axes (units)
CVAR_DEFINE_AUTO( vr_hb_x, "0", FCVAR_ARCHIVE, "weapon collider: centre, units forward of the hand (the weapon's own axes). Saved per weapon with vr_weapon_save" );
CVAR_DEFINE_AUTO( vr_hb_y, "0", FCVAR_ARCHIVE, "weapon collider: centre, units to the left of the hand" );
CVAR_DEFINE_AUTO( vr_hb_z, "0", FCVAR_ARCHIVE, "weapon collider: centre, units above the hand" );
// full size of the box along those axes. 0 = not set yet: the box is fitted to the weapon's mesh the first time it is held
CVAR_DEFINE_AUTO( vr_hb_sx, "0", FCVAR_ARCHIVE, "weapon collider: size along the weapon (units). 0 = fit to the weapon's mesh automatically" );
CVAR_DEFINE_AUTO( vr_hb_sy, "0", FCVAR_ARCHIVE, "weapon collider: size sideways (units)" );
CVAR_DEFINE_AUTO( vr_hb_sz, "0", FCVAR_ARCHIVE, "weapon collider: size up/down (units)" );
CVAR_DEFINE_AUTO( vr_hb_pitch, "0", FCVAR_ARCHIVE, "weapon collider: extra rotation, pitch degrees (around the weapon's left axis)" );
CVAR_DEFINE_AUTO( vr_hb_yaw, "0", FCVAR_ARCHIVE, "weapon collider: extra rotation, yaw degrees (around the weapon's up axis)" );
CVAR_DEFINE_AUTO( vr_hb_roll, "0", FCVAR_ARCHIVE, "weapon collider: extra rotation, roll degrees (around the weapon's forward axis)" );
CVAR_DEFINE_AUTO( vr_hb_dmg, "1", FCVAR_ARCHIVE, "weapon collider: damage multiplier of this weapon (1 = the game's own damage)" );

// the same for the weapon in the left hand of a dual wield (saved with vr_weapon_save_left)
CVAR_DEFINE_AUTO( vr_hbl_x, "0", FCVAR_ARCHIVE, "left weapon collider: centre, units forward of the left hand. Saved with vr_weapon_save_left" );
CVAR_DEFINE_AUTO( vr_hbl_y, "0", FCVAR_ARCHIVE, "left weapon collider: centre, units to the left" );
CVAR_DEFINE_AUTO( vr_hbl_z, "0", FCVAR_ARCHIVE, "left weapon collider: centre, units up" );
CVAR_DEFINE_AUTO( vr_hbl_sx, "0", FCVAR_ARCHIVE, "left weapon collider: size along the weapon (units). 0 = a default for the kind of weapon" );
CVAR_DEFINE_AUTO( vr_hbl_sy, "0", FCVAR_ARCHIVE, "left weapon collider: size sideways" );
CVAR_DEFINE_AUTO( vr_hbl_sz, "0", FCVAR_ARCHIVE, "left weapon collider: size up/down" );
CVAR_DEFINE_AUTO( vr_hbl_pitch, "0", FCVAR_ARCHIVE, "left weapon collider: extra pitch degrees" );
CVAR_DEFINE_AUTO( vr_hbl_yaw, "0", FCVAR_ARCHIVE, "left weapon collider: extra yaw degrees" );
CVAR_DEFINE_AUTO( vr_hbl_roll, "0", FCVAR_ARCHIVE, "left weapon collider: extra roll degrees" );

// whether this weapon hurts what it hits: -1 = by its kind (melee weapons, fists and guns do, phones, flares and the like do not), 0 = never (it only collides),
// 1 = always. Saved per weapon
CVAR_DEFINE_AUTO( vr_hb_melee, "-1", FCVAR_ARCHIVE, "this weapon: -1 = by its kind, 0 = only collides (never hurts), 1 = hurts what it hits. Saved per weapon" );

// the left fist (a free left hand that can punch): one box for it, not per weapon (saved in your config)
static CVAR_DEFINE_AUTO( vr_hb_lf_x, "0", FCVAR_ARCHIVE, "left fist collider: centre, units forward of the hand (the fist's own axes)" );
static CVAR_DEFINE_AUTO( vr_hb_lf_y, "0", FCVAR_ARCHIVE, "left fist collider: centre, units to the left" );
static CVAR_DEFINE_AUTO( vr_hb_lf_z, "0", FCVAR_ARCHIVE, "left fist collider: centre, units up" );
static CVAR_DEFINE_AUTO( vr_hb_lf_sx, "0", FCVAR_ARCHIVE, "left fist collider: size along the fist (units). 0 = the default (vr_hb_fist_size)" );
static CVAR_DEFINE_AUTO( vr_hb_lf_sy, "0", FCVAR_ARCHIVE, "left fist collider: size sideways" );
static CVAR_DEFINE_AUTO( vr_hb_lf_sz, "0", FCVAR_ARCHIVE, "left fist collider: size up/down" );
static CVAR_DEFINE_AUTO( vr_hb_lf_pitch, "0", FCVAR_ARCHIVE, "left fist collider: extra pitch degrees" );
static CVAR_DEFINE_AUTO( vr_hb_lf_yaw, "0", FCVAR_ARCHIVE, "left fist collider: extra yaw degrees" );
static CVAR_DEFINE_AUTO( vr_hb_lf_roll, "0", FCVAR_ARCHIVE, "left fist collider: extra roll degrees" );

// -------- cvars: how the system behaves --------
static CVAR_DEFINE_AUTO( vr_hb_enable, "1", FCVAR_ARCHIVE, "1 = weapon colliders hit and block. 0 = melee off" );
static CVAR_DEFINE_AUTO( vr_hb_debug, "0", 0, "1 = draw the weapon colliders (cyan = free, yellow = blocked by the world, red = just hit). 2 = also the sample points and the pushed-back box" );
static CVAR_DEFINE_AUTO( vr_hb_collide, "1", FCVAR_ARCHIVE, "1 = weapons cannot go through walls, doors, planks and monsters (only what you see is pushed back)" );
static CVAR_DEFINE_AUTO( vr_hb_bbox, "1", FCVAR_ARCHIVE, "1 = monsters are hit by their bounding box (forgiving). 0 = by their exact hitboxes" );
static CVAR_DEFINE_AUTO( vr_hb_minspeed, "1.6", FCVAR_ARCHIVE, "m/s the box has to move at to hurt something with a melee weapon or fists. Slower touches only bounce" );
static CVAR_DEFINE_AUTO( vr_hb_whipspeed, "2.6", FCVAR_ARCHIVE, "m/s the box has to move at to hurt something when pistol whipping" );
static CVAR_DEFINE_AUTO( vr_hb_fullspeed, "4.5", FCVAR_ARCHIVE, "m/s at which a hit does the weapon's full damage (slower hits do less, down to vr_hb_mindmg)" );
static CVAR_DEFINE_AUTO( vr_hb_mindmg, "0.4", FCVAR_ARCHIVE, "fraction of the damage a hit at the minimum speed does" );
static CVAR_DEFINE_AUTO( vr_hb_rehit, "0.45", FCVAR_ARCHIVE, "seconds before the same thing can be hit again by the same hand" );
static CVAR_DEFINE_AUTO( vr_hb_step, "3", FCVAR_ARCHIVE, "distance between the sample points on the collider (units). Smaller = more exact, more traces" );
static CVAR_DEFINE_AUTO( vr_hb_bounce, "1", FCVAR_ARCHIVE, "how hard the weapon bounces back from what it hits (0 = not at all)" );
static CVAR_DEFINE_AUTO( vr_hb_maxpush, "30", FCVAR_ARCHIVE, "how far a weapon can be pushed back by a wall, units" );
static CVAR_DEFINE_AUTO( vr_hb_haptic, "1", FCVAR_ARCHIVE, "1 = the hand vibrates when the weapon hits something" );
static CVAR_DEFINE_AUTO( vr_hb_wallspeed, "1.3", FCVAR_ARCHIVE, "m/s at which hitting the world makes a sound (0 = never)" );
static CVAR_DEFINE_AUTO( vr_hb_swingspeed, "2.6", FCVAR_ARCHIVE, "m/s at which the tip of a melee weapon makes its swing sound" );
static CVAR_DEFINE_AUTO( vr_hb_nodmg_models, "mobile+flare+lantern+camera+syringe+pills+cassette+flashlight+lighter+bandage", FCVAR_ARCHIVE, "items that block but never hurt (parts of the model name, separated by +). Every other gun can pistol whip" );
static CVAR_DEFINE_AUTO( vr_hb_handpush, "0.6", FCVAR_ARCHIVE, "how much the colliders of the two hands push each other apart (0 = they do not touch each other, 1 = each moves half the overlap)" );
static CVAR_DEFINE_AUTO( vr_hb_handmax, "5", FCVAR_ARCHIVE, "the most a hand is pushed by the other hand, units" );
static CVAR_DEFINE_AUTO( vr_hb_fist_size, "5", FCVAR_ARCHIVE, "size of the box of a fist (units)" );
static CVAR_DEFINE_AUTO( vr_hb_fist_fwd, "4", FCVAR_ARCHIVE, "how far in front of the hand the box of a fist sits (units)" );

// weapon classes (the engine turns them into damage, sounds and the like: engine/client/input/in_vr_melee.c)
enum { HB_NONE = 0, HB_AXE = 1, HB_BRANCH, HB_STICK, HB_SLEDGE, HB_BLADE, HB_FIST, HB_WHIP, HB_OTHER, HB_BLOCKONLY = 99 };

typedef struct
{
	qboolean	valid;
	vec3_t		c;		// centre
	vec3_t		ax[3];		// axes (unit)
	float		h[3];		// half sizes
	int		cls;
} hb_box_t;

#define HB_MAX_SAMPLES	96
#define HB_REHIT_SLOTS	8

typedef struct
{
	hb_box_t	cur, prev;
	qboolean	prev_ok;
	double		prev_time;
	vec3_t		head_prev;
	vec3_t		grip;
	vec3_t		push;		// where the box has to go to leave the walls (this frame)
	vec3_t		push_vis;	// the same, smoothed
	vec3_t		bounce, bounce_vel;
	vec3_t		hpush;		// pushed by the other hand (smoothed)
	double		flash_until;
	double		block_until;
	double		swing_next;
	qboolean	swing_armed;
	double		wall_next;
	struct { int ent; double until; } rehit[HB_REHIT_SLOTS];
	vec3_t		samples[HB_MAX_SAMPLES];
	int		num_samples;
	char		model[48];
	int		fit_tries;
} hb_hand_t;

static hb_hand_t hb[2];	// 0 = left, 1 = right

static qboolean hb_fit_wanted;
static qboolean hb_fit_done;	// for the current right hand weapon
static int hb_fit_n;
static vec3_t hb_fit_min, hb_fit_max;
static char hb_fit_model[48];
static int hb_fit_attempts;

// ---------------------------------------------------------------------------------------------------------------------
// small helpers
// ---------------------------------------------------------------------------------------------------------------------
static void HB_RotateAxis( vec3_t v, const vec3_t axis, float degrees )
{
	float rad = DEG2RAD( degrees ), c = cos( rad ), s = sin( rad );
	vec3_t cr, out;
	float d = DotProduct( axis, v );

	CrossProduct( axis, v, cr );
	out[0] = v[0] * c + cr[0] * s + axis[0] * d * ( 1.0f - c );
	out[1] = v[1] * c + cr[1] * s + axis[1] * d * ( 1.0f - c );
	out[2] = v[2] * c + cr[2] * s + axis[2] * d * ( 1.0f - c );
	VectorCopy( out, v );
}

static qboolean HB_NameHas( const char *name, const char *frag )
{
	size_t n = Q_strlen( frag ), i, len = Q_strlen( name );

	if( !n || n > len )
		return false;

	for( i = 0; i + n <= len; i++ )
	{
		if( !Q_strnicmp( name + i, frag, n ))
			return true;
	}

	return false;
}

static qboolean HB_InList( const char *list, const char *name )
{
	const char *p = list;

	while( *p )
	{
		char tok[48];
		int n = 0;

		while( *p == '+' || *p == ' ' ) p++;

		while( *p && *p != '+' && *p != ' ' && n < 47 )
			tok[n++] = *p++;

		tok[n] = 0;

		if( n && HB_NameHas( name, tok ))
			return true;
	}

	return false;
}

static float HB_Cvar( const char *name )
{
	return gEngfuncs.pfnGetCvarFloat( name );
}

static int HB_ClassOf( const char *base )
{
	if( !base || !base[0] )
		return HB_NONE;

	if( HB_NameHas( base, "v_actions" ) || HB_NameHas( base, "fist" ))
		return HB_FIST;
	if( HB_NameHas( base, "nightstick" ))
		return HB_STICK;
	if( HB_NameHas( base, "sledge" ))
		return HB_SLEDGE;
	if( HB_NameHas( base, "switchblade" ) || HB_NameHas( base, "knife" ))
		return HB_BLADE;
	if( HB_NameHas( base, "branch" ))
		return HB_BRANCH;
	if( HB_NameHas( base, "axe" ))
		return HB_AXE;
	if( VR_IsMeleeModel( base ))
		return HB_OTHER;
	if( HB_InList( vr_hb_nodmg_models.string, base ))
		return HB_BLOCKONLY;

	return HB_WHIP;
}

static void HB_ModelBase( const char *modelname, char *out, size_t size )
{
	const char *base = modelname, *p;
	size_t n = 0;

	for( p = modelname; *p; p++ )
	{
		if( *p == '/' || *p == '\\' )
			base = p + 1;
	}

	while( *base && *base != '.' && n < size - 1 )
		out[n++] = *base++;

	out[n] = 0;
}

static qboolean HB_IsMelee( int cls )
{
	return cls != HB_NONE && cls != HB_WHIP && cls != HB_BLOCKONLY;
}

// ---------------------------------------------------------------------------------------------------------------------
// building the box of a hand
// ---------------------------------------------------------------------------------------------------------------------
static void HB_DefaultBox( int cls, qboolean left, float *center, float *size )
{
	// used when nothing is set (left hand weapons, and when a mesh fit is not possible): a thin box in front of the hand
	if( cls == HB_FIST || cls == HB_NONE )
	{
		float s = bound( 2.0f, vr_hb_fist_size.value, 20.0f );

		VectorSet( center, vr_hb_fist_fwd.value, 0.0f, 0.0f );
		VectorSet( size, s, s, s );
	}
	else if( HB_IsMelee( cls ))
	{
		VectorSet( center, 9.0f, 0.0f, 0.0f );
		VectorSet( size, 18.0f, 3.0f, 3.0f );
	}
	else
	{
		VectorSet( center, 4.0f, 0.0f, 1.0f );
		VectorSet( size, 10.0f, 2.5f, 5.0f );
	}
}

// ---- the box of a weapon in the left hand of a dual wield: the box of the same weapon in the right hand, mirrored ----
// The last boxes this session of the weapons that were in the right hand (fitted or tuned), and the saved ones (vr_weapon_save)
#define HB_CACHE	16

static struct { char base[48]; float v[9]; } hb_cache[HB_CACHE];
static int hb_cache_n;

static void HB_CacheStore( const char *base )
{
	float v[9];
	int i;

	if( !base[0] || vr_hb_sx.value <= 0.01f || vr_hb_sy.value <= 0.01f || vr_hb_sz.value <= 0.01f )
		return;

	v[0] = vr_hb_x.value; v[1] = vr_hb_y.value; v[2] = vr_hb_z.value;
	v[3] = vr_hb_sx.value; v[4] = vr_hb_sy.value; v[5] = vr_hb_sz.value;
	v[6] = vr_hb_pitch.value; v[7] = vr_hb_yaw.value; v[8] = vr_hb_roll.value;

	for( i = 0; i < hb_cache_n; i++ )
	{
		if( !Q_stricmp( hb_cache[i].base, base ))
			break;
	}

	if( i == hb_cache_n )
	{
		if( hb_cache_n >= HB_CACHE )
			i = HB_CACHE - 1;
		else
			hb_cache_n++;

		Q_strncpy( hb_cache[i].base, base, sizeof( hb_cache[i].base ));
	}

	memcpy( hb_cache[i].v, v, sizeof( v ));
}

static qboolean HB_FileNumber( const char *text, const char *key, float *out )
{
	const char *p = text;
	size_t n = Q_strlen( key );

	while( p && *p )
	{
		if( !Q_strncmp( p, key, n ) && ( p[n] == ' ' || p[n] == '\t' ))
		{
			p += n;

			while( *p == ' ' || *p == '\t' || *p == '"' ) p++;

			*out = Q_atof( p );
			return true;
		}

		while( *p && *p != '\n' ) p++;
		while( *p == '\n' || *p == '\r' ) p++;
	}

	return false;
}

// false = no box known for that weapon (or it is one of those with its own position in the left hand)
static qboolean HB_MirrorBox( const char *base, float *out )
{
	static char file_base[48];
	static float file_v[9];
	static qboolean file_ok;
	static double file_time = -100.0;
	double now = gp_host ? gp_host->realtime : 0.0;
	int i;

	// the switchblade and the branch are held differently in the left hand: their boxes are set by hand (vr_hbl_*)
	if( HB_NameHas( base, "switchblade" ) || HB_NameHas( base, "knife" ) || HB_NameHas( base, "branch" ))
		return false;

	for( i = 0; i < hb_cache_n; i++ )
	{
		if( !Q_stricmp( hb_cache[i].base, base ))
		{
			memcpy( out, hb_cache[i].v, sizeof( float ) * 9 );
			return true;
		}
	}

	// the saved file of the weapon (read again every few seconds at most)
	if( Q_stricmp( file_base, base ) || now - file_time > 4.0 || now < file_time )
	{
		char name[96];
		fs_offset_t len = 0;
		char *text;
		static const char *keys[9] = { "vr_hb_x", "vr_hb_y", "vr_hb_z", "vr_hb_sx", "vr_hb_sy", "vr_hb_sz", "vr_hb_pitch", "vr_hb_yaw", "vr_hb_roll" };

		Q_strncpy( file_base, base, sizeof( file_base ));
		file_time = now;
		file_ok = false;
		Q_snprintf( name, sizeof( name ), "vr_weapon_%s.cfg", base );
		text = (char *)gEngfuncs.fsapi->LoadFileMalloc( name, &len, false );

		if( text )
		{
			qboolean all = true;

			for( i = 0; i < 9; i++ )
			{
				file_v[i] = 0.0f;

				if( !HB_FileNumber( text, keys[i], &file_v[i] ) && i >= 3 && i <= 5 )
					all = false;
			}

			file_ok = all && file_v[3] > 0.01f && file_v[4] > 0.01f && file_v[5] > 0.01f;
			free( text );
		}
	}

	if( file_ok )
	{
		memcpy( out, file_v, sizeof( file_v ));
		return true;
	}

	return false;
}

// the box of one hand in the world, from the hand and the weapon's axes. false = nothing to collide with this frame
static qboolean HB_BuildBox( int hand, hb_box_t *box )
{
	char base[48];
	vec3_t grip, f, l, u;
	float center[3], size[3], rot[3];
	const convar_t *cv[9];
	float mir[9];
	qboolean use_mirror = false;
	int cls, i;

	box->valid = false;

	if( hand == 1 )
	{
		const char *model = VR_CurrentWeaponModel();

		if( !model || !model[0] )
			return false;

		HB_ModelBase( model, base, sizeof( base ));
		cls = HB_ClassOf( base );

		if( !VR_WeaponBasis( 1, grip, f, l, u ))
			return false;

		cv[0] = &vr_hb_x; cv[1] = &vr_hb_y; cv[2] = &vr_hb_z; cv[3] = &vr_hb_sx; cv[4] = &vr_hb_sy; cv[5] = &vr_hb_sz;
		cv[6] = &vr_hb_pitch; cv[7] = &vr_hb_yaw; cv[8] = &vr_hb_roll;
	}
	else
	{
		char model[64];

		// the left hand grips the gun, or works its magazine or bolt: the collider of the weapon in the right hand is the only one
		if( VR_LeftGripping( ) || VR_TwoHandActive( ) || VR_ReloadLeftBusy( ))
			return false;

		if( VR_CofDualModel( model, sizeof( model )))
		{
			HB_ModelBase( model, base, sizeof( base ));
			cls = HB_ClassOf( base );

			if( !VR_WeaponBasis( 0, grip, f, l, u ))
				return false;

			cv[0] = &vr_hbl_x; cv[1] = &vr_hbl_y; cv[2] = &vr_hbl_z; cv[3] = &vr_hbl_sx; cv[4] = &vr_hbl_sy; cv[5] = &vr_hbl_sz;
			cv[6] = &vr_hbl_pitch; cv[7] = &vr_hbl_yaw; cv[8] = &vr_hbl_roll;

			// no box of its own set for the left hand: the box of the same weapon in the right hand, mirrored
			if( vr_hbl_sx.value <= 0.01f || vr_hbl_sy.value <= 0.01f || vr_hbl_sz.value <= 0.01f )
				use_mirror = HB_MirrorBox( base, mir );
		}
		else if( HB_Cvar( "vr_in_fists" ) > 0.5f || HB_Cvar( "vr_in_oneh" ) > 0.5f )
		{
			// the free left hand is a fist (when the right hand holds nothing or a one handed item). The box sits on the fist
			// that is drawn (the mirrored fist model), or on the hand when the universal left hand is shown instead
			if( VR_LeftFistWanted( ))
			{
				if( !VR_LeftFistBasis( grip, f, l, u ))
					return false;
			}
			else if( !VR_GetHandBasis( 0, grip, f, l, u ))
				return false;

			Q_strncpy( base, "left_fist", sizeof( base ));
			cls = HB_FIST;
			cv[0] = &vr_hb_lf_x; cv[1] = &vr_hb_lf_y; cv[2] = &vr_hb_lf_z; cv[3] = &vr_hb_lf_sx; cv[4] = &vr_hb_lf_sy; cv[5] = &vr_hb_lf_sz;
			cv[6] = &vr_hb_lf_pitch; cv[7] = &vr_hb_lf_yaw; cv[8] = &vr_hb_lf_roll;
		}
		else return false;
	}

	if( cls == HB_NONE )
		return false;

	// a weapon can be told to hurt or not to hurt (vr_hb_melee, saved per weapon). It collides either way
	if( hand == 1 )
	{
		if( vr_hb_melee.value >= 0.0f && vr_hb_melee.value < 0.5f )
			cls = HB_BLOCKONLY;
		else if( vr_hb_melee.value >= 0.5f && cls == HB_BLOCKONLY )
			cls = HB_WHIP;
	}

	if( cv[0] && cv[3]->value > 0.01f && cv[4]->value > 0.01f && cv[5]->value > 0.01f )
	{
		for( i = 0; i < 3; i++ )
		{
			center[i] = cv[i]->value;
			size[i] = cv[3 + i]->value;
			rot[i] = cv[6 + i]->value;
		}
	}
	else if( use_mirror )
	{
		// reflected across the middle of the body: sideways flips, so do yaw and roll
		VectorSet( center, mir[0], -mir[1], mir[2] );
		VectorSet( size, mir[3], mir[4], mir[5] );
		VectorSet( rot, mir[6], -mir[7], -mir[8] );
	}
	else
	{
		HB_DefaultBox( cls, hand == 0, center, size );
		VectorClear( rot );
	}

	// the centre is measured along the weapon's axes; the extra rotation of the box turns the box around its own centre
	VectorCopy( grip, box->c );
	VectorMA( box->c, center[0], f, box->c );
	VectorMA( box->c, center[1], l, box->c );
	VectorMA( box->c, center[2], u, box->c );

	HB_RotateAxis( f, u, rot[1] );
	HB_RotateAxis( l, u, rot[1] );
	HB_RotateAxis( f, l, rot[0] );
	HB_RotateAxis( u, l, rot[0] );
	HB_RotateAxis( l, f, rot[2] );
	HB_RotateAxis( u, f, rot[2] );

	VectorCopy( f, box->ax[0] );
	VectorCopy( l, box->ax[1] );
	VectorCopy( u, box->ax[2] );

	for( i = 0; i < 3; i++ )
		box->h[i] = Q_max( 0.4f, size[i] * 0.5f );

	box->cls = cls;
	box->valid = true;
	VectorCopy( grip, hb[hand].grip );
	Q_strncpy( hb[hand].model, base, sizeof( hb[hand].model ));
	return true;
}

// the lattice of sample points on the box, in the box's own coordinates (the same points every frame)
static void HB_MakeSamples( const hb_box_t *b, vec3_t *local, int *count )
{
	int n[3], i, j, k, total = 0, longest = 0;
	float step = bound( 1.0f, vr_hb_step.value, 12.0f );

	for( i = 1; i < 3; i++ )
	{
		if( b->h[i] > b->h[longest] )
			longest = i;
	}

	for( i = 0; i < 3; i++ )
	{
		float len = b->h[i] * 2.0f;
		int cap = ( i == longest ) ? 9 : 4;

		if( len < 2.5f )
			n[i] = 1;
		else
			n[i] = bound( 2, (int)ceil( len / step ) + 1, cap );
	}

	for( i = 0; i < n[0]; i++ )
	{
		for( j = 0; j < n[1]; j++ )
		{
			for( k = 0; k < n[2]; k++ )
			{
				if( total >= HB_MAX_SAMPLES )
					break;

				local[total][0] = ( n[0] > 1 ) ? ( -1.0f + 2.0f * i / ( n[0] - 1 )) * b->h[0] : 0.0f;
				local[total][1] = ( n[1] > 1 ) ? ( -1.0f + 2.0f * j / ( n[1] - 1 )) * b->h[1] : 0.0f;
				local[total][2] = ( n[2] > 1 ) ? ( -1.0f + 2.0f * k / ( n[2] - 1 )) * b->h[2] : 0.0f;
				total++;
			}
		}
	}

	*count = total;
}

static void HB_ToWorld( const hb_box_t *b, const vec3_t local, vec3_t out )
{
	VectorCopy( b->c, out );
	VectorMA( out, local[0], b->ax[0], out );
	VectorMA( out, local[1], b->ax[1], out );
	VectorMA( out, local[2], b->ax[2], out );
}

// ---------------------------------------------------------------------------------------------------------------------
// tracing
// ---------------------------------------------------------------------------------------------------------------------
static int HB_TraceFlags( void )
{
	return vr_hb_bbox.value ? PM_STUDIO_BOX : PM_NORMAL;
}

static qboolean HB_EntityNumber( const pmtrace_t *tr, int *ent )
{
	struct physent_s *pe;

	if( tr->ent < 0 )
		return false;

	pe = gEngfuncs.EV_GetPhysent( tr->ent );

	if( !pe )
		return false;

	*ent = ((physent_t *)pe)->info;
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// events to the engine
// ---------------------------------------------------------------------------------------------------------------------
// type 0 = hit an entity, 1 = swing, 2 = hit the world
static void HB_SendEvent( int type, int hand, int cls, int ent, const vec3_t p, const vec3_t n, float speed )
{
	char cmd[192];

	Q_snprintf( cmd, sizeof( cmd ), "vr_melee_event %d %d %d %d %.2f %.2f %.2f %.3f %.3f %.3f %.2f\n", type, hand, cls, ent,
		p[0], p[1], p[2], n[0], n[1], n[2], speed );
	gEngfuncs.Cbuf_AddText( cmd );
}

static void HB_Haptic( int hand, float speed, float scale )
{
	float k;

	if( !vr_hb_haptic.value )
		return;

	k = bound( 0.0f, ( speed - 1.0f ) / 5.0f, 1.0f ) * scale;
	gEngfuncs.Cvar_SetValue( "vr_in_haptic_len", 900.0f + k * 3000.0f );
	gEngfuncs.Cvar_SetValue( "vr_in_haptic", (float)( hand * 100 + ( k > 0.55f ? 2 : 1 )));
}

static qboolean HB_RehitBlocked( hb_hand_t *h, int ent, double now )
{
	int i;

	for( i = 0; i < HB_REHIT_SLOTS; i++ )
	{
		if( h->rehit[i].ent == ent && h->rehit[i].until > now )
			return true;
	}

	return false;
}

static void HB_RehitMark( hb_hand_t *h, int ent, double now )
{
	int i, slot = 0;
	double oldest = 1.0e30;

	for( i = 0; i < HB_REHIT_SLOTS; i++ )
	{
		if( h->rehit[i].ent == ent )
		{
			slot = i;
			break;
		}

		if( h->rehit[i].until < oldest )
		{
			oldest = h->rehit[i].until;
			slot = i;
		}
	}

	h->rehit[slot].ent = ent;
	h->rehit[slot].until = now + Q_max( 0.05f, vr_hb_rehit.value );
}

// ---------------------------------------------------------------------------------------------------------------------
// one hand, one frame
// ---------------------------------------------------------------------------------------------------------------------
static void HB_UpdateHand( int hand, double now, double dt, const vec3_t head_delta )
{
	hb_hand_t *h = &hb[hand];
	hb_box_t box;
	vec3_t local[HB_MAX_SAMPLES], wp[HB_MAX_SAMPLES];
	int i, count = 0, cls;
	float scale = VR_WorldScale();
	float best_f = 2.0f, tip_speed = 0.0f;
	int best_ent = -1;
	qboolean best_world = false;
	vec3_t best_p = { 0, 0, 0 }, best_n = { 0, 0, 1 }, best_v = { 0, 0, 0 };

	if( !HB_BuildBox( hand, &box ))
	{
		h->cur.valid = false;
		h->prev_ok = false;
		VectorClear( h->push );
		VectorClear( h->push_vis );
		return;
	}

	// the box changed size (fitted, or tuned by hand): no sweep from the old one
	if( h->prev_ok && ( fabs( h->prev.h[0] - box.h[0] ) > 0.01f || fabs( h->prev.h[1] - box.h[1] ) > 0.01f || fabs( h->prev.h[2] - box.h[2] ) > 0.01f ))
		h->prev_ok = false;

	h->cur = box;
	cls = box.cls;
	HB_MakeSamples( &box, local, &count );
	h->num_samples = count;

	for( i = 0; i < count; i++ )
	{
		HB_ToWorld( &box, local[i], wp[i] );
		VectorCopy( wp[i], h->samples[i] );
	}

	// ----- sweeping: where was every sample point a frame ago? -----
	if( h->prev_ok && dt > 0.0005 && dt < 0.1 && vr_hb_enable.value )
	{
		vec3_t disp;

		VectorSubtract( box.c, h->prev.c, disp );

		if( VectorLength( disp ) < 150.0f )
		{
			for( i = 0; i < count; i++ )
			{
				vec3_t from, motion, vel_ms;
				pmtrace_t tr;
				float len, speed_ms;

				HB_ToWorld( &h->prev, local[i], from );
				VectorSubtract( wp[i], from, motion );
				VectorSubtract( motion, head_delta, vel_ms );
				VectorScale( vel_ms, (float)( 1.0 / dt ) / scale, vel_ms );
				speed_ms = VectorLength( vel_ms );

				if( speed_ms > tip_speed )
					tip_speed = speed_ms;

				len = VectorLength( motion );

				if( len < 0.02f )
					continue;

				// a little further than the sample goes: a surface exactly where the sample ends still counts
				{
					vec3_t to, dir;
					int ent = -1;

					VectorScale( motion, 1.0f / len, dir );
					VectorMA( wp[i], 0.75f, dir, to );
					tr = gEngfuncs.CL_TraceLine( from, to, HB_TraceFlags( ));

					if( tr.startsolid || tr.allsolid || tr.fraction >= 1.0f )
						continue;

					if( !HB_EntityNumber( &tr, &ent ))
						continue;

					// the earliest thing the box ran into
					if( tr.fraction < best_f )
					{
						best_f = tr.fraction;
						best_ent = ent;
						best_world = ( ent <= 0 );
						VectorCopy( tr.endpos, best_p );
						VectorCopy( tr.plane.normal, best_n );
						VectorCopy( vel_ms, best_v );
					}
				}
			}
		}
	}

	// ----- a swing sound, when the tip of a melee weapon is going fast -----
	{
		float need = ( cls == HB_WHIP || cls == HB_BLOCKONLY ) ? vr_hb_swingspeed.value * 1.3f : vr_hb_swingspeed.value;

		if( tip_speed < need * 0.4f )
			h->swing_armed = true;

		if( cls != HB_BLOCKONLY && h->swing_armed && tip_speed > need && now >= h->swing_next && vr_hb_enable.value )
		{
			vec3_t zero = { 0, 0, 0 };

			h->swing_armed = false;
			h->swing_next = now + 0.35;
			HB_SendEvent( 1, hand, cls, 0, box.c, zero, tip_speed );
		}
	}

	// ----- what it ran into -----
	if( best_ent >= 0 && vr_hb_enable.value )
	{
		float into = -DotProduct( best_v, best_n );	// m/s straight into the surface
		float tang = sqrt( Q_max( 0.0f, DotProduct( best_v, best_v ) - into * into ));
		float eff = Q_max( into, tang * 0.6f );
		float need = ( cls == HB_WHIP ) ? vr_hb_whipspeed.value : vr_hb_minspeed.value;

		if( eff > 0.3f )
		{
			if( !best_world && cls != HB_BLOCKONLY )
			{
				if( eff >= need && !HB_RehitBlocked( h, best_ent, now ))
				{
					vec3_t hit_dir;

					HB_RehitMark( h, best_ent, now );
					VectorCopy( best_v, hit_dir );

					if( VectorNormalizeLength( hit_dir ) < 0.01f )
						VectorNegate( best_n, hit_dir );

					HB_SendEvent( 0, hand, cls, best_ent, best_p, hit_dir, eff );
					HB_Haptic( hand, eff, 1.0f );
					h->flash_until = now + 0.3;
				}
				else if( eff < need )
				{
					HB_Haptic( hand, eff, 0.3f );
				}

				// it bounces off whatever it hits, hurt or not
				if( vr_hb_bounce.value > 0.0f && eff > 0.8f && now >= h->wall_next )
				{
					float k = bound( 0.0f, eff * 3.5f * vr_hb_bounce.value, 14.0f );

					VectorMA( h->bounce_vel, k * 9.0f, best_n, h->bounce_vel );
					h->wall_next = now + 0.12;
				}
			}
			else if( best_world || cls == HB_BLOCKONLY )
			{
				float wall_need = vr_hb_wallspeed.value;

				if( wall_need > 0.0f && eff >= wall_need && now >= h->wall_next )
				{
					h->wall_next = now + 0.25;
					HB_SendEvent( 2, hand, cls, 0, best_p, best_n, eff );
					HB_Haptic( hand, eff, 0.7f );
					h->block_until = now + 0.25;

					if( vr_hb_bounce.value > 0.0f )
					{
						float k = bound( 0.0f, eff * 3.0f * vr_hb_bounce.value, 12.0f );

						VectorMA( h->bounce_vel, k * 9.0f, best_n, h->bounce_vel );
					}
				}
			}
		}
	}

	// ----- blocking: nothing goes through walls. Trace from the hand to every sample, push back what is inside something -----
	VectorClear( h->push );

	if( vr_hb_collide.value && vr_hb_enable.value )
	{
		vec3_t S = { 0, 0, 0 };
		int iter;

		for( iter = 0; iter < 3; iter++ )
		{
			float worst = 0.0f;
			vec3_t worst_d = { 0, 0, 0 };

			for( i = 0; i < count; i++ )
			{
				vec3_t p, d;
				pmtrace_t tr;
				float len;

				VectorAdd( wp[i], S, p );
				tr = gEngfuncs.CL_TraceLine( h->grip, p, HB_TraceFlags( ));

				if( tr.startsolid || tr.allsolid || tr.fraction >= 1.0f )
					continue;

				VectorSubtract( p, tr.endpos, d );
				len = VectorLength( d );

				if( len > worst )
				{
					worst = len;
					VectorCopy( d, worst_d );
				}
			}

			if( worst < 0.05f )
				break;

			VectorSubtract( S, worst_d, S );
		}

		{
			float sl = VectorLength( S ), mp = bound( 4.0f, vr_hb_maxpush.value, 80.0f );

			if( sl > mp )
				VectorScale( S, mp / sl, S );
		}

		VectorCopy( S, h->push );

		if( VectorLength( S ) > 0.3f )
			h->block_until = Q_max( h->block_until, now + 0.05 );
	}

	// ----- the visual push: instant when it goes in, soft when it goes out. And the bounce spring -----
	{
		float dtc = (float)bound( 0.0, dt, 0.05 );
		float fall = 1.0f - exp( -dtc / 0.06f );
		vec3_t d;

		if( VectorLength( h->push ) >= VectorLength( h->push_vis ))
			VectorCopy( h->push, h->push_vis );
		else
		{
			VectorSubtract( h->push, h->push_vis, d );
			VectorMA( h->push_vis, fall, d, h->push_vis );
		}

		// bounce: a damped spring, x'' = -k x - c x'
		VectorMA( h->bounce_vel, -dtc * 380.0f, h->bounce, h->bounce_vel );
		VectorScale( h->bounce_vel, 1.0f - bound( 0.0f, dtc * 22.0f, 0.9f ), h->bounce_vel );
		VectorMA( h->bounce, dtc, h->bounce_vel, h->bounce );

		{
			float bl = VectorLength( h->bounce );

			if( bl > 14.0f )
				VectorScale( h->bounce, 14.0f / bl, h->bounce );
		}
	}

	h->prev = box;
	h->prev_ok = true;
	h->prev_time = now;
}

// ---------------------------------------------------------------------------------------------------------------------
// the two hands (and what they hold) touch each other: overlapping boxes push each other apart a little, and the hands buzz
// ---------------------------------------------------------------------------------------------------------------------
// separating axis test of two oriented boxes. true = overlapping; dir = the way to move a to get away from b, depth = how far
static qboolean HB_BoxOverlap( const hb_box_t *a, const hb_box_t *b, vec3_t dir, float *depth )
{
	vec3_t axes[15], d;
	float best = 1.0e9f;
	int i, j, n = 0, best_i = -1;

	for( i = 0; i < 3; i++ )
	{
		VectorCopy( a->ax[i], axes[n] ); n++;
		VectorCopy( b->ax[i], axes[n] ); n++;
	}

	for( i = 0; i < 3; i++ )
	{
		for( j = 0; j < 3; j++ )
		{
			vec3_t c;
			float len;

			CrossProduct( a->ax[i], b->ax[j], c );
			len = VectorLength( c );

			if( len < 0.001f )
				continue;

			VectorScale( c, 1.0f / len, axes[n] );
			n++;
		}
	}

	VectorSubtract( b->c, a->c, d );

	for( i = 0; i < n; i++ )
	{
		float ra = 0.0f, rb = 0.0f, dist, overlap;
		int k;

		for( k = 0; k < 3; k++ )
		{
			ra += a->h[k] * fabs( DotProduct( a->ax[k], axes[i] ));
			rb += b->h[k] * fabs( DotProduct( b->ax[k], axes[i] ));
		}

		dist = DotProduct( d, axes[i] );
		overlap = ra + rb - fabs( dist );

		if( overlap <= 0.0f )
			return false;

		if( overlap < best )
		{
			best = overlap;
			best_i = i;
			VectorScale( axes[i], dist > 0.0f ? -1.0f : 1.0f, dir );
		}
	}

	if( best_i < 0 )
		return false;

	*depth = best;
	return true;
}

static void HB_HandsTouch( double now, double dt )
{
	static vec3_t last_c[2];
	static qboolean last_ok, was_touching;
	static double haptic_next;
	float strength = bound( 0.0f, vr_hb_handpush.value, 1.0f ), maxpush = bound( 0.5f, vr_hb_handmax.value, 20.0f );
	float fall = 1.0f - exp( -(float)bound( 0.0, dt, 0.05 ) / 0.08f );
	vec3_t want[2], dir, rel;
	float depth = 0.0f;
	qboolean touching = false;
	int h;

	VectorClear( want[0] );
	VectorClear( want[1] );

	if( strength > 0.0f && vr_hb_enable.value && hb[0].cur.valid && hb[1].cur.valid && HB_BoxOverlap( &hb[0].cur, &hb[1].cur, dir, &depth ))
	{
		float push = Q_min( depth * 0.5f * strength, maxpush );

		touching = true;
		VectorScale( dir, push, want[0] );		// the left one moves away from the right one
		VectorScale( dir, -push, want[1] );		// and the right one the other way
	}

	// how fast they came together
	if( last_ok && dt > 0.0005 && hb[0].cur.valid && hb[1].cur.valid )
	{
		vec3_t v0, v1;

		VectorSubtract( hb[0].cur.c, last_c[0], v0 );
		VectorSubtract( hb[1].cur.c, last_c[1], v1 );
		VectorSubtract( v0, v1, rel );
		VectorScale( rel, (float)( 1.0 / dt ) / VR_WorldScale(), rel );

		if( touching && !was_touching && now >= haptic_next && VectorLength( rel ) > 0.5f )
		{
			float sp = VectorLength( rel );

			HB_Haptic( 0, sp, 0.35f );
			HB_Haptic( 1, sp, 0.35f );
			haptic_next = now + 0.2;
		}
	}

	was_touching = touching;
	last_ok = hb[0].cur.valid && hb[1].cur.valid;

	if( hb[0].cur.valid ) VectorCopy( hb[0].cur.c, last_c[0] );
	if( hb[1].cur.valid ) VectorCopy( hb[1].cur.c, last_c[1] );

	// in at once, out softly
	for( h = 0; h < 2; h++ )
	{
		vec3_t diff;

		if( VectorLength( want[h] ) >= VectorLength( hb[h].hpush ))
			VectorCopy( want[h], hb[h].hpush );
		else
		{
			VectorSubtract( want[h], hb[h].hpush, diff );
			VectorMA( hb[h].hpush, fall, diff, hb[h].hpush );
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// the frame, called once per engine frame after the hands have moved
// ---------------------------------------------------------------------------------------------------------------------
void VR_MeleeFrame( void )
{
	static double last;
	static qboolean head_ok;
	static vec3_t head_last;
	double now = gp_host ? gp_host->realtime : 0.0, dt;
	vec3_t head, head_delta;
	float yaw;

	if( last <= 0.0 || now < last )
		last = now - 0.011;

	dt = now - last;
	last = now;

	VR_GetHeadGame( head, &yaw );

	if( head_ok )
		VectorSubtract( head, head_last, head_delta );
	else
		VectorClear( head_delta );

	VectorCopy( head, head_last );
	head_ok = true;

	// walking (the game moves the player) is not a swing of the hand
	if( VectorLength( head_delta ) > 40.0f )
	{
		hb[0].prev_ok = hb[1].prev_ok = false;
		VectorClear( head_delta );
	}

	if( !vr_hb_enable.value || HB_Cvar( "vr_in_menu" ) > 0.5f )
	{
		hb[0].cur.valid = hb[1].cur.valid = false;
		hb[0].prev_ok = hb[1].prev_ok = false;
		VectorClear( hb[0].push ); VectorClear( hb[1].push );
		VectorClear( hb[0].push_vis ); VectorClear( hb[1].push_vis );
		VectorClear( hb[0].hpush ); VectorClear( hb[1].hpush );
		return;
	}

	HB_UpdateHand( 1, now, dt, head_delta );
	HB_UpdateHand( 0, now, dt, head_delta );

	// remember the box of the weapon in the right hand: the same weapon in the left hand of a dual wield starts from it, mirrored
	if( hb[1].cur.valid )
		HB_CacheStore( hb[1].model );

	HB_HandsTouch( now, dt );

	// the left fist box starts from the default too, written into its cvars once so it can be tuned
	if( hb[0].cur.valid && hb[0].cur.cls == HB_FIST && !Q_strcmp( hb[0].model, "left_fist" ) && vr_hb_lf_sx.value <= 0.01f )
	{
		float c[3], z[3];

		HB_DefaultBox( HB_FIST, true, c, z );
		gEngfuncs.Cvar_SetValue( "vr_hb_lf_x", c[0] );
		gEngfuncs.Cvar_SetValue( "vr_hb_lf_y", c[1] );
		gEngfuncs.Cvar_SetValue( "vr_hb_lf_z", c[2] );
		gEngfuncs.Cvar_SetValue( "vr_hb_lf_sx", z[0] );
		gEngfuncs.Cvar_SetValue( "vr_hb_lf_sy", z[1] );
		gEngfuncs.Cvar_SetValue( "vr_hb_lf_sz", z[2] );
	}

	// the first time a weapon is held without a box of its own: fit the box to its mesh
	if( hb[1].cur.valid )
	{
		if( Q_strcmp( hb_fit_model, hb[1].model ))
		{
			Q_strncpy( hb_fit_model, hb[1].model, sizeof( hb_fit_model ));
			hb_fit_done = false;
			hb_fit_attempts = 0;
		}

		// no mesh to fit (the fist): its default box is written into the cvars, so it can be seen and tuned like any other
		if( !hb_fit_done && vr_hb_sx.value <= 0.01f && hb[1].cur.cls == HB_FIST )
		{
			float c[3], z[3];

			HB_DefaultBox( HB_FIST, false, c, z );
			gEngfuncs.Cvar_SetValue( "vr_hb_x", c[0] );
			gEngfuncs.Cvar_SetValue( "vr_hb_y", c[1] );
			gEngfuncs.Cvar_SetValue( "vr_hb_z", c[2] );
			gEngfuncs.Cvar_SetValue( "vr_hb_sx", z[0] );
			gEngfuncs.Cvar_SetValue( "vr_hb_sy", z[1] );
			gEngfuncs.Cvar_SetValue( "vr_hb_sz", z[2] );
			hb_fit_done = true;
		}

		if( !hb_fit_done && vr_hb_sx.value <= 0.01f && hb_fit_attempts < 30 && hb[1].cur.cls != HB_FIST )
		{
			hb_fit_wanted = true;
			hb_fit_n = 0;
		}
		else hb_fit_wanted = false;
	}
	else hb_fit_wanted = false;
}

// the weapon draws itself pushed back / bounced: the offset to add to the weapon's origin (world units)
void VR_MeleePushback( int hand, vec3_t out )
{
	hb_hand_t *h = &hb[hand & 1];

	if( !vr_hb_enable.value || !h->cur.valid )
	{
		VectorClear( out );
		return;
	}

	VectorAdd( h->push_vis, h->bounce, out );
	VectorAdd( out, h->hpush, out );
}

// ---------------------------------------------------------------------------------------------------------------------
// fitting the box to the weapon's mesh (called by the studio renderer while the viewmodel is drawn)
// ---------------------------------------------------------------------------------------------------------------------
qboolean VR_HbFitWanted( void )
{
	return hb_fit_wanted;
}

void VR_HbFitAccum( const float *p )
{
	vec3_t grip, f, l, u, d, local;
	int i;

	if( !VR_GetPoseAxes( f, l, u ) || !VR_GetHandBasis( 1, grip, d, local, local ))
		return;

	// measured where the weapon would be without a push back from a wall
	{
		vec3_t pb;

		VR_MeleePushback( 1, pb );
		VectorSubtract( p, pb, d );
		VectorSubtract( d, grip, d );
	}

	local[0] = DotProduct( d, f );
	local[1] = DotProduct( d, l );
	local[2] = DotProduct( d, u );

	if( !hb_fit_n )
	{
		VectorCopy( local, hb_fit_min );
		VectorCopy( local, hb_fit_max );
	}

	for( i = 0; i < 3; i++ )
	{
		if( local[i] < hb_fit_min[i] ) hb_fit_min[i] = local[i];
		if( local[i] > hb_fit_max[i] ) hb_fit_max[i] = local[i];
	}

	hb_fit_n++;
}

void VR_HbFitFinish( void )
{
	vec3_t c, ext;

	if( !hb_fit_wanted )
		return;

	hb_fit_attempts++;

	if( hb_fit_n < 12 )
		return;

	VectorSubtract( hb_fit_max, hb_fit_min, ext );
	VectorAdd( hb_fit_max, hb_fit_min, c );
	VectorScale( c, 0.5f, c );

	// a little fatter than the mesh, so thin weapons are not missed
	ext[0] = Q_max( ext[0], 3.0f );
	ext[1] = Q_max( ext[1] + 0.5f, 2.5f );
	ext[2] = Q_max( ext[2] + 0.5f, 2.5f );

	gEngfuncs.Cvar_SetValue( "vr_hb_x", c[0] );
	gEngfuncs.Cvar_SetValue( "vr_hb_y", c[1] );
	gEngfuncs.Cvar_SetValue( "vr_hb_z", c[2] );
	gEngfuncs.Cvar_SetValue( "vr_hb_sx", ext[0] );
	gEngfuncs.Cvar_SetValue( "vr_hb_sy", ext[1] );
	gEngfuncs.Cvar_SetValue( "vr_hb_sz", ext[2] );
	gEngfuncs.Cvar_SetValue( "vr_hb_pitch", 0.0f );
	gEngfuncs.Cvar_SetValue( "vr_hb_yaw", 0.0f );
	gEngfuncs.Cvar_SetValue( "vr_hb_roll", 0.0f );

	gEngfuncs.Con_Printf( "VR: fitted the collider of %s to the weapon: centre %.1f %.1f %.1f, size %.1f %.1f %.1f (vr_weapon_save keeps it, vr_hb_debug 1 shows it)\n",
		hb_fit_model, c[0], c[1], c[2], ext[0], ext[1], ext[2] );

	hb_fit_done = true;
	hb_fit_wanted = false;
	hb_fit_n = 0;
}

static void VR_HbFit_f( void )
{
	hb_fit_done = false;
	hb_fit_attempts = 0;
	gEngfuncs.Cvar_SetValue( "vr_hb_sx", 0.0f );
	gEngfuncs.Cvar_SetValue( "vr_hb_sy", 0.0f );
	gEngfuncs.Cvar_SetValue( "vr_hb_sz", 0.0f );
	gEngfuncs.Con_Printf( "VR: the collider of the weapon in your right hand will be fitted to its mesh again\n" );
}

static void VR_HbInfo_f( void )
{
	int h;

	for( h = 1; h >= 0; h-- )
	{
		const hb_hand_t *p = &hb[h];

		if( !p->cur.valid )
		{
			gEngfuncs.Con_Printf( "%s hand: no collider\n", h ? "right" : "left" );
			continue;
		}

		gEngfuncs.Con_Printf( "%s hand: class %d, model '%s', centre %.1f %.1f %.1f, size %.1f %.1f %.1f, push back %.1f units, %d sample points\n",
			h ? "right" : "left", p->cur.cls, p->model, p->cur.c[0], p->cur.c[1], p->cur.c[2],
			p->cur.h[0] * 2.0f, p->cur.h[1] * 2.0f, p->cur.h[2] * 2.0f, VectorLength( p->push ), p->num_samples );
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// debug drawing (called while GL is set up for the controller lines)
// ---------------------------------------------------------------------------------------------------------------------
qboolean VR_HitboxDebug( void )
{
	return vr_hb_debug.value != 0.0f && ( hb[0].cur.valid || hb[1].cur.valid );
}

static void HB_DrawBox( const hb_box_t *b, const vec3_t offset, float r, float g, float bl )
{
	static const int edges[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
	vec3_t corner[8];
	int i;

	for( i = 0; i < 8; i++ )
	{
		vec3_t local;

		local[0] = ( i & 1 ) ? b->h[0] : -b->h[0];
		local[1] = ( i & 2 ) ? b->h[1] : -b->h[1];
		local[2] = ( i & 4 ) ? b->h[2] : -b->h[2];
		HB_ToWorld( b, local, corner[i] );
		VectorAdd( corner[i], offset, corner[i] );
	}

	pglColor3f( r, g, bl );

	for( i = 0; i < 12; i++ )
	{
		pglVertex3fv( corner[edges[i][0]] );
		pglVertex3fv( corner[edges[i][1]] );
	}

	// an arrow along the forward axis of the box, so its direction is visible
	{
		vec3_t a, tip;

		VectorCopy( b->c, a );
		VectorAdd( a, offset, a );
		VectorMA( a, b->h[0] + 2.0f, b->ax[0], tip );
		pglVertex3fv( a );
		pglVertex3fv( tip );
	}
}

void VR_DrawMeleeDebug( void )
{
	int h;
	double now = gp_host ? gp_host->realtime : 0.0;

	if( !VR_HitboxDebug( ))
		return;

	pglLineWidth( 2.0f );
	pglBegin( GL_LINES );

	for( h = 0; h < 2; h++ )
	{
		const hb_hand_t *p = &hb[h];
		vec3_t none = { 0, 0, 0 };

		if( !p->cur.valid )
			continue;

		if( p->flash_until > now )
			HB_DrawBox( &p->cur, none, 1.0f, 0.15f, 0.15f );
		else if( p->block_until > now )
			HB_DrawBox( &p->cur, none, 1.0f, 0.9f, 0.1f );
		else
			HB_DrawBox( &p->cur, none, 0.1f, 0.9f, 1.0f );

		if( vr_hb_debug.value >= 2.0f )
		{
			vec3_t off;
			int i;

			// where the weapon is drawn (pushed back / bounced)
			VectorAdd( p->push_vis, p->bounce, off );
			VectorAdd( off, p->hpush, off );
			HB_DrawBox( &p->cur, off, 1.0f, 0.2f, 1.0f );

			// the sample points
			pglColor3f( 0.4f, 1.0f, 0.4f );

			for( i = 0; i < p->num_samples; i++ )
			{
				const float *s = p->samples[i];

				pglVertex3f( s[0] - 0.4f, s[1], s[2] ); pglVertex3f( s[0] + 0.4f, s[1], s[2] );
				pglVertex3f( s[0], s[1] - 0.4f, s[2] ); pglVertex3f( s[0], s[1] + 0.4f, s[2] );
				pglVertex3f( s[0], s[1], s[2] - 0.4f ); pglVertex3f( s[0], s[1], s[2] + 0.4f );
			}

			// the hand the blocking is traced from
			pglColor3f( 1.0f, 1.0f, 1.0f );
			pglVertex3f( p->grip[0] - 0.8f, p->grip[1], p->grip[2] ); pglVertex3f( p->grip[0] + 0.8f, p->grip[1], p->grip[2] );
			pglVertex3f( p->grip[0], p->grip[1] - 0.8f, p->grip[2] ); pglVertex3f( p->grip[0], p->grip[1] + 0.8f, p->grip[2] );
			pglVertex3f( p->grip[0], p->grip[1], p->grip[2] - 0.8f ); pglVertex3f( p->grip[0], p->grip[1], p->grip[2] + 0.8f );
		}
	}

	pglEnd();
}

// ---------------------------------------------------------------------------------------------------------------------
void VR_MeleeInitCvars( void )
{
	gEngfuncs.Cvar_RegisterVariable( &vr_hb_x );
	gEngfuncs.Cvar_RegisterVariable( &vr_hb_y );
	gEngfuncs.Cvar_RegisterVariable( &vr_hb_z );
	gEngfuncs.Cvar_RegisterVariable( &vr_hb_sx );
	gEngfuncs.Cvar_RegisterVariable( &vr_hb_sy );
	gEngfuncs.Cvar_RegisterVariable( &vr_hb_sz );
	gEngfuncs.Cvar_RegisterVariable( &vr_hb_pitch );
	gEngfuncs.Cvar_RegisterVariable( &vr_hb_yaw );
	gEngfuncs.Cvar_RegisterVariable( &vr_hb_roll );
	gEngfuncs.Cvar_RegisterVariable( &vr_hb_dmg );
	gEngfuncs.Cvar_RegisterVariable( &vr_hb_melee );
	gEngfuncs.Cvar_RegisterVariable( &vr_hb_lf_x );
	gEngfuncs.Cvar_RegisterVariable( &vr_hb_lf_y );
	gEngfuncs.Cvar_RegisterVariable( &vr_hb_lf_z );
	gEngfuncs.Cvar_RegisterVariable( &vr_hb_lf_sx );
	gEngfuncs.Cvar_RegisterVariable( &vr_hb_lf_sy );
	gEngfuncs.Cvar_RegisterVariable( &vr_hb_lf_sz );
	gEngfuncs.Cvar_RegisterVariable( &vr_hb_lf_pitch );
	gEngfuncs.Cvar_RegisterVariable( &vr_hb_lf_yaw );
	gEngfuncs.Cvar_RegisterVariable( &vr_hb_lf_roll );
	gEngfuncs.Cvar_RegisterVariable( &vr_hbl_x );
	gEngfuncs.Cvar_RegisterVariable( &vr_hbl_y );
	gEngfuncs.Cvar_RegisterVariable( &vr_hbl_z );
	gEngfuncs.Cvar_RegisterVariable( &vr_hbl_sx );
	gEngfuncs.Cvar_RegisterVariable( &vr_hbl_sy );
	gEngfuncs.Cvar_RegisterVariable( &vr_hbl_sz );
	gEngfuncs.Cvar_RegisterVariable( &vr_hbl_pitch );
	gEngfuncs.Cvar_RegisterVariable( &vr_hbl_yaw );
	gEngfuncs.Cvar_RegisterVariable( &vr_hbl_roll );

	gEngfuncs.Cvar_RegisterVariable( &vr_hb_enable );
	gEngfuncs.Cvar_RegisterVariable( &vr_hb_debug );
	gEngfuncs.Cvar_RegisterVariable( &vr_hb_collide );
	gEngfuncs.Cvar_RegisterVariable( &vr_hb_bbox );
	gEngfuncs.Cvar_RegisterVariable( &vr_hb_minspeed );
	gEngfuncs.Cvar_RegisterVariable( &vr_hb_whipspeed );
	gEngfuncs.Cvar_RegisterVariable( &vr_hb_fullspeed );
	gEngfuncs.Cvar_RegisterVariable( &vr_hb_mindmg );
	gEngfuncs.Cvar_RegisterVariable( &vr_hb_rehit );
	gEngfuncs.Cvar_RegisterVariable( &vr_hb_step );
	gEngfuncs.Cvar_RegisterVariable( &vr_hb_bounce );
	gEngfuncs.Cvar_RegisterVariable( &vr_hb_maxpush );
	gEngfuncs.Cvar_RegisterVariable( &vr_hb_haptic );
	gEngfuncs.Cvar_RegisterVariable( &vr_hb_wallspeed );
	gEngfuncs.Cvar_RegisterVariable( &vr_hb_swingspeed );
	gEngfuncs.Cvar_RegisterVariable( &vr_hb_nodmg_models );
	gEngfuncs.Cvar_RegisterVariable( &vr_hb_handpush );
	gEngfuncs.Cvar_RegisterVariable( &vr_hb_handmax );
	gEngfuncs.Cvar_RegisterVariable( &vr_hb_fist_size );
	gEngfuncs.Cvar_RegisterVariable( &vr_hb_fist_fwd );

	gEngfuncs.Cmd_AddCommand( "vr_hb_fit", VR_HbFit_f, "fit the collider of the weapon in your right hand to its mesh again (vr_weapon_save keeps it)" );
	gEngfuncs.Cmd_AddCommand( "vr_hb_info", VR_HbInfo_f, "print the colliders of both hands" );
}
