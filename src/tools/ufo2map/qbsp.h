/**
 * @file qbsp.h
 * @brief
 */

/*
Copyright (C) 1997-2001 Id Software, Inc.

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

#include <assert.h>

#include "common/scriplib.h"
#include "common/mathlib.h"
#include "common/polylib.h"
#include "common/bspfile.h"

#define	CLIP_EPSILON	0.1

#define	TEXINFO_NODE		-1		/* side is already on a node */


#define UNIT_SIZE			32
#define UNIT_HEIGHT			64

typedef byte pos_t;
typedef pos_t pos3_t[3];

typedef int ipos_t;
typedef ipos_t ipos3_t[3];

#define VecToPos(v,p)		(p[0]=(((int)v[0]+4096)/UNIT_SIZE), p[1]=(((int)v[1]+4096)/UNIT_SIZE), p[2]=((int)v[2]/UNIT_HEIGHT))
#define PosToVec(p,v)		(v[0]=((int)p[0]-128)*UNIT_SIZE+UNIT_SIZE/2, v[1]=((int)p[1]-128)*UNIT_SIZE+UNIT_SIZE/2, v[2]=(int)p[2]*UNIT_HEIGHT+UNIT_HEIGHT/2)


typedef struct plane_s {
	vec3_t	normal;
	vec_t	dist;
	int		type;
	struct plane_s	*hash_chain;
} plane_t;

typedef struct {
	vec_t	shift[2];
	vec_t	rotate;
	vec_t	scale[2];
	char	name[32];
	int		flags;
	int		value;
} brush_texture_t;

typedef struct side_s {
	int			planenum;
	int			texinfo;
	winding_t	*winding;
	struct side_s	*original;	/**< bspbrush_t sides will reference the mapbrush_t sides */
	int			contents;		/**< from miptex */
	int			surf;			/**< from miptex */
	qboolean	visible;		/**< choose visible planes first */
	qboolean	tested;			/**< this plane allready checked as a split */
	qboolean	bevel;			/**< don't ever use for bsp splitting */
} side_t;

typedef struct brush_s {
	int		entitynum;
	int		brushnum;

	int		contents;

	vec3_t	mins, maxs;

	int		numsides;
	side_t	*original_sides;

	qboolean finished;
	qboolean isTerrain;
	qboolean isGenSurf;
	qboolean optimizedDetail;
} mapbrush_t;

#define	PLANENUM_LEAF			-1

#define	MAXEDGES		20

typedef struct face_s {
	struct face_s	*next;		/**< on node */

	/** the chain of faces off of a node can be merged or split,
	 * but each face_t along the way will remain in the chain
	 * until the entire tree is freed */
	struct face_s	*merged;	/**< if set, this face isn't valid anymore */
	struct face_s	*split[2];	/**< if set, this face isn't valid anymore */

	struct portal_s	*portal;
	int				texinfo;
	int				planenum;
	int				contents;	/**< faces in different contents can't merge */
	int				outputnumber;
	winding_t		*w;
	int				numpoints;
	qboolean		badstartvert;	/**< tjunctions cannot be fixed without a midpoint vertex */
	int				vertexnums[MAXEDGES];
} face_t;

typedef struct bspbrush_s {
	struct bspbrush_s	*next;
	vec3_t	mins, maxs;
	int		side, testside;		/**< side of node during construction */
	mapbrush_t	*original;
	int		numsides;
	side_t	sides[6];			/**< variably sized */
} bspbrush_t;

#define	MAX_NODE_BRUSHES	8
typedef struct node_s {
	/** both leafs and nodes */
	int				planenum;	/**< -1 = leaf node */
	struct node_s	*parent;
	vec3_t			mins, maxs;	/**< valid after portalization */
	bspbrush_t		*volume;	/**< one for each leaf/node */

	/** nodes only */
	qboolean		detail_seperator;	/**< a detail brush caused the split */
	side_t			*side;		/**< the side that created the node */
	struct node_s	*children[2];
	face_t			*faces;

	/** leafs only */
	bspbrush_t		*brushlist;	/**< fragments of all brushes in this leaf */
	int				contents;	/**< OR of all brush contents */
	int				occupied;	/**< 1 or greater can reach entity */
	entity_t		*occupant;	/**< for leak file testing */
	int				cluster;	/**< for portalfile writing */
	int				area;		/**< for areaportals */
	struct portal_s	*portals;	/**< also on nodes during construction */
} node_t;

typedef struct portal_s {
	plane_t		plane;
	node_t		*onnode;		/**< NULL = outside box */
	node_t		*nodes[2];		/**< [0] = front side of plane */
	struct portal_s	*next[2];
	winding_t	*winding;

	qboolean	sidefound;		/**< false if ->side hasn't been checked */
	side_t		*side;			/**< NULL = non-visible */
	face_t		*face[2];		/**< output face in bsp file */
} portal_t;

typedef struct {
	node_t		*headnode;
	node_t		outside_node;
	vec3_t		mins, maxs;
} tree_t;

extern	int			entity_num;

extern	plane_t		mapplanes[MAX_MAP_PLANES];
extern	int			nummapplanes;

extern	int			nummapbrushes;
extern	mapbrush_t	mapbrushes[MAX_MAP_BRUSHES];

#define	MAX_MAP_SIDES		(MAX_MAP_BRUSHES*6)

extern	int			nummapbrushsides;
extern	side_t		brushsides[MAX_MAP_SIDES];


extern	vec3_t		v_epsilon;
extern	int			brush_start, brush_end;

extern	char		outbase[32];

extern	char	source[1024];

void LoadMapFile(const char *filename);
int FindFloatPlane(vec3_t normal, vec_t dist);

/*============================================================================= */

/* textures.c */

typedef struct {
	char	name[64];
	int		flags;
	int		value;
	int		contents;
	char	animname[64];
} textureref_t;

#define	MAX_MAP_TEXTURES	1024

extern	textureref_t	textureref[MAX_MAP_TEXTURES];
int	FindMiptex(const char *name);
int TexinfoForBrushTexture(plane_t *plane, brush_texture_t *bt, const vec3_t origin, qboolean isTerrain);

/* draw.c */

extern vec3_t	draw_mins, draw_maxs;

void Draw_ClearWindow(void);
void DrawWinding(winding_t *w);

void GLS_BeginScene(void);
void GLS_Winding(winding_t *w, int code);
void GLS_EndScene(void);

/* csg */

int MapBrushesBounds(int startbrush, int endbrush, int level, vec3_t clipmins, vec3_t clipmaxs, vec3_t mins, vec3_t maxs);
bspbrush_t *MakeBspBrushList(int startbrush, int endbrush, int level, vec3_t clipmins, vec3_t clipmaxs);
bspbrush_t *ChopBrushes(bspbrush_t *head);

/* brushbsp */

bspbrush_t *CopyBrush(bspbrush_t *brush);
void SplitBrush(bspbrush_t *brush, int planenum, bspbrush_t **front, bspbrush_t **back);
bspbrush_t *AllocBrush(int numsides);
int	CountBrushList(bspbrush_t *brushes);
void FreeBrush(bspbrush_t *brushes);
void FreeBrushList(bspbrush_t *brushes);
qboolean WindingIsTiny(winding_t *w);
tree_t *BrushBSP(bspbrush_t *brushlist, vec3_t mins, vec3_t maxs);

/* portals.c */

int VisibleContents(int contents);
void MarkVisibleSides(tree_t *tree, int start, int end);
void FreePortal(portal_t *p);
void MakeTreePortals(tree_t *tree);
void RemovePortalFromNode(portal_t *portal, node_t *l);

/*============================================================================= */

/* writebsp.c */

void SetModelNumbers(void);
void SetLightStyles(void);

void BeginBSPFile(void);
void WriteBSP(node_t *headnode);
void EndBSPFile(void);
void BeginModel(void);
void EndModel(void);
/*void EmitBrushes(void);*/
void EmitPlanes(void);

/* faces.c */

void MakeFaces(node_t *headnode);
void FixTjuncs(node_t *headnode);
int GetEdge2(int v1, int v2,  face_t *f);
void FreeFace(face_t *f);

/* tree.c */

void FreeTree(tree_t *tree);

/* trace.c */

void MakeTnodes(int levels);
void CloseTNodes(void);
int TestLine(const vec3_t start, const vec3_t stop);
int TestLineMask(const vec3_t start, const vec3_t stop, int levels);
int TestLineDM(const vec3_t start, const vec3_t stop, vec3_t end, int levels);
qboolean TestContents(const vec3_t pos);

/* levels.c */

extern vec3_t	v_epsilon;
extern vec3_t	worldMins, worldMaxs;

#define NUMMODELS 258

void PushInfo(void);
void PopInfo(void);
void ProcessLevel(unsigned int levelnum);
void PruneNodes(node_t *node);

/* routing.c */

void DoRouting(void);

#define ROUTING_NOT_REACHABLE 0xFF

/* qbsp3.c */

void ProcessModels(void);

/* conver_ase.c */

int ConvertBSPToASE(const char *bspName);
