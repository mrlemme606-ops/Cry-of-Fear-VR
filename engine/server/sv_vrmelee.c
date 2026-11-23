/*
sv_vrmelee.c - VR melee: apply the hit of a weapon collider to an entity of the game
Cry of Fear VR project.
Copyright (C) 2026 contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

The weapon colliders (ref/gl/gl_vr_melee.c) decide what was touched. This file makes the game hurt exactly that entity, the
same way the game's own melee code does it (CBasePlayer::MeleeHitThink*, see the decompile of hl.dll):

	ClearMultiDamage();
	pEntity->TraceAttack( pevPlayer, damage, direction, &tr, bits );	// virtual 24: blood, hit groups, hit sounds of breakables
	ApplyMultiDamage( pevPlayer, pevPlayer );				// pEntity->TakeDamage( pev, pev, amount, bits ), virtual 25

TraceAttack only collects the damage in the game's gMultiDamage (entity, amount, type). Clearing and applying it is done here
by reading and writing that structure, and calling TakeDamage of the entity, so no function of hl.dll is called by address:
only virtual functions of the entity itself, and one data structure whose place is checked against the image of the library
(inside a writable data section, the library's preferred base is the one the decompile was made from). When the check
fails, the hit is made with TakeDamage alone (no blood, no hit groups).
*/
#include "common.h"
#include "server.h"
#include "const.h"

#if XASH_WIN32 && XASH_X86

// the structure gMultiDamage of hl.dll: pEntity, amount, type (0x10231F34 in the decompile made at preferred base 0x10000000)
#define COF_PREFERRED_BASE		0x10000000u
#define COF_GMULTIDAMAGE_ADDR		0x10231F34u

// virtual functions of CBaseEntity in Cry of Fear (indexes found in the decompile: 20 Classify, 24 TraceAttack, 25 TakeDamage)
#define COF_VT_CLASSIFY			20
#define COF_VT_TRACEATTACK		24
#define COF_VT_TAKEDAMAGE		25

// a C++ member function (thiscall: this in ecx) called from C: __fastcall puts the first two arguments in ecx and edx, so the
// second one is a dummy
typedef int ( __fastcall *cof_classify_t )( void *self, void *edx );
typedef void ( __fastcall *cof_traceattack_t )( void *self, void *edx, entvars_t *pevAttacker, float flDamage, float dx, float dy, float dz, TraceResult *ptr, int bitsDamageType );
typedef int ( __fastcall *cof_takedamage_t )( void *self, void *edx, entvars_t *pevInflictor, entvars_t *pevAttacker, float flDamage, int bitsDamageType );

typedef struct
{
	void	*pEntity;
	float	amount;
	int	type;
} cof_multidamage_t;

static cof_multidamage_t *SV_VRMeleeFindMultiDamage( void )
{
	static cof_multidamage_t *cached;
	static qboolean checked;
	const byte *base;
	uint32_t nt, size_of_image, image_base, rva, nsec, opt_size, i;
	const byte *sec;
	qboolean ok = false;

	if( checked )
		return cached;

	checked = true;
	base = (const byte *)svgame.hInstance;

	if( !base )
		return NULL;

	// the image of the library: the DOS header says where the NT headers are
	if( base[0] != 'M' || base[1] != 'Z' )
		return NULL;

	nt = *(const uint32_t *)( base + 0x3C );

	if( nt < 0x40 || nt > 0x1000 || base[nt] != 'P' || base[nt + 1] != 'E' )
		return NULL;

	nsec = *(const uint16_t *)( base + nt + 6 );
	opt_size = *(const uint16_t *)( base + nt + 20 );

	if( opt_size < 60 || nsec == 0 || nsec > 96 )
		return NULL;

	image_base = *(const uint32_t *)( base + nt + 24 + 28 );
	size_of_image = *(const uint32_t *)( base + nt + 24 + 56 );

	// the decompile was made at this base: another one is another build of the library
	if( image_base != COF_PREFERRED_BASE )
		return NULL;

	rva = COF_GMULTIDAMAGE_ADDR - COF_PREFERRED_BASE;

	if( rva + sizeof( cof_multidamage_t ) > size_of_image )
		return NULL;

	sec = base + nt + 24 + opt_size;

	for( i = 0; i < nsec; i++, sec += 40 )
	{
		uint32_t vsize = *(const uint32_t *)( sec + 8 );
		uint32_t vaddr = *(const uint32_t *)( sec + 12 );
		uint32_t flags = *(const uint32_t *)( sec + 36 );

		// writable (0x80000000), not executable (0x20000000), initialised or not data
		if( rva >= vaddr && rva + sizeof( cof_multidamage_t ) <= vaddr + vsize && ( flags & 0x80000000u ) && !( flags & 0x20000000u ))
		{
			ok = true;
			break;
		}
	}

	if( !ok )
		return NULL;

	// whatever is in there now is cleared before every use, so it does not matter what it is
	cached = (cof_multidamage_t *)( base + rva );
	return cached;
}

static void **SV_VRMeleeVTable( edict_t *e )
{
	if( !e || !e->pvPrivateData )
		return NULL;

	return *(void ***)e->pvPrivateData;
}

#endif // XASH_WIN32 && XASH_X86

/*
=============
SV_VRMeleeHit

Hurts the entity a weapon collider hit. Returns
-1 = could not be done (no player, no game object, not supported)
 0 = the entity cannot be hurt
 1 = a creature was hurt (the body hit sound)
 2 = something else was hurt (a breakable...)
=============
*/
int SV_VRMeleeHit( int entnum, const vec3_t point, const vec3_t dir, float damage, int bits, qboolean effects )
{
#if XASH_WIN32 && XASH_X86
	edict_t *victim, *player;
	void **vt;
	TraceResult tr;
	vec3_t d, eye;
	int cls = 0, result = 2;
	cof_multidamage_t *md = NULL;

	if( sv.state != ss_active || svs.maxclients != 1 || entnum <= 1 || entnum >= svgame.numEntities || damage <= 0.0f )
		return -1;

	player = &svgame.edicts[1];
	victim = &svgame.edicts[entnum];

	if( !SV_IsValidEdict( player ) || !SV_IsValidEdict( victim ) || victim->free || !victim->pvPrivateData )
		return -1;

	if( FBitSet( victim->v.flags, FL_KILLME|FL_DORMANT ) || victim->v.takedamage == DAMAGE_NO )
		return 0;

	// a hit that comes from somewhere you cannot reach is no hit
	VectorAdd( player->v.origin, player->v.view_ofs, eye );
	VectorSubtract( point, eye, d );

	if( DotProduct( d, d ) > 260.0f * 260.0f )
		return -1;

	vt = SV_VRMeleeVTable( victim );

	if( !vt )
		return -1;

	VectorCopy( dir, d );

	if( VectorNormalizeLength( d ) < 0.5f )
	{
		VectorSubtract( victim->v.origin, point, d );
		VectorNormalize( d );
	}

	// what the game's own trace would have said: the entity, the point, and the part of the body that was hit
	memset( &tr, 0, sizeof( tr ));
	tr.flFraction = 0.0f;
	tr.pHit = victim;
	VectorCopy( point, tr.vecEndPos );
	VectorNegate( d, tr.vecPlaneNormal );

	{
		vec3_t start, end;
		trace_t t;

		VectorMA( point, -6.0f, d, start );
		VectorMA( point, 10.0f, d, end );
		t = SV_Move( start, vec3_origin, vec3_origin, end, MOVE_NORMAL, player, false );

		if( t.ent == victim && !t.startsolid )
		{
			tr.iHitgroup = t.hitgroup;
			tr.flFraction = t.fraction;
			VectorCopy( t.endpos, tr.vecEndPos );
			VectorCopy( t.plane.normal, tr.vecPlaneNormal );
			tr.flPlaneDist = t.plane.dist;
		}
	}

	// does the game count it as a creature? (class none and machine are not)
	cls = ((cof_classify_t)vt[COF_VT_CLASSIFY])( victim->pvPrivateData, NULL );
	result = ( cls != 0 && cls != 1 ) ? 1 : 2;

	if( effects )
		md = SV_VRMeleeFindMultiDamage();

	if( md )
	{
		// ClearMultiDamage, TraceAttack, ApplyMultiDamage: the way the game's melee does it
		md->pEntity = NULL;
		md->amount = 0.0f;
		md->type = 0;

		((cof_traceattack_t)vt[COF_VT_TRACEATTACK])( victim->pvPrivateData, NULL, &player->v, damage, d[0], d[1], d[2], &tr, bits );

		if( md->pEntity )
		{
			void *target = md->pEntity;
			void **mvt = *(void ***)target;
			float amount = md->amount;
			int type = md->type;

			md->pEntity = NULL;
			md->amount = 0.0f;
			md->type = 0;

			((cof_takedamage_t)mvt[COF_VT_TAKEDAMAGE])( target, NULL, &player->v, &player->v, amount, type );
		}
	}
	else
	{
		((cof_takedamage_t)vt[COF_VT_TAKEDAMAGE])( victim->pvPrivateData, NULL, &player->v, &player->v, damage, bits );
	}

	return result;
#else
	return -1;
#endif
}
