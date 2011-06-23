/*
Copyright (C) 1996-1997 Id Software, Inc.

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

#include "quakedef.h"
#include "image.h"
#include "r_shadow.h"
#include "mod_skeletal_animatevertices_generic.h"
#ifdef SSE_POSSIBLE
#include "mod_skeletal_animatevertices_sse.h"
#endif

#ifdef SSE_POSSIBLE
static qboolean r_skeletal_use_sse_defined = false;
cvar_t r_skeletal_use_sse = {0, "r_skeletal_use_sse", "1", "use SSE for skeletal model animation"};
#endif
cvar_t r_skeletal_debugbone = {0, "r_skeletal_debugbone", "-1", "development cvar for testing skeletal model code"};
cvar_t r_skeletal_debugbonecomponent = {0, "r_skeletal_debugbonecomponent", "3", "development cvar for testing skeletal model code"};
cvar_t r_skeletal_debugbonevalue = {0, "r_skeletal_debugbonevalue", "100", "development cvar for testing skeletal model code"};
cvar_t r_skeletal_debugtranslatex = {0, "r_skeletal_debugtranslatex", "1", "development cvar for testing skeletal model code"};
cvar_t r_skeletal_debugtranslatey = {0, "r_skeletal_debugtranslatey", "1", "development cvar for testing skeletal model code"};
cvar_t r_skeletal_debugtranslatez = {0, "r_skeletal_debugtranslatez", "1", "development cvar for testing skeletal model code"};
cvar_t mod_alias_supporttagscale = {0, "mod_alias_supporttagscale", "1", "support scaling factors in bone/tag attachment matrices as supported by MD3"};

float mod_md3_sin[320];

static size_t Mod_Skeltal_AnimateVertices_maxbonepose = 0;
static void *Mod_Skeltal_AnimateVertices_bonepose = NULL;
void Mod_Skeletal_FreeBuffers(void)
{
	if(Mod_Skeltal_AnimateVertices_bonepose)
		Mem_Free(Mod_Skeltal_AnimateVertices_bonepose);
	Mod_Skeltal_AnimateVertices_maxbonepose = 0;
	Mod_Skeltal_AnimateVertices_bonepose = NULL;
}
void *Mod_Skeletal_AnimateVertices_AllocBuffers(size_t nbytes)
{
	if(Mod_Skeltal_AnimateVertices_maxbonepose < nbytes)
	{
		if(Mod_Skeltal_AnimateVertices_bonepose)
			Mem_Free(Mod_Skeltal_AnimateVertices_bonepose);
		Mod_Skeltal_AnimateVertices_bonepose = Z_Malloc(nbytes);
		Mod_Skeltal_AnimateVertices_maxbonepose = nbytes;
	}
	return Mod_Skeltal_AnimateVertices_bonepose;
}

void Mod_Skeletal_AnimateVertices(const dp_model_t * RESTRICT model, const frameblend_t * RESTRICT frameblend, const skeleton_t *skeleton, float * RESTRICT vertex3f, float * RESTRICT normal3f, float * RESTRICT svector3f, float * RESTRICT tvector3f)
{
#ifdef SSE_POSSIBLE
	if(r_skeletal_use_sse_defined)
		if(r_skeletal_use_sse.integer)
		{
			Mod_Skeletal_AnimateVertices_SSE(model, frameblend, skeleton, vertex3f, normal3f, svector3f, tvector3f);
			return;
		}
#endif
	Mod_Skeletal_AnimateVertices_Generic(model, frameblend, skeleton, vertex3f, normal3f, svector3f, tvector3f);
}

void Mod_AliasInit (void)
{
	int i;
	Cvar_RegisterVariable(&r_skeletal_debugbone);
	Cvar_RegisterVariable(&r_skeletal_debugbonecomponent);
	Cvar_RegisterVariable(&r_skeletal_debugbonevalue);
	Cvar_RegisterVariable(&r_skeletal_debugtranslatex);
	Cvar_RegisterVariable(&r_skeletal_debugtranslatey);
	Cvar_RegisterVariable(&r_skeletal_debugtranslatez);
	Cvar_RegisterVariable(&mod_alias_supporttagscale);
	for (i = 0;i < 320;i++)
		mod_md3_sin[i] = sin(i * M_PI * 2.0f / 256.0);
#ifdef SSE_POSSIBLE
	if(Sys_HaveSSE())
	{
		Con_Printf("Skeletal animation uses SSE code path\n");
		r_skeletal_use_sse_defined = true;
		Cvar_RegisterVariable(&r_skeletal_use_sse);
	}
	else
		Con_Printf("Skeletal animation uses generic code path (SSE disabled or not detected)\n");
#else
	Con_Printf("Skeletal animation uses generic code path (SSE not compiled in)\n");
#endif
}

int Mod_Skeletal_AddBlend(dp_model_t *model, const blendweights_t *newweights)
{
	int i;
	blendweights_t *weights;
	if(!newweights->influence[1])
		return newweights->index[0];
	weights = model->surfmesh.data_blendweights;
	for (i = 0;i < model->surfmesh.num_blends;i++, weights++)
	{
		if (!memcmp(weights, newweights, sizeof(blendweights_t)))
			return model->num_bones + i;
	}
	model->surfmesh.num_blends++;
	memcpy(weights, newweights, sizeof(blendweights_t));
	return model->num_bones + i;
}

int Mod_Skeletal_CompressBlend(dp_model_t *model, const int *newindex, const float *newinfluence)
{
	int i, total;
	float scale;
	blendweights_t newweights;
	if(!newinfluence[1])
		return newindex[0];
	scale = 0;
	for (i = 0;i < 4;i++)
		scale += newinfluence[i];
	scale = 255.0f / scale;
	total = 0;
	for (i = 0;i < 4;i++)
	{
		newweights.index[i] = newindex[i];
		newweights.influence[i] = (unsigned char)(newinfluence[i] * scale);
		total += newweights.influence[i];
	}	
	while (total > 255)
	{
		for (i = 0;i < 4;i++)
		{
			if(newweights.influence[i] > 0 && total > 255) 
			{ 
				newweights.influence[i]--;
				total--; 
			}
		}
	}
	while (total < 255)
	{
		for (i = 0; i < 4;i++)
		{
			if(newweights.influence[i] < 255 && total < 255) 
			{ 
				newweights.influence[i]++; 
				total++; 
			}
		}
	}
	return Mod_Skeletal_AddBlend(model, &newweights);
}

void Mod_MD3_AnimateVertices(const dp_model_t * RESTRICT model, const frameblend_t * RESTRICT frameblend, const skeleton_t *skeleton, float * RESTRICT vertex3f, float * RESTRICT normal3f, float * RESTRICT svector3f, float * RESTRICT tvector3f)
{
	// vertex morph
	int i, numblends, blendnum;
	int numverts = model->surfmesh.num_vertices;
	numblends = 0;
	for (blendnum = 0;blendnum < MAX_FRAMEBLENDS;blendnum++)
	{
		//VectorMA(translate, model->surfmesh.num_morphmdlframetranslate, frameblend[blendnum].lerp, translate);
		if (frameblend[blendnum].lerp > 0)
			numblends = blendnum + 1;
	}
	// special case for the first blend because it avoids some adds and the need to memset the arrays first
	for (blendnum = 0;blendnum < numblends;blendnum++)
	{
		const md3vertex_t *verts = model->surfmesh.data_morphmd3vertex + numverts * frameblend[blendnum].subframe;
		if (vertex3f)
		{
			float scale = frameblend[blendnum].lerp * (1.0f / 64.0f);
			if (blendnum == 0)
			{
				for (i = 0;i < numverts;i++)
				{
					vertex3f[i * 3 + 0] = verts[i].origin[0] * scale;
					vertex3f[i * 3 + 1] = verts[i].origin[1] * scale;
					vertex3f[i * 3 + 2] = verts[i].origin[2] * scale;
				}
			}
			else
			{
				for (i = 0;i < numverts;i++)
				{
					vertex3f[i * 3 + 0] += verts[i].origin[0] * scale;
					vertex3f[i * 3 + 1] += verts[i].origin[1] * scale;
					vertex3f[i * 3 + 2] += verts[i].origin[2] * scale;
				}
			}
		}
		// the yaw and pitch stored in md3 models are 8bit quantized angles
		// (0-255), and as such a lookup table is very well suited to
		// decoding them, and since cosine is equivalent to sine with an
		// extra 45 degree rotation, this uses one lookup table for both
		// sine and cosine with a +64 bias to get cosine.
		if (normal3f)
		{
			float lerp = frameblend[blendnum].lerp;
			if (blendnum == 0)
			{
				for (i = 0;i < numverts;i++)
				{
					normal3f[i * 3 + 0] = mod_md3_sin[verts[i].yaw + 64] * mod_md3_sin[verts[i].pitch     ] * lerp;
					normal3f[i * 3 + 1] = mod_md3_sin[verts[i].yaw     ] * mod_md3_sin[verts[i].pitch     ] * lerp;
					normal3f[i * 3 + 2] =                                  mod_md3_sin[verts[i].pitch + 64] * lerp;
				}
			}
			else
			{
				for (i = 0;i < numverts;i++)
				{
					normal3f[i * 3 + 0] += mod_md3_sin[verts[i].yaw + 64] * mod_md3_sin[verts[i].pitch     ] * lerp;
					normal3f[i * 3 + 1] += mod_md3_sin[verts[i].yaw     ] * mod_md3_sin[verts[i].pitch     ] * lerp;
					normal3f[i * 3 + 2] +=                                  mod_md3_sin[verts[i].pitch + 64] * lerp;
				}
			}
		}
		if (svector3f)
		{
			const texvecvertex_t *texvecvert = model->surfmesh.data_morphtexvecvertex + numverts * frameblend[blendnum].subframe;
			float f = frameblend[blendnum].lerp * (1.0f / 127.0f);
			if (blendnum == 0)
			{
				for (i = 0;i < numverts;i++, texvecvert++)
				{
					VectorScale(texvecvert->svec, f, svector3f + i*3);
					VectorScale(texvecvert->tvec, f, tvector3f + i*3);
				}
			}
			else
			{
				for (i = 0;i < numverts;i++, texvecvert++)
				{
					VectorMA(svector3f + i*3, f, texvecvert->svec, svector3f + i*3);
					VectorMA(tvector3f + i*3, f, texvecvert->tvec, tvector3f + i*3);
				}
			}
		}
	}
}
void Mod_MDL_AnimateVertices(const dp_model_t * RESTRICT model, const frameblend_t * RESTRICT frameblend, const skeleton_t *skeleton, float * RESTRICT vertex3f, float * RESTRICT normal3f, float * RESTRICT svector3f, float * RESTRICT tvector3f)
{
	// vertex morph
	int i, numblends, blendnum;
	int numverts = model->surfmesh.num_vertices;
	float translate[3];
	VectorClear(translate);
	numblends = 0;
	// blend the frame translates to avoid redundantly doing so on each vertex
	// (a bit of a brain twister but it works)
	for (blendnum = 0;blendnum < MAX_FRAMEBLENDS;blendnum++)
	{
		if (model->surfmesh.data_morphmd2framesize6f)
			VectorMA(translate, frameblend[blendnum].lerp, model->surfmesh.data_morphmd2framesize6f + frameblend[blendnum].subframe * 6 + 3, translate);
		else
			VectorMA(translate, frameblend[blendnum].lerp, model->surfmesh.num_morphmdlframetranslate, translate);
		if (frameblend[blendnum].lerp > 0)
			numblends = blendnum + 1;
	}
	// special case for the first blend because it avoids some adds and the need to memset the arrays first
	for (blendnum = 0;blendnum < numblends;blendnum++)
	{
		const trivertx_t *verts = model->surfmesh.data_morphmdlvertex + numverts * frameblend[blendnum].subframe;
		if (vertex3f)
		{
			float scale[3];
			if (model->surfmesh.data_morphmd2framesize6f)
				VectorScale(model->surfmesh.data_morphmd2framesize6f + frameblend[blendnum].subframe * 6, frameblend[blendnum].lerp, scale);
			else
				VectorScale(model->surfmesh.num_morphmdlframescale, frameblend[blendnum].lerp, scale);
			if (blendnum == 0)
			{
				for (i = 0;i < numverts;i++)
				{
					vertex3f[i * 3 + 0] = translate[0] + verts[i].v[0] * scale[0];
					vertex3f[i * 3 + 1] = translate[1] + verts[i].v[1] * scale[1];
					vertex3f[i * 3 + 2] = translate[2] + verts[i].v[2] * scale[2];
				}
			}
			else
			{
				for (i = 0;i < numverts;i++)
				{
					vertex3f[i * 3 + 0] += verts[i].v[0] * scale[0];
					vertex3f[i * 3 + 1] += verts[i].v[1] * scale[1];
					vertex3f[i * 3 + 2] += verts[i].v[2] * scale[2];
				}
			}
		}
		// the vertex normals in mdl models are an index into a table of
		// 162 unique values, this very crude quantization reduces the
		// vertex normal to only one byte, which saves a lot of space but
		// also makes lighting pretty coarse
		if (normal3f)
		{
			float lerp = frameblend[blendnum].lerp;
			if (blendnum == 0)
			{
				for (i = 0;i < numverts;i++)
				{
					const float *vn = m_bytenormals[verts[i].lightnormalindex];
					VectorScale(vn, lerp, normal3f + i*3);
				}
			}
			else
			{
				for (i = 0;i < numverts;i++)
				{
					const float *vn = m_bytenormals[verts[i].lightnormalindex];
					VectorMA(normal3f + i*3, lerp, vn, normal3f + i*3);
				}
			}
		}
		if (svector3f)
		{
			const texvecvertex_t *texvecvert = model->surfmesh.data_morphtexvecvertex + numverts * frameblend[blendnum].subframe;
			float f = frameblend[blendnum].lerp * (1.0f / 127.0f);
			if (blendnum == 0)
			{
				for (i = 0;i < numverts;i++, texvecvert++)
				{
					VectorScale(texvecvert->svec, f, svector3f + i*3);
					VectorScale(texvecvert->tvec, f, tvector3f + i*3);
				}
			}
			else
			{
				for (i = 0;i < numverts;i++, texvecvert++)
				{
					VectorMA(svector3f + i*3, f, texvecvert->svec, svector3f + i*3);
					VectorMA(tvector3f + i*3, f, texvecvert->tvec, tvector3f + i*3);
				}
			}
		}
	}
}

int Mod_Alias_GetTagMatrix(const dp_model_t *model, const frameblend_t *frameblend, const skeleton_t *skeleton, int tagindex, matrix4x4_t *outmatrix)
{
	matrix4x4_t temp;
	matrix4x4_t parentbonematrix;
	matrix4x4_t tempbonematrix;
	matrix4x4_t bonematrix;
	matrix4x4_t blendmatrix;
	int blendindex;
	int parenttagindex;
	int k;
	float lerp;
	const float *input;
	float blendtag[12];
	*outmatrix = identitymatrix;
	if (skeleton && skeleton->relativetransforms)
	{
		if (tagindex < 0 || tagindex >= skeleton->model->num_bones)
			return 4;
		*outmatrix = skeleton->relativetransforms[tagindex];
		while ((tagindex = model->data_bones[tagindex].parent) >= 0)
		{
			temp = *outmatrix;
			Matrix4x4_Concat(outmatrix, &skeleton->relativetransforms[tagindex], &temp);
		}
	}
	else if (model->num_bones)
	{
		if (tagindex < 0 || tagindex >= model->num_bones)
			return 4;
		Matrix4x4_Clear(&blendmatrix);
		for (blendindex = 0;blendindex < MAX_FRAMEBLENDS && frameblend[blendindex].lerp > 0;blendindex++)
		{
			lerp = frameblend[blendindex].lerp;
			Matrix4x4_FromBonePose6s(&bonematrix, model->num_posescale, model->data_poses6s + 6 * (frameblend[blendindex].subframe * model->num_bones + tagindex));
			parenttagindex = tagindex;
			while ((parenttagindex = model->data_bones[parenttagindex].parent) >= 0)
			{
				Matrix4x4_FromBonePose6s(&parentbonematrix, model->num_posescale, model->data_poses6s + 6 * (frameblend[blendindex].subframe * model->num_bones + parenttagindex));
				tempbonematrix = bonematrix;
				Matrix4x4_Concat(&bonematrix, &parentbonematrix, &tempbonematrix);
			}
			Matrix4x4_Accumulate(&blendmatrix, &bonematrix, lerp);
		}
		*outmatrix = blendmatrix;
	}
	else if (model->num_tags)
	{
		if (tagindex < 0 || tagindex >= model->num_tags)
			return 4;
		for (k = 0;k < 12;k++)
			blendtag[k] = 0;
		for (blendindex = 0;blendindex < MAX_FRAMEBLENDS && frameblend[blendindex].lerp > 0;blendindex++)
		{
			lerp = frameblend[blendindex].lerp;
			input = model->data_tags[frameblend[blendindex].subframe * model->num_tags + tagindex].matrixgl;
			for (k = 0;k < 12;k++)
				blendtag[k] += input[k] * lerp;
		}
		Matrix4x4_FromArray12FloatGL(outmatrix, blendtag);
	}

	if(!mod_alias_supporttagscale.integer)
		Matrix4x4_Normalize3(outmatrix, outmatrix);

	return 0;
}

int Mod_Alias_GetExtendedTagInfoForIndex(const dp_model_t *model, unsigned int skin, const frameblend_t *frameblend, const skeleton_t *skeleton, int tagindex, int *parentindex, const char **tagname, matrix4x4_t *tag_localmatrix)
{
	int blendindex;
	int k;
	float lerp;
	matrix4x4_t bonematrix;
	matrix4x4_t blendmatrix;
	const float *input;
	float blendtag[12];

	if (skeleton && skeleton->relativetransforms)
	{
		if (tagindex < 0 || tagindex >= skeleton->model->num_bones)
			return 1;
		*parentindex = skeleton->model->data_bones[tagindex].parent;
		*tagname = skeleton->model->data_bones[tagindex].name;
		*tag_localmatrix = skeleton->relativetransforms[tagindex];
		return 0;
	}
	else if (model->num_bones)
	{
		if (tagindex < 0 || tagindex >= model->num_bones)
			return 1;
		*parentindex = model->data_bones[tagindex].parent;
		*tagname = model->data_bones[tagindex].name;
		Matrix4x4_Clear(&blendmatrix);
		for (blendindex = 0;blendindex < MAX_FRAMEBLENDS && frameblend[blendindex].lerp > 0;blendindex++)
		{
			lerp = frameblend[blendindex].lerp;
			Matrix4x4_FromBonePose6s(&bonematrix, model->num_posescale, model->data_poses6s + 6 * (frameblend[blendindex].subframe * model->num_bones + tagindex));
			Matrix4x4_Accumulate(&blendmatrix, &bonematrix, lerp);
		}
		*tag_localmatrix = blendmatrix;
		return 0;
	}
	else if (model->num_tags)
	{
		if (tagindex < 0 || tagindex >= model->num_tags)
			return 1;
		*parentindex = -1;
		*tagname = model->data_tags[tagindex].name;
		for (k = 0;k < 12;k++)
			blendtag[k] = 0;
		for (blendindex = 0;blendindex < MAX_FRAMEBLENDS && frameblend[blendindex].lerp > 0;blendindex++)
		{
			lerp = frameblend[blendindex].lerp;
			input = model->data_tags[frameblend[blendindex].subframe * model->num_tags + tagindex].matrixgl;
			for (k = 0;k < 12;k++)
				blendtag[k] += input[k] * lerp;
		}
		Matrix4x4_FromArray12FloatGL(tag_localmatrix, blendtag);
		return 0;
	}

	return 2;
}

int Mod_Alias_GetTagIndexForName(const dp_model_t *model, unsigned int skin, const char *tagname)
{
	int i;
	if(skin >= (unsigned int)model->numskins)
		skin = 0;
	if (model->num_bones)
		for (i = 0;i < model->num_bones;i++)
			if (!strcasecmp(tagname, model->data_bones[i].name))
				return i + 1;
	if (model->num_tags)
		for (i = 0;i < model->num_tags;i++)
			if (!strcasecmp(tagname, model->data_tags[i].name))
				return i + 1;
	return 0;
}

static void Mod_BuildBaseBonePoses(void)
{
	int boneindex;
	matrix4x4_t *basebonepose;
	float *outinvmatrix = loadmodel->data_baseboneposeinverse;
	matrix4x4_t bonematrix;
	matrix4x4_t tempbonematrix;
	if (!loadmodel->num_bones)
		return;
	basebonepose = (matrix4x4_t *)Mem_Alloc(tempmempool, loadmodel->num_bones * sizeof(matrix4x4_t));
	for (boneindex = 0;boneindex < loadmodel->num_bones;boneindex++)
	{
		Matrix4x4_FromBonePose6s(&bonematrix, loadmodel->num_posescale, loadmodel->data_poses6s + 6 * boneindex);
		if (loadmodel->data_bones[boneindex].parent >= 0)
		{
			tempbonematrix = bonematrix;
			Matrix4x4_Concat(&bonematrix, basebonepose + loadmodel->data_bones[boneindex].parent, &tempbonematrix);
		}
		basebonepose[boneindex] = bonematrix;
		Matrix4x4_Invert_Simple(&tempbonematrix, basebonepose + boneindex);
		Matrix4x4_ToArray12FloatD3D(&tempbonematrix, outinvmatrix + 12*boneindex);
	}
	Mem_Free(basebonepose);
}

static void Mod_Alias_CalculateBoundingBox(void)
{
	int vnum;
	qboolean firstvertex = true;
	float dist, yawradius, radius;
	float *v;
	float *vertex3f;
	frameblend_t frameblend[MAX_FRAMEBLENDS];
	memset(frameblend, 0, sizeof(frameblend));
	frameblend[0].lerp = 1;
	vertex3f = (float *) Mem_Alloc(loadmodel->mempool, loadmodel->surfmesh.num_vertices * sizeof(float[3]));
	VectorClear(loadmodel->normalmins);
	VectorClear(loadmodel->normalmaxs);
	yawradius = 0;
	radius = 0;
	for (frameblend[0].subframe = 0;frameblend[0].subframe < loadmodel->num_poses;frameblend[0].subframe++)
	{
		loadmodel->AnimateVertices(loadmodel, frameblend, NULL, vertex3f, NULL, NULL, NULL);
		for (vnum = 0, v = vertex3f;vnum < loadmodel->surfmesh.num_vertices;vnum++, v += 3)
		{
			if (firstvertex)
			{
				firstvertex = false;
				VectorCopy(v, loadmodel->normalmins);
				VectorCopy(v, loadmodel->normalmaxs);
			}
			else
			{
				if (loadmodel->normalmins[0] > v[0]) loadmodel->normalmins[0] = v[0];
				if (loadmodel->normalmins[1] > v[1]) loadmodel->normalmins[1] = v[1];
				if (loadmodel->normalmins[2] > v[2]) loadmodel->normalmins[2] = v[2];
				if (loadmodel->normalmaxs[0] < v[0]) loadmodel->normalmaxs[0] = v[0];
				if (loadmodel->normalmaxs[1] < v[1]) loadmodel->normalmaxs[1] = v[1];
				if (loadmodel->normalmaxs[2] < v[2]) loadmodel->normalmaxs[2] = v[2];
			}
			dist = v[0] * v[0] + v[1] * v[1];
			if (yawradius < dist)
				yawradius = dist;
			dist += v[2] * v[2];
			if (radius < dist)
				radius = dist;
		}
	}
	if (vertex3f)
		Mem_Free(vertex3f);
	radius = sqrt(radius);
	yawradius = sqrt(yawradius);
	loadmodel->yawmins[0] = loadmodel->yawmins[1] = -yawradius;
	loadmodel->yawmaxs[0] = loadmodel->yawmaxs[1] = yawradius;
	loadmodel->yawmins[2] = loadmodel->normalmins[2];
	loadmodel->yawmaxs[2] = loadmodel->normalmaxs[2];
	loadmodel->rotatedmins[0] = loadmodel->rotatedmins[1] = loadmodel->rotatedmins[2] = -radius;
	loadmodel->rotatedmaxs[0] = loadmodel->rotatedmaxs[1] = loadmodel->rotatedmaxs[2] = radius;
	loadmodel->radius = radius;
	loadmodel->radius2 = radius * radius;
}

static void Mod_Alias_MorphMesh_CompileFrames(void)
{
	int i, j;
	frameblend_t frameblend[MAX_FRAMEBLENDS];
	unsigned char *datapointer;
	memset(frameblend, 0, sizeof(frameblend));
	frameblend[0].lerp = 1;
	datapointer = (unsigned char *)Mem_Alloc(loadmodel->mempool, loadmodel->surfmesh.num_vertices * (sizeof(float[3]) * 4 + loadmodel->surfmesh.num_morphframes * sizeof(texvecvertex_t)));
	loadmodel->surfmesh.data_vertex3f = (float *)datapointer;datapointer += loadmodel->surfmesh.num_vertices * sizeof(float[3]);
	loadmodel->surfmesh.data_svector3f = (float *)datapointer;datapointer += loadmodel->surfmesh.num_vertices * sizeof(float[3]);
	loadmodel->surfmesh.data_tvector3f = (float *)datapointer;datapointer += loadmodel->surfmesh.num_vertices * sizeof(float[3]);
	loadmodel->surfmesh.data_normal3f = (float *)datapointer;datapointer += loadmodel->surfmesh.num_vertices * sizeof(float[3]);
	loadmodel->surfmesh.data_morphtexvecvertex = (texvecvertex_t *)datapointer;datapointer += loadmodel->surfmesh.num_morphframes * loadmodel->surfmesh.num_vertices * sizeof(texvecvertex_t);
	// this counts down from the last frame to the first so that the final data in surfmesh is for frame zero (which is what the renderer expects to be there)
	for (i = loadmodel->surfmesh.num_morphframes-1;i >= 0;i--)
	{
		frameblend[0].subframe = i;
		loadmodel->AnimateVertices(loadmodel, frameblend, NULL, loadmodel->surfmesh.data_vertex3f, loadmodel->surfmesh.data_normal3f, NULL, NULL);
		Mod_BuildTextureVectorsFromNormals(0, loadmodel->surfmesh.num_vertices, loadmodel->surfmesh.num_triangles, loadmodel->surfmesh.data_vertex3f, loadmodel->surfmesh.data_texcoordtexture2f, loadmodel->surfmesh.data_normal3f, loadmodel->surfmesh.data_element3i, loadmodel->surfmesh.data_svector3f, loadmodel->surfmesh.data_tvector3f, r_smoothnormals_areaweighting.integer != 0);
		// encode the svector and tvector in 3 byte format for permanent storage
		for (j = 0;j < loadmodel->surfmesh.num_vertices;j++)
		{
			VectorScaleCast(loadmodel->surfmesh.data_svector3f + j * 3, 127.0f, signed char, loadmodel->surfmesh.data_morphtexvecvertex[i*loadmodel->surfmesh.num_vertices+j].svec);
			VectorScaleCast(loadmodel->surfmesh.data_tvector3f + j * 3, 127.0f, signed char, loadmodel->surfmesh.data_morphtexvecvertex[i*loadmodel->surfmesh.num_vertices+j].tvec);
		}
	}
}

static void Mod_MDLMD2MD3_TraceLine(dp_model_t *model, const frameblend_t *frameblend, const skeleton_t *skeleton, trace_t *trace, const vec3_t start, const vec3_t end, int hitsupercontentsmask)
{
	int i;
	float segmentmins[3], segmentmaxs[3];
	msurface_t *surface;
	static int maxvertices = 0;
	static float *vertex3f = NULL;
	memset(trace, 0, sizeof(*trace));
	trace->fraction = 1;
	trace->realfraction = 1;
	trace->hitsupercontentsmask = hitsupercontentsmask;
	if (maxvertices < model->surfmesh.num_vertices)
	{
		if (vertex3f)
			Z_Free(vertex3f);
		maxvertices = (model->surfmesh.num_vertices + 255) & ~255;
		vertex3f = (float *)Z_Malloc(maxvertices * sizeof(float[3]));
	}
	segmentmins[0] = min(start[0], end[0]) - 1;
	segmentmins[1] = min(start[1], end[1]) - 1;
	segmentmins[2] = min(start[2], end[2]) - 1;
	segmentmaxs[0] = max(start[0], end[0]) + 1;
	segmentmaxs[1] = max(start[1], end[1]) + 1;
	segmentmaxs[2] = max(start[2], end[2]) + 1;
	model->AnimateVertices(model, frameblend, skeleton, vertex3f, NULL, NULL, NULL);
	for (i = 0, surface = model->data_surfaces;i < model->num_surfaces;i++, surface++)
		Collision_TraceLineTriangleMeshFloat(trace, start, end, model->surfmesh.num_triangles, model->surfmesh.data_element3i, vertex3f, 0, NULL, SUPERCONTENTS_SOLID | (surface->texture->basematerialflags & MATERIALFLAGMASK_TRANSLUCENT ? 0 : SUPERCONTENTS_OPAQUE), 0, surface->texture, segmentmins, segmentmaxs);
}

static int maxvertices = 0;
static float *vertex3f = NULL;

static void Mod_MDLMD2MD3_TraceBox(dp_model_t *model, const frameblend_t *frameblend, const skeleton_t *skeleton, trace_t *trace, const vec3_t start, const vec3_t boxmins, const vec3_t boxmaxs, const vec3_t end, int hitsupercontentsmask)
{
	int i;
	vec3_t shiftstart, shiftend;
	float segmentmins[3], segmentmaxs[3];
	msurface_t *surface;
	colboxbrushf_t thisbrush_start, thisbrush_end;
	vec3_t boxstartmins, boxstartmaxs, boxendmins, boxendmaxs;

	if (VectorCompare(boxmins, boxmaxs))
	{
		VectorAdd(start, boxmins, shiftstart);
		VectorAdd(end, boxmins, shiftend);
		Mod_MDLMD2MD3_TraceLine(model, frameblend, skeleton, trace, shiftstart, shiftend, hitsupercontentsmask);
		VectorSubtract(trace->endpos, boxmins, trace->endpos);
		return;
	}

	// box trace, performed as brush trace
	memset(trace, 0, sizeof(*trace));
	trace->fraction = 1;
	trace->realfraction = 1;
	trace->hitsupercontentsmask = hitsupercontentsmask;
	if (maxvertices < model->surfmesh.num_vertices)
	{
		if (vertex3f)
			Z_Free(vertex3f);
		maxvertices = (model->surfmesh.num_vertices + 255) & ~255;
		vertex3f = (float *)Z_Malloc(maxvertices * sizeof(float[3]));
	}
	segmentmins[0] = min(start[0], end[0]) + boxmins[0] - 1;
	segmentmins[1] = min(start[1], end[1]) + boxmins[1] - 1;
	segmentmins[2] = min(start[2], end[2]) + boxmins[2] - 1;
	segmentmaxs[0] = max(start[0], end[0]) + boxmaxs[0] + 1;
	segmentmaxs[1] = max(start[1], end[1]) + boxmaxs[1] + 1;
	segmentmaxs[2] = max(start[2], end[2]) + boxmaxs[2] + 1;
	VectorAdd(start, boxmins, boxstartmins);
	VectorAdd(start, boxmaxs, boxstartmaxs);
	VectorAdd(end, boxmins, boxendmins);
	VectorAdd(end, boxmaxs, boxendmaxs);
	Collision_BrushForBox(&thisbrush_start, boxstartmins, boxstartmaxs, 0, 0, NULL);
	Collision_BrushForBox(&thisbrush_end, boxendmins, boxendmaxs, 0, 0, NULL);
	if (maxvertices < model->surfmesh.num_vertices)
	{
		if (vertex3f)
			Z_Free(vertex3f);
		maxvertices = (model->surfmesh.num_vertices + 255) & ~255;
		vertex3f = (float *)Z_Malloc(maxvertices * sizeof(float[3]));
	}
	model->AnimateVertices(model, frameblend, skeleton, vertex3f, NULL, NULL, NULL);
	for (i = 0, surface = model->data_surfaces;i < model->num_surfaces;i++, surface++)
		Collision_TraceBrushTriangleMeshFloat(trace, &thisbrush_start.brush, &thisbrush_end.brush, model->surfmesh.num_triangles, model->surfmesh.data_element3i, vertex3f, 0, NULL, SUPERCONTENTS_SOLID | (surface->texture->basematerialflags & MATERIALFLAGMASK_TRANSLUCENT ? 0 : SUPERCONTENTS_OPAQUE), 0, surface->texture, segmentmins, segmentmaxs);
}

static void Mod_ConvertAliasVerts (int inverts, trivertx_t *v, trivertx_t *out, int *vertremap)
{
	int i, j;
	for (i = 0;i < inverts;i++)
	{
		if (vertremap[i] < 0 && vertremap[i+inverts] < 0) // only used vertices need apply...
			continue;
		j = vertremap[i]; // not onseam
		if (j >= 0)
			out[j] = v[i];
		j = vertremap[i+inverts]; // onseam
		if (j >= 0)
			out[j] = v[i];
	}
}

static void Mod_MDL_LoadFrames (unsigned char* datapointer, int inverts, int *vertremap)
{
	int i, f, pose, groupframes;
	float interval;
	daliasframetype_t *pframetype;
	daliasframe_t *pinframe;
	daliasgroup_t *group;
	daliasinterval_t *intervals;
	animscene_t *scene;
	pose = 0;
	scene = loadmodel->animscenes;
	for (f = 0;f < loadmodel->numframes;f++)
	{
		pframetype = (daliasframetype_t *)datapointer;
		datapointer += sizeof(daliasframetype_t);
		if (LittleLong (pframetype->type) == ALIAS_SINGLE)
		{
			// a single frame is still treated as a group
			interval = 0.1f;
			groupframes = 1;
		}
		else
		{
			// read group header
			group = (daliasgroup_t *)datapointer;
			datapointer += sizeof(daliasgroup_t);
			groupframes = LittleLong (group->numframes);

			// intervals (time per frame)
			intervals = (daliasinterval_t *)datapointer;
			datapointer += sizeof(daliasinterval_t) * groupframes;

			interval = LittleFloat (intervals->interval); // FIXME: support variable framerate groups
			if (interval < 0.01f)
			{
				Con_Printf("%s has an invalid interval %f, changing to 0.1\n", loadmodel->name, interval);
				interval = 0.1f;
			}
		}

		// get scene name from first frame
		pinframe = (daliasframe_t *)datapointer;

		strlcpy(scene->name, pinframe->name, sizeof(scene->name));
		scene->firstframe = pose;
		scene->framecount = groupframes;
		scene->framerate = 1.0f / interval;
		scene->loop = true;
		scene++;

		// read frames
		for (i = 0;i < groupframes;i++)
		{
			pinframe = (daliasframe_t *)datapointer;
			datapointer += sizeof(daliasframe_t);
			Mod_ConvertAliasVerts(inverts, (trivertx_t *)datapointer, loadmodel->surfmesh.data_morphmdlvertex + pose * loadmodel->surfmesh.num_vertices, vertremap);
			datapointer += sizeof(trivertx_t) * inverts;
			pose++;
		}
	}
}

static void Mod_BuildAliasSkinFromSkinFrame(texture_t *texture, skinframe_t *skinframe)
{
	if (cls.state == ca_dedicated)
		return;
	// hack
	if (!skinframe)
		skinframe = R_SkinFrame_LoadMissing();
	memset(texture, 0, sizeof(*texture));
	texture->currentframe = texture;
	//texture->animated = false;
	texture->numskinframes = 1;
	texture->skinframerate = 1;
	texture->skinframes[0] = skinframe;
	texture->currentskinframe = skinframe;
	//texture->backgroundnumskinframes = 0;
	//texture->customblendfunc[0] = 0;
	//texture->customblendfunc[1] = 0;
	//texture->surfaceflags = 0;
	//texture->supercontents = 0;
	//texture->surfaceparms = 0;
	//texture->textureflags = 0;

	texture->basematerialflags = MATERIALFLAG_WALL;
	if (texture->currentskinframe->hasalpha)
		texture->basematerialflags |= MATERIALFLAG_ALPHA | MATERIALFLAG_BLENDED | MATERIALFLAG_NOSHADOW;
	texture->currentmaterialflags = texture->basematerialflags;
	texture->offsetmapping = OFFSETMAPPING_OFF;
	texture->offsetscale = 1;
	texture->specularscalemod = 1;
	texture->specularpowermod = 1;
	texture->surfaceflags = 0;
	texture->supercontents = SUPERCONTENTS_SOLID;
	if (!(texture->basematerialflags & MATERIALFLAG_BLENDED))
		texture->supercontents |= SUPERCONTENTS_OPAQUE;
}

void Mod_BuildAliasSkinsFromSkinFiles(texture_t *skin, skinfile_t *skinfile, const char *meshname, const char *shadername)
{
	int i;
	static char stripbuf[MAX_QPATH];
	skinfileitem_t *skinfileitem;
	if(developer_extra.integer)
		Con_DPrintf("Looking up texture for %s (default: %s)\n", meshname, shadername);
	if (skinfile)
	{
		// the skin += loadmodel->num_surfaces part of this is because data_textures on alias models is arranged as [numskins][numsurfaces]
		for (i = 0;skinfile;skinfile = skinfile->next, i++, skin += loadmodel->num_surfaces)
		{
			memset(skin, 0, sizeof(*skin));
			// see if a mesh
			for (skinfileitem = skinfile->items;skinfileitem;skinfileitem = skinfileitem->next)
			{
				// leave the skin unitialized (nodraw) if the replacement is "common/nodraw" or "textures/common/nodraw"
				if (!strcmp(skinfileitem->name, meshname))
				{
					Image_StripImageExtension(skinfileitem->replacement, stripbuf, sizeof(stripbuf));
					if(developer_extra.integer)
						Con_DPrintf("--> got %s from skin file\n", stripbuf);
					Mod_LoadTextureFromQ3Shader(skin, stripbuf, true, true, (r_mipskins.integer ? TEXF_MIPMAP : 0) | TEXF_ALPHA | TEXF_PICMIP | TEXF_COMPRESS);
					break;
				}
			}
			if (!skinfileitem)
			{
				// don't render unmentioned meshes
				Mod_BuildAliasSkinFromSkinFrame(skin, NULL);
				if(developer_extra.integer)
					Con_DPrintf("--> skipping\n");
				skin->basematerialflags = skin->currentmaterialflags = MATERIALFLAG_NOSHADOW | MATERIALFLAG_NODRAW;
			}
		}
	}
	else
	{
		if(developer_extra.integer)
			Con_DPrintf("--> using default\n");
		Image_StripImageExtension(shadername, stripbuf, sizeof(stripbuf));
		Mod_LoadTextureFromQ3Shader(skin, stripbuf, true, true, (r_mipskins.integer ? TEXF_MIPMAP : 0) | TEXF_ALPHA | TEXF_PICMIP | TEXF_COMPRESS);
	}
}

#define BOUNDI(VALUE,MIN,MAX) if (VALUE < MIN || VALUE >= MAX) Host_Error("model %s has an invalid ##VALUE (%d exceeds %d - %d)", loadmodel->name, VALUE, MIN, MAX);
#define BOUNDF(VALUE,MIN,MAX) if (VALUE < MIN || VALUE >= MAX) Host_Error("model %s has an invalid ##VALUE (%f exceeds %f - %f)", loadmodel->name, VALUE, MIN, MAX);
void Mod_IDP0_Load(dp_model_t *mod, void *buffer, void *bufferend)
{
	int i, j, version, totalskins, skinwidth, skinheight, groupframes, groupskins, numverts;
	float scales, scalet, interval;
	msurface_t *surface;
	unsigned char *data;
	mdl_t *pinmodel;
	stvert_t *pinstverts;
	dtriangle_t *pintriangles;
	daliasskintype_t *pinskintype;
	daliasskingroup_t *pinskingroup;
	daliasskininterval_t *pinskinintervals;
	daliasframetype_t *pinframetype;
	daliasgroup_t *pinframegroup;
	unsigned char *datapointer, *startframes, *startskins;
	char name[MAX_QPATH];
	skinframe_t *tempskinframe;
	animscene_t *tempskinscenes;
	texture_t *tempaliasskins;
	float *vertst;
	int *vertonseam, *vertremap;
	skinfile_t *skinfiles;

	datapointer = (unsigned char *)buffer;
	pinmodel = (mdl_t *)datapointer;
	datapointer += sizeof(mdl_t);

	version = LittleLong (pinmodel->version);
	if (version != ALIAS_VERSION)
		Host_Error ("%s has wrong version number (%i should be %i)",
				 loadmodel->name, version, ALIAS_VERSION);

	loadmodel->modeldatatypestring = "MDL";

	loadmodel->type = mod_alias;
	loadmodel->AnimateVertices = Mod_MDL_AnimateVertices;
	loadmodel->DrawSky = NULL;
	loadmodel->DrawAddWaterPlanes = NULL;
	loadmodel->Draw = R_Q1BSP_Draw;
	loadmodel->DrawDepth = R_Q1BSP_DrawDepth;
	loadmodel->DrawDebug = R_Q1BSP_DrawDebug;
	loadmodel->DrawPrepass = R_Q1BSP_DrawPrepass;
	loadmodel->CompileShadowMap = R_Q1BSP_CompileShadowMap;
	loadmodel->DrawShadowMap = R_Q1BSP_DrawShadowMap;
	loadmodel->CompileShadowVolume = R_Q1BSP_CompileShadowVolume;
	loadmodel->DrawShadowVolume = R_Q1BSP_DrawShadowVolume;
	loadmodel->DrawLight = R_Q1BSP_DrawLight;
	loadmodel->TraceBox = Mod_MDLMD2MD3_TraceBox;
	loadmodel->TraceLine = Mod_MDLMD2MD3_TraceLine;
	// FIXME add TraceBrush!
	loadmodel->PointSuperContents = NULL;

	loadmodel->num_surfaces = 1;
	loadmodel->nummodelsurfaces = loadmodel->num_surfaces;
	data = (unsigned char *)Mem_Alloc(loadmodel->mempool, loadmodel->num_surfaces * sizeof(msurface_t) + loadmodel->num_surfaces * sizeof(int));
	loadmodel->data_surfaces = (msurface_t *)data;data += loadmodel->num_surfaces * sizeof(msurface_t);
	loadmodel->sortedmodelsurfaces = (int *)data;data += loadmodel->num_surfaces * sizeof(int);
	loadmodel->sortedmodelsurfaces[0] = 0;

	loadmodel->numskins = LittleLong(pinmodel->numskins);
	BOUNDI(loadmodel->numskins,0,65536);
	skinwidth = LittleLong (pinmodel->skinwidth);
	BOUNDI(skinwidth,0,65536);
	skinheight = LittleLong (pinmodel->skinheight);
	BOUNDI(skinheight,0,65536);
	numverts = LittleLong(pinmodel->numverts);
	BOUNDI(numverts,0,65536);
	loadmodel->surfmesh.num_triangles = LittleLong(pinmodel->numtris);
	BOUNDI(loadmodel->surfmesh.num_triangles,0,65536);
	loadmodel->numframes = LittleLong(pinmodel->numframes);
	BOUNDI(loadmodel->numframes,0,65536);
	loadmodel->synctype = (synctype_t)LittleLong (pinmodel->synctype);
	BOUNDI((int)loadmodel->synctype,0,2);
	// convert model flags to EF flags (MF_ROCKET becomes EF_ROCKET, etc)
	i = LittleLong (pinmodel->flags);
	loadmodel->effects = ((i & 255) << 24) | (i & 0x00FFFF00);

	for (i = 0;i < 3;i++)
	{
		loadmodel->surfmesh.num_morphmdlframescale[i] = LittleFloat (pinmodel->scale[i]);
		loadmodel->surfmesh.num_morphmdlframetranslate[i] = LittleFloat (pinmodel->scale_origin[i]);
	}

	startskins = datapointer;
	totalskins = 0;
	for (i = 0;i < loadmodel->numskins;i++)
	{
		pinskintype = (daliasskintype_t *)datapointer;
		datapointer += sizeof(daliasskintype_t);
		if (LittleLong(pinskintype->type) == ALIAS_SKIN_SINGLE)
			groupskins = 1;
		else
		{
			pinskingroup = (daliasskingroup_t *)datapointer;
			datapointer += sizeof(daliasskingroup_t);
			groupskins = LittleLong(pinskingroup->numskins);
			datapointer += sizeof(daliasskininterval_t) * groupskins;
		}

		for (j = 0;j < groupskins;j++)
		{
			datapointer += skinwidth * skinheight;
			totalskins++;
		}
	}

	pinstverts = (stvert_t *)datapointer;
	datapointer += sizeof(stvert_t) * numverts;

	pintriangles = (dtriangle_t *)datapointer;
	datapointer += sizeof(dtriangle_t) * loadmodel->surfmesh.num_triangles;

	startframes = datapointer;
	loadmodel->surfmesh.num_morphframes = 0;
	for (i = 0;i < loadmodel->numframes;i++)
	{
		pinframetype = (daliasframetype_t *)datapointer;
		datapointer += sizeof(daliasframetype_t);
		if (LittleLong (pinframetype->type) == ALIAS_SINGLE)
			groupframes = 1;
		else
		{
			pinframegroup = (daliasgroup_t *)datapointer;
			datapointer += sizeof(daliasgroup_t);
			groupframes = LittleLong(pinframegroup->numframes);
			datapointer += sizeof(daliasinterval_t) * groupframes;
		}

		for (j = 0;j < groupframes;j++)
		{
			datapointer += sizeof(daliasframe_t);
			datapointer += sizeof(trivertx_t) * numverts;
			loadmodel->surfmesh.num_morphframes++;
		}
	}
	loadmodel->num_poses = loadmodel->surfmesh.num_morphframes;

	// store texture coordinates into temporary array, they will be stored
	// after usage is determined (triangle data)
	vertst = (float *)Mem_Alloc(tempmempool, numverts * 2 * sizeof(float[2]));
	vertremap = (int *)Mem_Alloc(tempmempool, numverts * 3 * sizeof(int));
	vertonseam = vertremap + numverts * 2;

	scales = 1.0 / skinwidth;
	scalet = 1.0 / skinheight;
	for (i = 0;i < numverts;i++)
	{
		vertonseam[i] = LittleLong(pinstverts[i].onseam);
		vertst[i*2+0] = (LittleLong(pinstverts[i].s) + 0.5) * scales;
		vertst[i*2+1] = (LittleLong(pinstverts[i].t) + 0.5) * scalet;
		vertst[(i+numverts)*2+0] = vertst[i*2+0] + 0.5;
		vertst[(i+numverts)*2+1] = vertst[i*2+1];
	}

// load triangle data
	loadmodel->surfmesh.data_element3i = (int *)Mem_Alloc(loadmodel->mempool, sizeof(int[3]) * loadmodel->surfmesh.num_triangles);

	// read the triangle elements
	for (i = 0;i < loadmodel->surfmesh.num_triangles;i++)
		for (j = 0;j < 3;j++)
			loadmodel->surfmesh.data_element3i[i*3+j] = LittleLong(pintriangles[i].vertindex[j]);
	// validate (note numverts is used because this is the original data)
	Mod_ValidateElements(loadmodel->surfmesh.data_element3i, loadmodel->surfmesh.num_triangles, 0, numverts, __FILE__, __LINE__);
	// now butcher the elements according to vertonseam and tri->facesfront
	// and then compact the vertex set to remove duplicates
	for (i = 0;i < loadmodel->surfmesh.num_triangles;i++)
		if (!LittleLong(pintriangles[i].facesfront)) // backface
			for (j = 0;j < 3;j++)
				if (vertonseam[loadmodel->surfmesh.data_element3i[i*3+j]])
					loadmodel->surfmesh.data_element3i[i*3+j] += numverts;
	// count the usage
	// (this uses vertremap to count usage to save some memory)
	for (i = 0;i < numverts*2;i++)
		vertremap[i] = 0;
	for (i = 0;i < loadmodel->surfmesh.num_triangles*3;i++)
		vertremap[loadmodel->surfmesh.data_element3i[i]]++;
	// build remapping table and compact array
	loadmodel->surfmesh.num_vertices = 0;
	for (i = 0;i < numverts*2;i++)
	{
		if (vertremap[i])
		{
			vertremap[i] = loadmodel->surfmesh.num_vertices;
			vertst[loadmodel->surfmesh.num_vertices*2+0] = vertst[i*2+0];
			vertst[loadmodel->surfmesh.num_vertices*2+1] = vertst[i*2+1];
			loadmodel->surfmesh.num_vertices++;
		}
		else
			vertremap[i] = -1; // not used at all
	}
	// remap the elements to the new vertex set
	for (i = 0;i < loadmodel->surfmesh.num_triangles * 3;i++)
		loadmodel->surfmesh.data_element3i[i] = vertremap[loadmodel->surfmesh.data_element3i[i]];
	// store the texture coordinates
	loadmodel->surfmesh.data_texcoordtexture2f = (float *)Mem_Alloc(loadmodel->mempool, sizeof(float[2]) * loadmodel->surfmesh.num_vertices);
	for (i = 0;i < loadmodel->surfmesh.num_vertices;i++)
	{
		loadmodel->surfmesh.data_texcoordtexture2f[i*2+0] = vertst[i*2+0];
		loadmodel->surfmesh.data_texcoordtexture2f[i*2+1] = vertst[i*2+1];
	}

	// generate ushort elements array if possible
	if (loadmodel->surfmesh.num_vertices <= 65536)
		loadmodel->surfmesh.data_element3s = (unsigned short *)Mem_Alloc(loadmodel->mempool, sizeof(unsigned short[3]) * loadmodel->surfmesh.num_triangles);
	if (loadmodel->surfmesh.data_element3s)
		for (i = 0;i < loadmodel->surfmesh.num_triangles*3;i++)
			loadmodel->surfmesh.data_element3s[i] = loadmodel->surfmesh.data_element3i[i];

// load the frames
	loadmodel->animscenes = (animscene_t *)Mem_Alloc(loadmodel->mempool, sizeof(animscene_t) * loadmodel->numframes);
	loadmodel->surfmesh.data_morphmdlvertex = (trivertx_t *)Mem_Alloc(loadmodel->mempool, sizeof(trivertx_t) * loadmodel->surfmesh.num_morphframes * loadmodel->surfmesh.num_vertices);
	if (r_enableshadowvolumes.integer)
	{
		loadmodel->surfmesh.data_neighbor3i = (int *)Mem_Alloc(loadmodel->mempool, loadmodel->surfmesh.num_triangles * sizeof(int[3]));
	}
	Mod_MDL_LoadFrames (startframes, numverts, vertremap);
	if (loadmodel->surfmesh.data_neighbor3i)
		Mod_BuildTriangleNeighbors(loadmodel->surfmesh.data_neighbor3i, loadmodel->surfmesh.data_element3i, loadmodel->surfmesh.num_triangles);
	Mod_Alias_CalculateBoundingBox();
	Mod_Alias_MorphMesh_CompileFrames();

	Mem_Free(vertst);
	Mem_Free(vertremap);

	// load the skins
	skinfiles = Mod_LoadSkinFiles();
	if (skinfiles)
	{
		loadmodel->skinscenes = (animscene_t *)Mem_Alloc(loadmodel->mempool, loadmodel->numskins * sizeof(animscene_t));
		loadmodel->num_textures = loadmodel->num_surfaces * loadmodel->numskins;
		loadmodel->num_texturesperskin = loadmodel->num_surfaces;
		loadmodel->data_textures = (texture_t *)Mem_Alloc(loadmodel->mempool, loadmodel->num_surfaces * loadmodel->numskins * sizeof(texture_t));
		Mod_BuildAliasSkinsFromSkinFiles(loadmodel->data_textures, skinfiles, "default", "");
		Mod_FreeSkinFiles(skinfiles);
		for (i = 0;i < loadmodel->numskins;i++)
		{
			loadmodel->skinscenes[i].firstframe = i;
			loadmodel->skinscenes[i].framecount = 1;
			loadmodel->skinscenes[i].loop = true;
			loadmodel->skinscenes[i].framerate = 10;
		}
	}
	else
	{
		loadmodel->skinscenes = (animscene_t *)Mem_Alloc(loadmodel->mempool, loadmodel->numskins * sizeof(animscene_t));
		loadmodel->num_textures = loadmodel->num_surfaces * totalskins;
		loadmodel->num_texturesperskin = loadmodel->num_surfaces;
		loadmodel->data_textures = (texture_t *)Mem_Alloc(loadmodel->mempool, loadmodel->num_surfaces * totalskins * sizeof(texture_t));
		totalskins = 0;
		datapointer = startskins;
		for (i = 0;i < loadmodel->numskins;i++)
		{
			pinskintype = (daliasskintype_t *)datapointer;
			datapointer += sizeof(daliasskintype_t);

			if (pinskintype->type == ALIAS_SKIN_SINGLE)
			{
				groupskins = 1;
				interval = 0.1f;
			}
			else
			{
				pinskingroup = (daliasskingroup_t *)datapointer;
				datapointer += sizeof(daliasskingroup_t);

				groupskins = LittleLong (pinskingroup->numskins);

				pinskinintervals = (daliasskininterval_t *)datapointer;
				datapointer += sizeof(daliasskininterval_t) * groupskins;

				interval = LittleFloat(pinskinintervals[0].interval);
				if (interval < 0.01f)
				{
					Con_Printf("%s has an invalid interval %f, changing to 0.1\n", loadmodel->name, interval);
					interval = 0.1f;
				}
			}

			dpsnprintf(loadmodel->skinscenes[i].name, sizeof(loadmodel->skinscenes[i].name), "skin %i", i);
			loadmodel->skinscenes[i].firstframe = totalskins;
			loadmodel->skinscenes[i].framecount = groupskins;
			loadmodel->skinscenes[i].framerate = 1.0f / interval;
			loadmodel->skinscenes[i].loop = true;

			for (j = 0;j < groupskins;j++)
			{
				if (groupskins > 1)
					dpsnprintf (name, sizeof(name), "%s_%i_%i", loadmodel->name, i, j);
				else
					dpsnprintf (name, sizeof(name), "%s_%i", loadmodel->name, i);
				if (!Mod_LoadTextureFromQ3Shader(loadmodel->data_textures + totalskins * loadmodel->num_surfaces, name, false, true, (r_mipskins.integer ? TEXF_MIPMAP : 0) | TEXF_ALPHA | TEXF_PICMIP | TEXF_COMPRESS))
					Mod_BuildAliasSkinFromSkinFrame(loadmodel->data_textures + totalskins * loadmodel->num_surfaces, R_SkinFrame_LoadInternalQuake(name, (r_mipskins.integer ? TEXF_MIPMAP : 0) | TEXF_PICMIP, true, r_fullbrights.integer, (unsigned char *)datapointer, skinwidth, skinheight));
				datapointer += skinwidth * skinheight;
				totalskins++;
			}
		}
		// check for skins that don't exist in the model, but do exist as external images
		// (this was added because yummyluv kept pestering me about support for it)
		// TODO: support shaders here?
		while ((tempskinframe = R_SkinFrame_LoadExternal(va("%s_%i", loadmodel->name, loadmodel->numskins), (r_mipskins.integer ? TEXF_MIPMAP : 0) | TEXF_ALPHA | TEXF_PICMIP | TEXF_COMPRESS, false)))
		{
			// expand the arrays to make room
			tempskinscenes = loadmodel->skinscenes;
			loadmodel->skinscenes = (animscene_t *)Mem_Alloc(loadmodel->mempool, (loadmodel->numskins + 1) * sizeof(animscene_t));
			memcpy(loadmodel->skinscenes, tempskinscenes, loadmodel->numskins * sizeof(animscene_t));
			Mem_Free(tempskinscenes);

			tempaliasskins = loadmodel->data_textures;
			loadmodel->data_textures = (texture_t *)Mem_Alloc(loadmodel->mempool, loadmodel->num_surfaces * (totalskins + 1) * sizeof(texture_t));
			memcpy(loadmodel->data_textures, tempaliasskins, loadmodel->num_surfaces * totalskins * sizeof(texture_t));
			Mem_Free(tempaliasskins);

			// store the info about the new skin
			Mod_BuildAliasSkinFromSkinFrame(loadmodel->data_textures + totalskins * loadmodel->num_surfaces, tempskinframe);
			strlcpy(loadmodel->skinscenes[loadmodel->numskins].name, name, sizeof(loadmodel->skinscenes[loadmodel->numskins].name));
			loadmodel->skinscenes[loadmodel->numskins].firstframe = totalskins;
			loadmodel->skinscenes[loadmodel->numskins].framecount = 1;
			loadmodel->skinscenes[loadmodel->numskins].framerate = 10.0f;
			loadmodel->skinscenes[loadmodel->numskins].loop = true;

			//increase skin counts
			loadmodel->numskins++;
			totalskins++;

			// fix up the pointers since they are pointing at the old textures array
			// FIXME: this is a hack!
			for (j = 0;j < loadmodel->numskins * loadmodel->num_surfaces;j++)
				loadmodel->data_textures[j].currentframe = &loadmodel->data_textures[j];
		}
	}

	surface = loadmodel->data_surfaces;
	surface->texture = loadmodel->data_textures;
	surface->num_firsttriangle = 0;
	surface->num_triangles = loadmodel->surfmesh.num_triangles;
	surface->num_firstvertex = 0;
	surface->num_vertices = loadmodel->surfmesh.num_vertices;

	loadmodel->surfmesh.isanimated = loadmodel->numframes > 1 || loadmodel->animscenes[0].framecount > 1;

	if (!loadmodel->surfmesh.isanimated)
	{
		Mod_MakeCollisionBIH(loadmodel, true, &loadmodel->collision_bih);
		loadmodel->TraceBox = Mod_CollisionBIH_TraceBox;
		loadmodel->TraceBrush = Mod_CollisionBIH_TraceBrush;
		loadmodel->TraceLine = Mod_CollisionBIH_TraceLine;
		loadmodel->TracePoint = Mod_CollisionBIH_TracePoint_Mesh;
		loadmodel->PointSuperContents = Mod_CollisionBIH_PointSuperContents_Mesh;
	}
}

void Mod_IDP2_Load(dp_model_t *mod, void *buffer, void *bufferend)
{
	int i, j, hashindex, numxyz, numst, xyz, st, skinwidth, skinheight, *vertremap, version, end;
	float iskinwidth, iskinheight;
	unsigned char *data;
	msurface_t *surface;
	md2_t *pinmodel;
	unsigned char *base, *datapointer;
	md2frame_t *pinframe;
	char *inskin;
	md2triangle_t *intri;
	unsigned short *inst;
	struct md2verthash_s
	{
		struct md2verthash_s *next;
		unsigned short xyz;
		unsigned short st;
	}
	*hash, **md2verthash, *md2verthashdata;
	skinfile_t *skinfiles;

	pinmodel = (md2_t *)buffer;
	base = (unsigned char *)buffer;

	version = LittleLong (pinmodel->version);
	if (version != MD2ALIAS_VERSION)
		Host_Error ("%s has wrong version number (%i should be %i)",
			loadmodel->name, version, MD2ALIAS_VERSION);

	loadmodel->modeldatatypestring = "MD2";

	loadmodel->type = mod_alias;
	loadmodel->AnimateVertices = Mod_MDL_AnimateVertices;
	loadmodel->DrawSky = NULL;
	loadmodel->DrawAddWaterPlanes = NULL;
	loadmodel->Draw = R_Q1BSP_Draw;
	loadmodel->DrawDepth = R_Q1BSP_DrawDepth;
	loadmodel->DrawDebug = R_Q1BSP_DrawDebug;
	loadmodel->DrawPrepass = R_Q1BSP_DrawPrepass;
	loadmodel->CompileShadowMap = R_Q1BSP_CompileShadowMap;
	loadmodel->DrawShadowMap = R_Q1BSP_DrawShadowMap;
	loadmodel->CompileShadowVolume = R_Q1BSP_CompileShadowVolume;
	loadmodel->DrawShadowVolume = R_Q1BSP_DrawShadowVolume;
	loadmodel->DrawLight = R_Q1BSP_DrawLight;
	loadmodel->TraceBox = Mod_MDLMD2MD3_TraceBox;
	loadmodel->TraceLine = Mod_MDLMD2MD3_TraceLine;
	loadmodel->PointSuperContents = NULL;

	if (LittleLong(pinmodel->num_tris) < 1 || LittleLong(pinmodel->num_tris) > 65536)
		Host_Error ("%s has invalid number of triangles: %i", loadmodel->name, LittleLong(pinmodel->num_tris));
	if (LittleLong(pinmodel->num_xyz) < 1 || LittleLong(pinmodel->num_xyz) > 65536)
		Host_Error ("%s has invalid number of vertices: %i", loadmodel->name, LittleLong(pinmodel->num_xyz));
	if (LittleLong(pinmodel->num_frames) < 1 || LittleLong(pinmodel->num_frames) > 65536)
		Host_Error ("%s has invalid number of frames: %i", loadmodel->name, LittleLong(pinmodel->num_frames));
	if (LittleLong(pinmodel->num_skins) < 0 || LittleLong(pinmodel->num_skins) > 256)
		Host_Error ("%s has invalid number of skins: %i", loadmodel->name, LittleLong(pinmodel->num_skins));

	end = LittleLong(pinmodel->ofs_end);
	if (LittleLong(pinmodel->num_skins) >= 1 && (LittleLong(pinmodel->ofs_skins) <= 0 || LittleLong(pinmodel->ofs_skins) >= end))
		Host_Error ("%s is not a valid model", loadmodel->name);
	if (LittleLong(pinmodel->ofs_st) <= 0 || LittleLong(pinmodel->ofs_st) >= end)
		Host_Error ("%s is not a valid model", loadmodel->name);
	if (LittleLong(pinmodel->ofs_tris) <= 0 || LittleLong(pinmodel->ofs_tris) >= end)
		Host_Error ("%s is not a valid model", loadmodel->name);
	if (LittleLong(pinmodel->ofs_frames) <= 0 || LittleLong(pinmodel->ofs_frames) >= end)
		Host_Error ("%s is not a valid model", loadmodel->name);
	if (LittleLong(pinmodel->ofs_glcmds) <= 0 || LittleLong(pinmodel->ofs_glcmds) >= end)
		Host_Error ("%s is not a valid model", loadmodel->name);

	loadmodel->numskins = LittleLong(pinmodel->num_skins);
	numxyz = LittleLong(pinmodel->num_xyz);
	numst = LittleLong(pinmodel->num_st);
	loadmodel->surfmesh.num_triangles = LittleLong(pinmodel->num_tris);
	loadmodel->numframes = LittleLong(pinmodel->num_frames);
	loadmodel->surfmesh.num_morphframes = loadmodel->numframes;
	loadmodel->num_poses = loadmodel->surfmesh.num_morphframes;
	skinwidth = LittleLong(pinmodel->skinwidth);
	skinheight = LittleLong(pinmodel->skinheight);
	iskinwidth = 1.0f / skinwidth;
	iskinheight = 1.0f / skinheight;

	loadmodel->num_surfaces = 1;
	loadmodel->nummodelsurfaces = loadmodel->num_surfaces;
	data = (unsigned char *)Mem_Alloc(loadmodel->mempool, loadmodel->num_surfaces * sizeof(msurface_t) + loadmodel->num_surfaces * sizeof(int) + loadmodel->numframes * sizeof(animscene_t) + loadmodel->numframes * sizeof(float[6]) + loadmodel->surfmesh.num_triangles * sizeof(int[3]) + (r_enableshadowvolumes.integer ? loadmodel->surfmesh.num_triangles * sizeof(int[3]) : 0));
	loadmodel->data_surfaces = (msurface_t *)data;data += loadmodel->num_surfaces * sizeof(msurface_t);
	loadmodel->sortedmodelsurfaces = (int *)data;data += loadmodel->num_surfaces * sizeof(int);
	loadmodel->sortedmodelsurfaces[0] = 0;
	loadmodel->animscenes = (animscene_t *)data;data += loadmodel->numframes * sizeof(animscene_t);
	loadmodel->surfmesh.data_morphmd2framesize6f = (float *)data;data += loadmodel->numframes * sizeof(float[6]);
	loadmodel->surfmesh.data_element3i = (int *)data;data += loadmodel->surfmesh.num_triangles * sizeof(int[3]);
	if (r_enableshadowvolumes.integer)
	{
		loadmodel->surfmesh.data_neighbor3i = (int *)data;data += loadmodel->surfmesh.num_triangles * sizeof(int[3]);
	}

	loadmodel->synctype = ST_RAND;

	// load the skins
	inskin = (char *)(base + LittleLong(pinmodel->ofs_skins));
	skinfiles = Mod_LoadSkinFiles();
	if (skinfiles)
	{
		loadmodel->num_textures = loadmodel->num_surfaces * loadmodel->numskins;
		loadmodel->num_texturesperskin = loadmodel->num_surfaces;
		loadmodel->data_textures = (texture_t *)Mem_Alloc(loadmodel->mempool, loadmodel->num_surfaces * loadmodel->numskins * sizeof(texture_t));
		Mod_BuildAliasSkinsFromSkinFiles(loadmodel->data_textures, skinfiles, "default", "");
		Mod_FreeSkinFiles(skinfiles);
	}
	else if (loadmodel->numskins)
	{
		// skins found (most likely not a player model)
		loadmodel->num_textures = loadmodel->num_surfaces * loadmodel->numskins;
		loadmodel->num_texturesperskin = loadmodel->num_surfaces;
		loadmodel->data_textures = (texture_t *)Mem_Alloc(loadmodel->mempool, loadmodel->num_surfaces * loadmodel->numskins * sizeof(texture_t));
		for (i = 0;i < loadmodel->numskins;i++, inskin += MD2_SKINNAME)
			Mod_LoadTextureFromQ3Shader(loadmodel->data_textures + i * loadmodel->num_surfaces, inskin, true, true, (r_mipskins.integer ? TEXF_MIPMAP : 0) | TEXF_ALPHA | TEXF_PICMIP | TEXF_COMPRESS);
	}
	else
	{
		// no skins (most likely a player model)
		loadmodel->numskins = 1;
		loadmodel->num_textures = loadmodel->num_surfaces * loadmodel->numskins;
		loadmodel->num_texturesperskin = loadmodel->num_surfaces;
		loadmodel->data_textures = (texture_t *)Mem_Alloc(loadmodel->mempool, loadmodel->num_surfaces * loadmodel->numskins * sizeof(texture_t));
		Mod_BuildAliasSkinFromSkinFrame(loadmodel->data_textures, NULL);
	}

	loadmodel->skinscenes = (animscene_t *)Mem_Alloc(loadmodel->mempool, sizeof(animscene_t) * loadmodel->numskins);
	for (i = 0;i < loadmodel->numskins;i++)
	{
		loadmodel->skinscenes[i].firstframe = i;
		loadmodel->skinscenes[i].framecount = 1;
		loadmodel->skinscenes[i].loop = true;
		loadmodel->skinscenes[i].framerate = 10;
	}

	// load the triangles and stvert data
	inst = (unsigned short *)(base + LittleLong(pinmodel->ofs_st));
	intri = (md2triangle_t *)(base + LittleLong(pinmodel->ofs_tris));
	md2verthash = (struct md2verthash_s **)Mem_Alloc(tempmempool, 65536 * sizeof(hash));
	md2verthashdata = (struct md2verthash_s *)Mem_Alloc(tempmempool, loadmodel->surfmesh.num_triangles * 3 * sizeof(*hash));
	// swap the triangle list
	loadmodel->surfmesh.num_vertices = 0;
	for (i = 0;i < loadmodel->surfmesh.num_triangles;i++)
	{
		for (j = 0;j < 3;j++)
		{
			xyz = (unsigned short) LittleShort (intri[i].index_xyz[j]);
			st = (unsigned short) LittleShort (intri[i].index_st[j]);
			if (xyz >= numxyz)
			{
				Con_Printf("%s has an invalid xyz index (%i) on triangle %i, resetting to 0\n", loadmodel->name, xyz, i);
				xyz = 0;
			}
			if (st >= numst)
			{
				Con_Printf("%s has an invalid st index (%i) on triangle %i, resetting to 0\n", loadmodel->name, st, i);
				st = 0;
			}
			hashindex = (xyz * 256 + st) & 65535;
			for (hash = md2verthash[hashindex];hash;hash = hash->next)
				if (hash->xyz == xyz && hash->st == st)
					break;
			if (hash == NULL)
			{
				hash = md2verthashdata + loadmodel->surfmesh.num_vertices++;
				hash->xyz = xyz;
				hash->st = st;
				hash->next = md2verthash[hashindex];
				md2verthash[hashindex] = hash;
			}
			loadmodel->surfmesh.data_element3i[i*3+j] = (hash - md2verthashdata);
		}
	}

	vertremap = (int *)Mem_Alloc(loadmodel->mempool, loadmodel->surfmesh.num_vertices * sizeof(int));
	data = (unsigned char *)Mem_Alloc(loadmodel->mempool, loadmodel->surfmesh.num_vertices * sizeof(float[2]) + loadmodel->surfmesh.num_vertices * loadmodel->surfmesh.num_morphframes * sizeof(trivertx_t));
	loadmodel->surfmesh.data_texcoordtexture2f = (float *)data;data += loadmodel->surfmesh.num_vertices * sizeof(float[2]);
	loadmodel->surfmesh.data_morphmdlvertex = (trivertx_t *)data;data += loadmodel->surfmesh.num_vertices * loadmodel->surfmesh.num_morphframes * sizeof(trivertx_t);
	for (i = 0;i < loadmodel->surfmesh.num_vertices;i++)
	{
		int sts, stt;
		hash = md2verthashdata + i;
		vertremap[i] = hash->xyz;
		sts = LittleShort(inst[hash->st*2+0]);
		stt = LittleShort(inst[hash->st*2+1]);
		if (sts < 0 || sts >= skinwidth || stt < 0 || stt >= skinheight)
		{
			Con_Printf("%s has an invalid skin coordinate (%i %i) on vert %i, changing to 0 0\n", loadmodel->name, sts, stt, i);
			sts = 0;
			stt = 0;
		}
		loadmodel->surfmesh.data_texcoordtexture2f[i*2+0] = sts * iskinwidth;
		loadmodel->surfmesh.data_texcoordtexture2f[i*2+1] = stt * iskinheight;
	}

	Mem_Free(md2verthash);
	Mem_Free(md2verthashdata);

	// generate ushort elements array if possible
	if (loadmodel->surfmesh.num_vertices <= 65536)
		loadmodel->surfmesh.data_element3s = (unsigned short *)Mem_Alloc(loadmodel->mempool, sizeof(unsigned short[3]) * loadmodel->surfmesh.num_triangles);
	if (loadmodel->surfmesh.data_element3s)
		for (i = 0;i < loadmodel->surfmesh.num_triangles*3;i++)
			loadmodel->surfmesh.data_element3s[i] = loadmodel->surfmesh.data_element3i[i];

	// load the frames
	datapointer = (base + LittleLong(pinmodel->ofs_frames));
	for (i = 0;i < loadmodel->surfmesh.num_morphframes;i++)
	{
		int k;
		trivertx_t *v;
		trivertx_t *out;
		pinframe = (md2frame_t *)datapointer;
		datapointer += sizeof(md2frame_t);
		// store the frame scale/translate into the appropriate array
		for (j = 0;j < 3;j++)
		{
			loadmodel->surfmesh.data_morphmd2framesize6f[i*6+j] = LittleFloat(pinframe->scale[j]);
			loadmodel->surfmesh.data_morphmd2framesize6f[i*6+3+j] = LittleFloat(pinframe->translate[j]);
		}
		// convert the vertices
		v = (trivertx_t *)datapointer;
		out = loadmodel->surfmesh.data_morphmdlvertex + i * loadmodel->surfmesh.num_vertices;
		for (k = 0;k < loadmodel->surfmesh.num_vertices;k++)
			out[k] = v[vertremap[k]];
		datapointer += numxyz * sizeof(trivertx_t);

		strlcpy(loadmodel->animscenes[i].name, pinframe->name, sizeof(loadmodel->animscenes[i].name));
		loadmodel->animscenes[i].firstframe = i;
		loadmodel->animscenes[i].framecount = 1;
		loadmodel->animscenes[i].framerate = 10;
		loadmodel->animscenes[i].loop = true;
	}

	Mem_Free(vertremap);

	if (loadmodel->surfmesh.data_neighbor3i)
		Mod_BuildTriangleNeighbors(loadmodel->surfmesh.data_neighbor3i, loadmodel->surfmesh.data_element3i, loadmodel->surfmesh.num_triangles);
	Mod_Alias_CalculateBoundingBox();
	Mod_Alias_MorphMesh_CompileFrames();

	surface = loadmodel->data_surfaces;
	surface->texture = loadmodel->data_textures;
	surface->num_firsttriangle = 0;
	surface->num_triangles = loadmodel->surfmesh.num_triangles;
	surface->num_firstvertex = 0;
	surface->num_vertices = loadmodel->surfmesh.num_vertices;

	loadmodel->surfmesh.isanimated = loadmodel->numframes > 1 || loadmodel->animscenes[0].framecount > 1;

	if (!loadmodel->surfmesh.isanimated)
	{
		Mod_MakeCollisionBIH(loadmodel, true, &loadmodel->collision_bih);
		loadmodel->TraceBox = Mod_CollisionBIH_TraceBox;
		loadmodel->TraceBrush = Mod_CollisionBIH_TraceBrush;
		loadmodel->TraceLine = Mod_CollisionBIH_TraceLine;
		loadmodel->TracePoint = Mod_CollisionBIH_TracePoint_Mesh;
		loadmodel->PointSuperContents = Mod_CollisionBIH_PointSuperContents_Mesh;
	}
}

void Mod_IDP3_Load(dp_model_t *mod, void *buffer, void *bufferend)
{
	int i, j, k, version, meshvertices, meshtriangles;
	unsigned char *data;
	msurface_t *surface;
	md3modelheader_t *pinmodel;
	md3frameinfo_t *pinframe;
	md3mesh_t *pinmesh;
	md3tag_t *pintag;
	skinfile_t *skinfiles;

	pinmodel = (md3modelheader_t *)buffer;

	if (memcmp(pinmodel->identifier, "IDP3", 4))
		Host_Error ("%s is not a MD3 (IDP3) file", loadmodel->name);
	version = LittleLong (pinmodel->version);
	if (version != MD3VERSION)
		Host_Error ("%s has wrong version number (%i should be %i)",
			loadmodel->name, version, MD3VERSION);

	skinfiles = Mod_LoadSkinFiles();
	if (loadmodel->numskins < 1)
		loadmodel->numskins = 1;

	loadmodel->modeldatatypestring = "MD3";

	loadmodel->type = mod_alias;
	loadmodel->AnimateVertices = Mod_MD3_AnimateVertices;
	loadmodel->DrawSky = NULL;
	loadmodel->DrawAddWaterPlanes = NULL;
	loadmodel->Draw = R_Q1BSP_Draw;
	loadmodel->DrawDepth = R_Q1BSP_DrawDepth;
	loadmodel->DrawDebug = R_Q1BSP_DrawDebug;
	loadmodel->DrawPrepass = R_Q1BSP_DrawPrepass;
	loadmodel->CompileShadowMap = R_Q1BSP_CompileShadowMap;
	loadmodel->DrawShadowMap = R_Q1BSP_DrawShadowMap;
	loadmodel->CompileShadowVolume = R_Q1BSP_CompileShadowVolume;
	loadmodel->DrawShadowVolume = R_Q1BSP_DrawShadowVolume;
	loadmodel->DrawLight = R_Q1BSP_DrawLight;
	loadmodel->TraceBox = Mod_MDLMD2MD3_TraceBox;
	loadmodel->TraceLine = Mod_MDLMD2MD3_TraceLine;
	loadmodel->PointSuperContents = NULL;
	loadmodel->synctype = ST_RAND;
	// convert model flags to EF flags (MF_ROCKET becomes EF_ROCKET, etc)
	i = LittleLong (pinmodel->flags);
	loadmodel->effects = ((i & 255) << 24) | (i & 0x00FFFF00);

	// set up some global info about the model
	loadmodel->numframes = LittleLong(pinmodel->num_frames);
	loadmodel->num_surfaces = LittleLong(pinmodel->num_meshes);

	// make skinscenes for the skins (no groups)
	loadmodel->skinscenes = (animscene_t *)Mem_Alloc(loadmodel->mempool, sizeof(animscene_t) * loadmodel->numskins);
	for (i = 0;i < loadmodel->numskins;i++)
	{
		loadmodel->skinscenes[i].firstframe = i;
		loadmodel->skinscenes[i].framecount = 1;
		loadmodel->skinscenes[i].loop = true;
		loadmodel->skinscenes[i].framerate = 10;
	}

	// load frameinfo
	loadmodel->animscenes = (animscene_t *)Mem_Alloc(loadmodel->mempool, loadmodel->numframes * sizeof(animscene_t));
	for (i = 0, pinframe = (md3frameinfo_t *)((unsigned char *)pinmodel + LittleLong(pinmodel->lump_frameinfo));i < loadmodel->numframes;i++, pinframe++)
	{
		strlcpy(loadmodel->animscenes[i].name, pinframe->name, sizeof(loadmodel->animscenes[i].name));
		loadmodel->animscenes[i].firstframe = i;
		loadmodel->animscenes[i].framecount = 1;
		loadmodel->animscenes[i].framerate = 10;
		loadmodel->animscenes[i].loop = true;
	}

	// load tags
	loadmodel->num_tagframes = loadmodel->numframes;
	loadmodel->num_tags = LittleLong(pinmodel->num_tags);
	loadmodel->data_tags = (aliastag_t *)Mem_Alloc(loadmodel->mempool, loadmodel->num_tagframes * loadmodel->num_tags * sizeof(aliastag_t));
	for (i = 0, pintag = (md3tag_t *)((unsigned char *)pinmodel + LittleLong(pinmodel->lump_tags));i < loadmodel->num_tagframes * loadmodel->num_tags;i++, pintag++)
	{
		strlcpy(loadmodel->data_tags[i].name, pintag->name, sizeof(loadmodel->data_tags[i].name));
		for (j = 0;j < 9;j++)
			loadmodel->data_tags[i].matrixgl[j] = LittleFloat(pintag->rotationmatrix[j]);
		for (j = 0;j < 3;j++)
			loadmodel->data_tags[i].matrixgl[9+j] = LittleFloat(pintag->origin[j]);
		//Con_Printf("model \"%s\" frame #%i tag #%i \"%s\"\n", loadmodel->name, i / loadmodel->num_tags, i % loadmodel->num_tags, loadmodel->data_tags[i].name);
	}

	// load meshes
	meshvertices = 0;
	meshtriangles = 0;
	for (i = 0, pinmesh = (md3mesh_t *)((unsigned char *)pinmodel + LittleLong(pinmodel->lump_meshes));i < loadmodel->num_surfaces;i++, pinmesh = (md3mesh_t *)((unsigned char *)pinmesh + LittleLong(pinmesh->lump_end)))
	{
		if (memcmp(pinmesh->identifier, "IDP3", 4))
			Host_Error("Mod_IDP3_Load: invalid mesh identifier (not IDP3)");
		if (LittleLong(pinmesh->num_frames) != loadmodel->numframes)
			Host_Error("Mod_IDP3_Load: mesh numframes differs from header");
		meshvertices += LittleLong(pinmesh->num_vertices);
		meshtriangles += LittleLong(pinmesh->num_triangles);
	}

	loadmodel->nummodelsurfaces = loadmodel->num_surfaces;
	loadmodel->num_textures = loadmodel->num_surfaces * loadmodel->numskins;
	loadmodel->num_texturesperskin = loadmodel->num_surfaces;
	data = (unsigned char *)Mem_Alloc(loadmodel->mempool, loadmodel->num_surfaces * sizeof(msurface_t) + loadmodel->num_surfaces * sizeof(int) + loadmodel->num_surfaces * loadmodel->numskins * sizeof(texture_t) + meshtriangles * sizeof(int[3]) + (r_enableshadowvolumes.integer ? meshtriangles * sizeof(int[3]) : 0) + (meshvertices <= 65536 ? meshtriangles * sizeof(unsigned short[3]) : 0) + meshvertices * sizeof(float[2]) + meshvertices * loadmodel->numframes * sizeof(md3vertex_t));
	loadmodel->data_surfaces = (msurface_t *)data;data += loadmodel->num_surfaces * sizeof(msurface_t);
	loadmodel->sortedmodelsurfaces = (int *)data;data += loadmodel->num_surfaces * sizeof(int);
	loadmodel->data_textures = (texture_t *)data;data += loadmodel->num_surfaces * loadmodel->numskins * sizeof(texture_t);
	loadmodel->surfmesh.num_vertices = meshvertices;
	loadmodel->surfmesh.num_triangles = meshtriangles;
	loadmodel->surfmesh.num_morphframes = loadmodel->numframes; // TODO: remove?
	loadmodel->num_poses = loadmodel->surfmesh.num_morphframes;
	loadmodel->surfmesh.data_element3i = (int *)data;data += meshtriangles * sizeof(int[3]);
	if (r_enableshadowvolumes.integer)
	{
		loadmodel->surfmesh.data_neighbor3i = (int *)data;data += meshtriangles * sizeof(int[3]);
	}
	loadmodel->surfmesh.data_texcoordtexture2f = (float *)data;data += meshvertices * sizeof(float[2]);
	loadmodel->surfmesh.data_morphmd3vertex = (md3vertex_t *)data;data += meshvertices * loadmodel->numframes * sizeof(md3vertex_t);
	if (meshvertices <= 65536)
	{
		loadmodel->surfmesh.data_element3s = (unsigned short *)data;data += meshtriangles * sizeof(unsigned short[3]);
	}

	meshvertices = 0;
	meshtriangles = 0;
	for (i = 0, pinmesh = (md3mesh_t *)((unsigned char *)pinmodel + LittleLong(pinmodel->lump_meshes));i < loadmodel->num_surfaces;i++, pinmesh = (md3mesh_t *)((unsigned char *)pinmesh + LittleLong(pinmesh->lump_end)))
	{
		if (memcmp(pinmesh->identifier, "IDP3", 4))
			Host_Error("Mod_IDP3_Load: invalid mesh identifier (not IDP3)");
		loadmodel->sortedmodelsurfaces[i] = i;
		surface = loadmodel->data_surfaces + i;
		surface->texture = loadmodel->data_textures + i;
		surface->num_firsttriangle = meshtriangles;
		surface->num_triangles = LittleLong(pinmesh->num_triangles);
		surface->num_firstvertex = meshvertices;
		surface->num_vertices = LittleLong(pinmesh->num_vertices);
		meshvertices += surface->num_vertices;
		meshtriangles += surface->num_triangles;

		for (j = 0;j < surface->num_triangles * 3;j++)
			loadmodel->surfmesh.data_element3i[j + surface->num_firsttriangle * 3] = surface->num_firstvertex + LittleLong(((int *)((unsigned char *)pinmesh + LittleLong(pinmesh->lump_elements)))[j]);
		for (j = 0;j < surface->num_vertices;j++)
		{
			loadmodel->surfmesh.data_texcoordtexture2f[(j + surface->num_firstvertex) * 2 + 0] = LittleFloat(((float *)((unsigned char *)pinmesh + LittleLong(pinmesh->lump_texcoords)))[j * 2 + 0]);
			loadmodel->surfmesh.data_texcoordtexture2f[(j + surface->num_firstvertex) * 2 + 1] = LittleFloat(((float *)((unsigned char *)pinmesh + LittleLong(pinmesh->lump_texcoords)))[j * 2 + 1]);
		}
		for (j = 0;j < loadmodel->numframes;j++)
		{
			const md3vertex_t *in = (md3vertex_t *)((unsigned char *)pinmesh + LittleLong(pinmesh->lump_framevertices)) + j * surface->num_vertices;
			md3vertex_t *out = loadmodel->surfmesh.data_morphmd3vertex + surface->num_firstvertex + j * loadmodel->surfmesh.num_vertices;
			for (k = 0;k < surface->num_vertices;k++, in++, out++)
			{
				out->origin[0] = LittleShort(in->origin[0]);
				out->origin[1] = LittleShort(in->origin[1]);
				out->origin[2] = LittleShort(in->origin[2]);
				out->pitch = in->pitch;
				out->yaw = in->yaw;
			}
		}

		Mod_BuildAliasSkinsFromSkinFiles(loadmodel->data_textures + i, skinfiles, pinmesh->name, LittleLong(pinmesh->num_shaders) >= 1 ? ((md3shader_t *)((unsigned char *) pinmesh + LittleLong(pinmesh->lump_shaders)))->name : "");

		Mod_ValidateElements(loadmodel->surfmesh.data_element3i + surface->num_firsttriangle * 3, surface->num_triangles, surface->num_firstvertex, surface->num_vertices, __FILE__, __LINE__);
	}
	if (loadmodel->surfmesh.data_element3s)
		for (i = 0;i < loadmodel->surfmesh.num_triangles*3;i++)
			loadmodel->surfmesh.data_element3s[i] = loadmodel->surfmesh.data_element3i[i];
	if (loadmodel->surfmesh.data_neighbor3i)
		Mod_BuildTriangleNeighbors(loadmodel->surfmesh.data_neighbor3i, loadmodel->surfmesh.data_element3i, loadmodel->surfmesh.num_triangles);
	Mod_Alias_MorphMesh_CompileFrames();
	Mod_Alias_CalculateBoundingBox();
	Mod_FreeSkinFiles(skinfiles);
	Mod_MakeSortedSurfaces(loadmodel);

	loadmodel->surfmesh.isanimated = loadmodel->numframes > 1
	     || (loadmodel->animscenes && loadmodel->animscenes[0].framecount > 1);

	if (!loadmodel->surfmesh.isanimated)
	{
		Mod_MakeCollisionBIH(loadmodel, true, &loadmodel->collision_bih);
		loadmodel->TraceBox = Mod_CollisionBIH_TraceBox;
		loadmodel->TraceBrush = Mod_CollisionBIH_TraceBrush;
		loadmodel->TraceLine = Mod_CollisionBIH_TraceLine;
		loadmodel->TracePoint = Mod_CollisionBIH_TracePoint_Mesh;
		loadmodel->PointSuperContents = Mod_CollisionBIH_PointSuperContents_Mesh;
	}
}

void Mod_ZYMOTICMODEL_Load(dp_model_t *mod, void *buffer, void *bufferend)
{
	zymtype1header_t *pinmodel, *pheader;
	unsigned char *pbase;
	int i, j, k, numposes, meshvertices, meshtriangles, *bonecount, *vertbonecounts, count, *renderlist, *renderlistend, *outelements;
	float modelradius, corner[2], *poses, *intexcoord2f, *outtexcoord2f, *bonepose, f, biggestorigin, tempvec[3], modelscale;
	zymvertex_t *verts, *vertdata;
	zymscene_t *scene;
	zymbone_t *bone;
	char *shadername;
	skinfile_t *skinfiles;
	unsigned char *data;
	msurface_t *surface;

	pinmodel = (zymtype1header_t *)buffer;
	pbase = (unsigned char *)buffer;
	if (memcmp(pinmodel->id, "ZYMOTICMODEL", 12))
		Host_Error ("Mod_ZYMOTICMODEL_Load: %s is not a zymotic model", loadmodel->name);
	if (BigLong(pinmodel->type) != 1)
		Host_Error ("Mod_ZYMOTICMODEL_Load: only type 1 (skeletal pose) models are currently supported (name = %s)", loadmodel->name);

	loadmodel->modeldatatypestring = "ZYM";

	loadmodel->type = mod_alias;
	loadmodel->synctype = ST_RAND;

	// byteswap header
	pheader = pinmodel;
	pheader->type = BigLong(pinmodel->type);
	pheader->filesize = BigLong(pinmodel->filesize);
	pheader->mins[0] = BigFloat(pinmodel->mins[0]);
	pheader->mins[1] = BigFloat(pinmodel->mins[1]);
	pheader->mins[2] = BigFloat(pinmodel->mins[2]);
	pheader->maxs[0] = BigFloat(pinmodel->maxs[0]);
	pheader->maxs[1] = BigFloat(pinmodel->maxs[1]);
	pheader->maxs[2] = BigFloat(pinmodel->maxs[2]);
	pheader->radius = BigFloat(pinmodel->radius);
	pheader->numverts = BigLong(pinmodel->numverts);
	pheader->numtris = BigLong(pinmodel->numtris);
	pheader->numshaders = BigLong(pinmodel->numshaders);
	pheader->numbones = BigLong(pinmodel->numbones);
	pheader->numscenes = BigLong(pinmodel->numscenes);
	pheader->lump_scenes.start = BigLong(pinmodel->lump_scenes.start);
	pheader->lump_scenes.length = BigLong(pinmodel->lump_scenes.length);
	pheader->lump_poses.start = BigLong(pinmodel->lump_poses.start);
	pheader->lump_poses.length = BigLong(pinmodel->lump_poses.length);
	pheader->lump_bones.start = BigLong(pinmodel->lump_bones.start);
	pheader->lump_bones.length = BigLong(pinmodel->lump_bones.length);
	pheader->lump_vertbonecounts.start = BigLong(pinmodel->lump_vertbonecounts.start);
	pheader->lump_vertbonecounts.length = BigLong(pinmodel->lump_vertbonecounts.length);
	pheader->lump_verts.start = BigLong(pinmodel->lump_verts.start);
	pheader->lump_verts.length = BigLong(pinmodel->lump_verts.length);
	pheader->lump_texcoords.start = BigLong(pinmodel->lump_texcoords.start);
	pheader->lump_texcoords.length = BigLong(pinmodel->lump_texcoords.length);
	pheader->lump_render.start = BigLong(pinmodel->lump_render.start);
	pheader->lump_render.length = BigLong(pinmodel->lump_render.length);
	pheader->lump_shaders.start = BigLong(pinmodel->lump_shaders.start);
	pheader->lump_shaders.length = BigLong(pinmodel->lump_shaders.length);
	pheader->lump_trizone.start = BigLong(pinmodel->lump_trizone.start);
	pheader->lump_trizone.length = BigLong(pinmodel->lump_trizone.length);

	if (pheader->numtris < 1 || pheader->numverts < 3 || pheader->numshaders < 1)
	{
		Con_Printf("%s has no geometry\n", loadmodel->name);
		return;
	}
	if (pheader->numscenes < 1 || pheader->lump_poses.length < (int)sizeof(float[3][4]))
	{
		Con_Printf("%s has no animations\n", loadmodel->name);
		return;
	}

	loadmodel->AnimateVertices = Mod_Skeletal_AnimateVertices;
	loadmodel->DrawSky = NULL;
	loadmodel->DrawAddWaterPlanes = NULL;
	loadmodel->Draw = R_Q1BSP_Draw;
	loadmodel->DrawDepth = R_Q1BSP_DrawDepth;
	loadmodel->DrawDebug = R_Q1BSP_DrawDebug;
	loadmodel->DrawPrepass = R_Q1BSP_DrawPrepass;
	loadmodel->CompileShadowMap = R_Q1BSP_CompileShadowMap;
	loadmodel->DrawShadowMap = R_Q1BSP_DrawShadowMap;
	loadmodel->CompileShadowVolume = R_Q1BSP_CompileShadowVolume;
	loadmodel->DrawShadowVolume = R_Q1BSP_DrawShadowVolume;
	loadmodel->DrawLight = R_Q1BSP_DrawLight;
	loadmodel->TraceBox = Mod_MDLMD2MD3_TraceBox;
	loadmodel->TraceLine = Mod_MDLMD2MD3_TraceLine;
	loadmodel->PointSuperContents = NULL;

	loadmodel->numframes = pheader->numscenes;
	loadmodel->num_surfaces = pheader->numshaders;

	skinfiles = Mod_LoadSkinFiles();
	if (loadmodel->numskins < 1)
		loadmodel->numskins = 1;

	// make skinscenes for the skins (no groups)
	loadmodel->skinscenes = (animscene_t *)Mem_Alloc(loadmodel->mempool, sizeof(animscene_t) * loadmodel->numskins);
	for (i = 0;i < loadmodel->numskins;i++)
	{
		loadmodel->skinscenes[i].firstframe = i;
		loadmodel->skinscenes[i].framecount = 1;
		loadmodel->skinscenes[i].loop = true;
		loadmodel->skinscenes[i].framerate = 10;
	}

	// model bbox
	modelradius = pheader->radius;
	for (i = 0;i < 3;i++)
	{
		loadmodel->normalmins[i] = pheader->mins[i];
		loadmodel->normalmaxs[i] = pheader->maxs[i];
		loadmodel->rotatedmins[i] = -modelradius;
		loadmodel->rotatedmaxs[i] = modelradius;
	}
	corner[0] = max(fabs(loadmodel->normalmins[0]), fabs(loadmodel->normalmaxs[0]));
	corner[1] = max(fabs(loadmodel->normalmins[1]), fabs(loadmodel->normalmaxs[1]));
	loadmodel->yawmaxs[0] = loadmodel->yawmaxs[1] = sqrt(corner[0]*corner[0]+corner[1]*corner[1]);
	if (loadmodel->yawmaxs[0] > modelradius)
		loadmodel->yawmaxs[0] = loadmodel->yawmaxs[1] = modelradius;
	loadmodel->yawmins[0] = loadmodel->yawmins[1] = -loadmodel->yawmaxs[0];
	loadmodel->yawmins[2] = loadmodel->normalmins[2];
	loadmodel->yawmaxs[2] = loadmodel->normalmaxs[2];
	loadmodel->radius = modelradius;
	loadmodel->radius2 = modelradius * modelradius;

	// go through the lumps, swapping things

	//zymlump_t lump_scenes; // zymscene_t scene[numscenes]; // name and other information for each scene (see zymscene struct)
	loadmodel->animscenes = (animscene_t *)Mem_Alloc(loadmodel->mempool, sizeof(animscene_t) * loadmodel->numframes);
	scene = (zymscene_t *) (pheader->lump_scenes.start + pbase);
	numposes = pheader->lump_poses.length / pheader->numbones / sizeof(float[3][4]);
	for (i = 0;i < pheader->numscenes;i++)
	{
		memcpy(loadmodel->animscenes[i].name, scene->name, 32);
		loadmodel->animscenes[i].firstframe = BigLong(scene->start);
		loadmodel->animscenes[i].framecount = BigLong(scene->length);
		loadmodel->animscenes[i].framerate = BigFloat(scene->framerate);
		loadmodel->animscenes[i].loop = (BigLong(scene->flags) & ZYMSCENEFLAG_NOLOOP) == 0;
		if ((unsigned int) loadmodel->animscenes[i].firstframe >= (unsigned int) numposes)
			Host_Error("%s scene->firstframe (%i) >= numposes (%i)", loadmodel->name, loadmodel->animscenes[i].firstframe, numposes);
		if ((unsigned int) loadmodel->animscenes[i].firstframe + (unsigned int) loadmodel->animscenes[i].framecount > (unsigned int) numposes)
			Host_Error("%s scene->firstframe (%i) + framecount (%i) >= numposes (%i)", loadmodel->name, loadmodel->animscenes[i].firstframe, loadmodel->animscenes[i].framecount, numposes);
		if (loadmodel->animscenes[i].framerate < 0)
			Host_Error("%s scene->framerate (%f) < 0", loadmodel->name, loadmodel->animscenes[i].framerate);
		scene++;
	}

	//zymlump_t lump_bones; // zymbone_t bone[numbones];
	loadmodel->num_bones = pheader->numbones;
	loadmodel->data_bones = (aliasbone_t *)Mem_Alloc(loadmodel->mempool, loadmodel->num_bones * sizeof(aliasbone_t));
	bone = (zymbone_t *) (pheader->lump_bones.start + pbase);
	for (i = 0;i < pheader->numbones;i++)
	{
		memcpy(loadmodel->data_bones[i].name, bone[i].name, sizeof(bone[i].name));
		loadmodel->data_bones[i].flags = BigLong(bone[i].flags);
		loadmodel->data_bones[i].parent = BigLong(bone[i].parent);
		if (loadmodel->data_bones[i].parent >= i)
			Host_Error("%s bone[%i].parent >= %i", loadmodel->name, i, i);
	}

	//zymlump_t lump_vertbonecounts; // int vertbonecounts[numvertices]; // how many bones influence each vertex (separate mainly to make this compress better)
	vertbonecounts = (int *)Mem_Alloc(loadmodel->mempool, pheader->numverts * sizeof(int));
	bonecount = (int *) (pheader->lump_vertbonecounts.start + pbase);
	for (i = 0;i < pheader->numverts;i++)
	{
		vertbonecounts[i] = BigLong(bonecount[i]);
		if (vertbonecounts[i] != 1)
			Host_Error("%s bonecount[%i] != 1 (vertex weight support is impossible in this format)", loadmodel->name, i);
	}

	loadmodel->num_poses = pheader->lump_poses.length / sizeof(float[3][4]) / loadmodel->num_bones;

	meshvertices = pheader->numverts;
	meshtriangles = pheader->numtris;

	loadmodel->nummodelsurfaces = loadmodel->num_surfaces;
	loadmodel->num_textures = loadmodel->num_surfaces * loadmodel->numskins;
	loadmodel->num_texturesperskin = loadmodel->num_surfaces;
	data = (unsigned char *)Mem_Alloc(loadmodel->mempool, loadmodel->num_surfaces * sizeof(msurface_t) + loadmodel->num_surfaces * sizeof(int) + loadmodel->num_surfaces * loadmodel->numskins * sizeof(texture_t) + meshtriangles * sizeof(int[3]) + (r_enableshadowvolumes.integer ? meshtriangles * sizeof(int[3]) : 0) + (meshvertices <= 65536 ? meshtriangles * sizeof(unsigned short[3]) : 0) + meshvertices * sizeof(float[14]) + meshvertices * sizeof(unsigned short) + loadmodel->num_poses * loadmodel->num_bones * sizeof(short[6]) + loadmodel->num_bones * sizeof(float[12]));
	loadmodel->data_surfaces = (msurface_t *)data;data += loadmodel->num_surfaces * sizeof(msurface_t);
	loadmodel->sortedmodelsurfaces = (int *)data;data += loadmodel->num_surfaces * sizeof(int);
	loadmodel->data_textures = (texture_t *)data;data += loadmodel->num_surfaces * loadmodel->numskins * sizeof(texture_t);
	loadmodel->surfmesh.num_vertices = meshvertices;
	loadmodel->surfmesh.num_triangles = meshtriangles;
	loadmodel->surfmesh.data_element3i = (int *)data;data += meshtriangles * sizeof(int[3]);
	if (r_enableshadowvolumes.integer)
	{
		loadmodel->surfmesh.data_neighbor3i = (int *)data;data += meshtriangles * sizeof(int[3]);
	}
	loadmodel->surfmesh.data_vertex3f = (float *)data;data += meshvertices * sizeof(float[3]);
	loadmodel->surfmesh.data_svector3f = (float *)data;data += meshvertices * sizeof(float[3]);
	loadmodel->surfmesh.data_tvector3f = (float *)data;data += meshvertices * sizeof(float[3]);
	loadmodel->surfmesh.data_normal3f = (float *)data;data += meshvertices * sizeof(float[3]);
	loadmodel->surfmesh.data_texcoordtexture2f = (float *)data;data += meshvertices * sizeof(float[2]);
	loadmodel->data_baseboneposeinverse = (float *)data;data += loadmodel->num_bones * sizeof(float[12]);
	loadmodel->surfmesh.num_blends = 0;
	loadmodel->surfmesh.blends = (unsigned short *)data;data += meshvertices * sizeof(unsigned short);
	if (loadmodel->surfmesh.num_vertices <= 65536)
	{
		loadmodel->surfmesh.data_element3s = (unsigned short *)data;data += loadmodel->surfmesh.num_triangles * sizeof(unsigned short[3]);
	}
	loadmodel->data_poses6s = (short *)data;data += loadmodel->num_poses * loadmodel->num_bones * sizeof(short[6]);
	loadmodel->surfmesh.data_blendweights = NULL;

	//zymlump_t lump_poses; // float pose[numposes][numbones][3][4]; // animation data
	poses = (float *) (pheader->lump_poses.start + pbase);
	// figure out scale of model from root bone, for compatibility with old zmodel versions
	tempvec[0] = BigFloat(poses[0]);
	tempvec[1] = BigFloat(poses[1]);
	tempvec[2] = BigFloat(poses[2]);
	modelscale = VectorLength(tempvec);
	biggestorigin = 0;
	for (i = 0;i < loadmodel->num_bones * numposes * 12;i++)
	{
		f = fabs(BigFloat(poses[i]));
		biggestorigin = max(biggestorigin, f);
	}
	loadmodel->num_posescale = biggestorigin / 32767.0f;
	loadmodel->num_poseinvscale = 1.0f / loadmodel->num_posescale;
	for (i = 0;i < numposes;i++)
	{
		const float *frameposes = (float *) (pheader->lump_poses.start + pbase) + 12*i*loadmodel->num_bones;
		for (j = 0;j < loadmodel->num_bones;j++)
		{
			float pose[12];
			matrix4x4_t posematrix;
			for (k = 0;k < 12;k++)
				pose[k] = BigFloat(frameposes[j*12+k]);
			//if (j < loadmodel->num_bones)
			//	Con_Printf("%s: bone %i = %f %f %f %f : %f %f %f %f : %f %f %f %f : scale = %f\n", loadmodel->name, j, pose[0], pose[1], pose[2], pose[3], pose[4], pose[5], pose[6], pose[7], pose[8], pose[9], pose[10], pose[11], VectorLength(pose));
			// scale child bones to match the root scale
			if (loadmodel->data_bones[j].parent >= 0)
			{
				pose[3] *= modelscale;
				pose[7] *= modelscale;
				pose[11] *= modelscale;
			}
			// normalize rotation matrix
			VectorNormalize(pose + 0);
			VectorNormalize(pose + 4);
			VectorNormalize(pose + 8);
			Matrix4x4_FromArray12FloatD3D(&posematrix, pose);
			Matrix4x4_ToBonePose6s(&posematrix, loadmodel->num_poseinvscale, loadmodel->data_poses6s + 6*(i*loadmodel->num_bones+j));
		}
	}

	//zymlump_t lump_verts; // zymvertex_t vert[numvertices]; // see vertex struct
	verts = (zymvertex_t *)Mem_Alloc(loadmodel->mempool, pheader->lump_verts.length);
	vertdata = (zymvertex_t *) (pheader->lump_verts.start + pbase);
	// reconstruct frame 0 matrices to allow reconstruction of the base mesh
	// (converting from weight-blending skeletal animation to
	//  deformation-based skeletal animation)
	bonepose = (float *)Z_Malloc(loadmodel->num_bones * sizeof(float[12]));
	for (i = 0;i < loadmodel->num_bones;i++)
	{
		float m[12];
		for (k = 0;k < 12;k++)
			m[k] = BigFloat(poses[i*12+k]);
		if (loadmodel->data_bones[i].parent >= 0)
			R_ConcatTransforms(bonepose + 12 * loadmodel->data_bones[i].parent, m, bonepose + 12 * i);
		else
			for (k = 0;k < 12;k++)
				bonepose[12*i+k] = m[k];
	}
	for (j = 0;j < pheader->numverts;j++)
	{
		// this format really should have had a per vertexweight weight value...
		// but since it does not, the weighting is completely ignored and
		// only one weight is allowed per vertex
		int boneindex = BigLong(vertdata[j].bonenum);
		const float *m = bonepose + 12 * boneindex;
		float relativeorigin[3];
		relativeorigin[0] = BigFloat(vertdata[j].origin[0]);
		relativeorigin[1] = BigFloat(vertdata[j].origin[1]);
		relativeorigin[2] = BigFloat(vertdata[j].origin[2]);
		// transform the vertex bone weight into the base mesh
		loadmodel->surfmesh.data_vertex3f[j*3+0] = relativeorigin[0] * m[0] + relativeorigin[1] * m[1] + relativeorigin[2] * m[ 2] + m[ 3];
		loadmodel->surfmesh.data_vertex3f[j*3+1] = relativeorigin[0] * m[4] + relativeorigin[1] * m[5] + relativeorigin[2] * m[ 6] + m[ 7];
		loadmodel->surfmesh.data_vertex3f[j*3+2] = relativeorigin[0] * m[8] + relativeorigin[1] * m[9] + relativeorigin[2] * m[10] + m[11];
		// store the weight as the primary weight on this vertex
		loadmodel->surfmesh.blends[j] = boneindex;
	}
	Z_Free(bonepose);
	// normals and tangents are calculated after elements are loaded

	//zymlump_t lump_texcoords; // float texcoords[numvertices][2];
	outtexcoord2f = loadmodel->surfmesh.data_texcoordtexture2f;
	intexcoord2f = (float *) (pheader->lump_texcoords.start + pbase);
	for (i = 0;i < pheader->numverts;i++)
	{
		outtexcoord2f[i*2+0] = BigFloat(intexcoord2f[i*2+0]);
		// flip T coordinate for OpenGL
		outtexcoord2f[i*2+1] = 1 - BigFloat(intexcoord2f[i*2+1]);
	}

	//zymlump_t lump_trizone; // byte trizone[numtris]; // see trizone explanation
	//loadmodel->alias.zymdata_trizone = Mem_Alloc(loadmodel->mempool, pheader->numtris);
	//memcpy(loadmodel->alias.zymdata_trizone, (void *) (pheader->lump_trizone.start + pbase), pheader->numtris);

	//zymlump_t lump_shaders; // char shadername[numshaders][32]; // shaders used on this model
	//zymlump_t lump_render; // int renderlist[rendersize]; // sorted by shader with run lengths (int count), shaders are sequentially used, each run can be used with glDrawElements (each triangle is 3 int indices)
	// byteswap, validate, and swap winding order of tris
	count = pheader->numshaders * sizeof(int) + pheader->numtris * sizeof(int[3]);
	if (pheader->lump_render.length != count)
		Host_Error("%s renderlist is wrong size (%i bytes, should be %i bytes)", loadmodel->name, pheader->lump_render.length, count);
	renderlist = (int *) (pheader->lump_render.start + pbase);
	renderlistend = (int *) ((unsigned char *) renderlist + pheader->lump_render.length);
	meshtriangles = 0;
	for (i = 0;i < loadmodel->num_surfaces;i++)
	{
		int firstvertex, lastvertex;
		if (renderlist >= renderlistend)
			Host_Error("%s corrupt renderlist (wrong size)", loadmodel->name);
		count = BigLong(*renderlist);renderlist++;
		if (renderlist + count * 3 > renderlistend || (i == pheader->numshaders - 1 && renderlist + count * 3 != renderlistend))
			Host_Error("%s corrupt renderlist (wrong size)", loadmodel->name);

		loadmodel->sortedmodelsurfaces[i] = i;
		surface = loadmodel->data_surfaces + i;
		surface->texture = loadmodel->data_textures + i;
		surface->num_firsttriangle = meshtriangles;
		surface->num_triangles = count;
		meshtriangles += surface->num_triangles;

		// load the elements
		outelements = loadmodel->surfmesh.data_element3i + surface->num_firsttriangle * 3;
		for (j = 0;j < surface->num_triangles;j++, renderlist += 3)
		{
			outelements[j*3+2] = BigLong(renderlist[0]);
			outelements[j*3+1] = BigLong(renderlist[1]);
			outelements[j*3+0] = BigLong(renderlist[2]);
		}
		// validate the elements and find the used vertex range
		firstvertex = meshvertices;
		lastvertex = 0;
		for (j = 0;j < surface->num_triangles * 3;j++)
		{
			if ((unsigned int)outelements[j] >= (unsigned int)meshvertices)
				Host_Error("%s corrupt renderlist (out of bounds index)", loadmodel->name);
			firstvertex = min(firstvertex, outelements[j]);
			lastvertex = max(lastvertex, outelements[j]);
		}
		surface->num_firstvertex = firstvertex;
		surface->num_vertices = lastvertex + 1 - firstvertex;

		// since zym models do not have named sections, reuse their shader
		// name as the section name
		shadername = (char *) (pheader->lump_shaders.start + pbase) + i * 32;
		Mod_BuildAliasSkinsFromSkinFiles(loadmodel->data_textures + i, skinfiles, shadername, shadername);
	}
	Mod_FreeSkinFiles(skinfiles);
	Mem_Free(vertbonecounts);
	Mem_Free(verts);
	Mod_MakeSortedSurfaces(loadmodel);

	// compute all the mesh information that was not loaded from the file
	if (loadmodel->surfmesh.data_element3s)
		for (i = 0;i < loadmodel->surfmesh.num_triangles*3;i++)
			loadmodel->surfmesh.data_element3s[i] = loadmodel->surfmesh.data_element3i[i];
	Mod_ValidateElements(loadmodel->surfmesh.data_element3i, loadmodel->surfmesh.num_triangles, 0, loadmodel->surfmesh.num_vertices, __FILE__, __LINE__);
	Mod_BuildBaseBonePoses();
	Mod_BuildNormals(0, loadmodel->surfmesh.num_vertices, loadmodel->surfmesh.num_triangles, loadmodel->surfmesh.data_vertex3f, loadmodel->surfmesh.data_element3i, loadmodel->surfmesh.data_normal3f, r_smoothnormals_areaweighting.integer != 0);
	Mod_BuildTextureVectorsFromNormals(0, loadmodel->surfmesh.num_vertices, loadmodel->surfmesh.num_triangles, loadmodel->surfmesh.data_vertex3f, loadmodel->surfmesh.data_texcoordtexture2f, loadmodel->surfmesh.data_normal3f, loadmodel->surfmesh.data_element3i, loadmodel->surfmesh.data_svector3f, loadmodel->surfmesh.data_tvector3f, r_smoothnormals_areaweighting.integer != 0);
	if (loadmodel->surfmesh.data_neighbor3i)
		Mod_BuildTriangleNeighbors(loadmodel->surfmesh.data_neighbor3i, loadmodel->surfmesh.data_element3i, loadmodel->surfmesh.num_triangles);

	loadmodel->surfmesh.isanimated = loadmodel->numframes > 1 || loadmodel->animscenes[0].framecount > 1;

	if (!loadmodel->surfmesh.isanimated)
	{
		Mod_MakeCollisionBIH(loadmodel, true, &loadmodel->collision_bih);
		loadmodel->TraceBox = Mod_CollisionBIH_TraceBox;
		loadmodel->TraceBrush = Mod_CollisionBIH_TraceBrush;
		loadmodel->TraceLine = Mod_CollisionBIH_TraceLine;
		loadmodel->TracePoint = Mod_CollisionBIH_TracePoint_Mesh;
		loadmodel->PointSuperContents = Mod_CollisionBIH_PointSuperContents_Mesh;
	}
}

void Mod_DARKPLACESMODEL_Load(dp_model_t *mod, void *buffer, void *bufferend)
{
	dpmheader_t *pheader;
	dpmframe_t *frames;
	dpmbone_t *bone;
	dpmmesh_t *dpmmesh;
	unsigned char *pbase;
	int i, j, k, meshvertices, meshtriangles;
	skinfile_t *skinfiles;
	unsigned char *data;
	float *bonepose;
	float biggestorigin, tempvec[3], modelscale;
	float f;
	float *poses;

	pheader = (dpmheader_t *)buffer;
	pbase = (unsigned char *)buffer;
	if (memcmp(pheader->id, "DARKPLACESMODEL\0", 16))
		Host_Error ("Mod_DARKPLACESMODEL_Load: %s is not a darkplaces model", loadmodel->name);
	if (BigLong(pheader->type) != 2)
		Host_Error ("Mod_DARKPLACESMODEL_Load: only type 2 (hierarchical skeletal pose) models are currently supported (name = %s)", loadmodel->name);

	loadmodel->modeldatatypestring = "DPM";

	loadmodel->type = mod_alias;
	loadmodel->synctype = ST_RAND;

	// byteswap header
	pheader->type = BigLong(pheader->type);
	pheader->filesize = BigLong(pheader->filesize);
	pheader->mins[0] = BigFloat(pheader->mins[0]);
	pheader->mins[1] = BigFloat(pheader->mins[1]);
	pheader->mins[2] = BigFloat(pheader->mins[2]);
	pheader->maxs[0] = BigFloat(pheader->maxs[0]);
	pheader->maxs[1] = BigFloat(pheader->maxs[1]);
	pheader->maxs[2] = BigFloat(pheader->maxs[2]);
	pheader->yawradius = BigFloat(pheader->yawradius);
	pheader->allradius = BigFloat(pheader->allradius);
	pheader->num_bones = BigLong(pheader->num_bones);
	pheader->num_meshs = BigLong(pheader->num_meshs);
	pheader->num_frames = BigLong(pheader->num_frames);
	pheader->ofs_bones = BigLong(pheader->ofs_bones);
	pheader->ofs_meshs = BigLong(pheader->ofs_meshs);
	pheader->ofs_frames = BigLong(pheader->ofs_frames);

	if (pheader->num_bones < 1 || pheader->num_meshs < 1)
	{
		Con_Printf("%s has no geometry\n", loadmodel->name);
		return;
	}
	if (pheader->num_frames < 1)
	{
		Con_Printf("%s has no frames\n", loadmodel->name);
		return;
	}

	loadmodel->AnimateVertices = Mod_Skeletal_AnimateVertices;
	loadmodel->DrawSky = NULL;
	loadmodel->DrawAddWaterPlanes = NULL;
	loadmodel->Draw = R_Q1BSP_Draw;
	loadmodel->DrawDepth = R_Q1BSP_DrawDepth;
	loadmodel->DrawDebug = R_Q1BSP_DrawDebug;
	loadmodel->DrawPrepass = R_Q1BSP_DrawPrepass;
	loadmodel->CompileShadowMap = R_Q1BSP_CompileShadowMap;
	loadmodel->DrawShadowMap = R_Q1BSP_DrawShadowMap;
	loadmodel->CompileShadowVolume = R_Q1BSP_CompileShadowVolume;
	loadmodel->DrawShadowVolume = R_Q1BSP_DrawShadowVolume;
	loadmodel->DrawLight = R_Q1BSP_DrawLight;
	loadmodel->TraceBox = Mod_MDLMD2MD3_TraceBox;
	loadmodel->TraceLine = Mod_MDLMD2MD3_TraceLine;
	loadmodel->PointSuperContents = NULL;

	// model bbox
	for (i = 0;i < 3;i++)
	{
		loadmodel->normalmins[i] = pheader->mins[i];
		loadmodel->normalmaxs[i] = pheader->maxs[i];
		loadmodel->yawmins[i] = i != 2 ? -pheader->yawradius : pheader->mins[i];
		loadmodel->yawmaxs[i] = i != 2 ? pheader->yawradius : pheader->maxs[i];
		loadmodel->rotatedmins[i] = -pheader->allradius;
		loadmodel->rotatedmaxs[i] = pheader->allradius;
	}
	loadmodel->radius = pheader->allradius;
	loadmodel->radius2 = pheader->allradius * pheader->allradius;

	// load external .skin files if present
	skinfiles = Mod_LoadSkinFiles();
	if (loadmodel->numskins < 1)
		loadmodel->numskins = 1;

	meshvertices = 0;
	meshtriangles = 0;

	// gather combined statistics from the meshes
	dpmmesh = (dpmmesh_t *) (pbase + pheader->ofs_meshs);
	for (i = 0;i < (int)pheader->num_meshs;i++)
	{
		int numverts = BigLong(dpmmesh->num_verts);
		meshvertices += numverts;
		meshtriangles += BigLong(dpmmesh->num_tris);
		dpmmesh++;
	}

	loadmodel->numframes = pheader->num_frames;
	loadmodel->num_bones = pheader->num_bones;
	loadmodel->num_poses = loadmodel->numframes;
	loadmodel->nummodelsurfaces = loadmodel->num_surfaces = pheader->num_meshs;
	loadmodel->num_textures = loadmodel->num_surfaces * loadmodel->numskins;
	loadmodel->num_texturesperskin = loadmodel->num_surfaces;
	// do most allocations as one merged chunk
	data = (unsigned char *)Mem_Alloc(loadmodel->mempool, loadmodel->num_surfaces * sizeof(msurface_t) + loadmodel->num_surfaces * sizeof(int) + loadmodel->num_surfaces * loadmodel->numskins * sizeof(texture_t) + meshtriangles * sizeof(int[3]) + (meshvertices <= 65536 ? meshtriangles * sizeof(unsigned short[3]) : 0) + (r_enableshadowvolumes.integer ? meshtriangles * sizeof(int[3]) : 0) + meshvertices * (sizeof(float[14]) + sizeof(unsigned short)) + loadmodel->num_poses * loadmodel->num_bones * sizeof(short[6]) + loadmodel->num_bones * sizeof(float[12]) + loadmodel->numskins * sizeof(animscene_t) + loadmodel->num_bones * sizeof(aliasbone_t) + loadmodel->numframes * sizeof(animscene_t));
	loadmodel->data_surfaces = (msurface_t *)data;data += loadmodel->num_surfaces * sizeof(msurface_t);
	loadmodel->sortedmodelsurfaces = (int *)data;data += loadmodel->num_surfaces * sizeof(int);
	loadmodel->data_textures = (texture_t *)data;data += loadmodel->num_surfaces * loadmodel->numskins * sizeof(texture_t);
	loadmodel->surfmesh.num_vertices = meshvertices;
	loadmodel->surfmesh.num_triangles = meshtriangles;
	loadmodel->surfmesh.data_element3i = (int *)data;data += meshtriangles * sizeof(int[3]);
	if (r_enableshadowvolumes.integer)
	{
		loadmodel->surfmesh.data_neighbor3i = (int *)data;data += meshtriangles * sizeof(int[3]);
	}
	loadmodel->surfmesh.data_vertex3f = (float *)data;data += meshvertices * sizeof(float[3]);
	loadmodel->surfmesh.data_svector3f = (float *)data;data += meshvertices * sizeof(float[3]);
	loadmodel->surfmesh.data_tvector3f = (float *)data;data += meshvertices * sizeof(float[3]);
	loadmodel->surfmesh.data_normal3f = (float *)data;data += meshvertices * sizeof(float[3]);
	loadmodel->surfmesh.data_texcoordtexture2f = (float *)data;data += meshvertices * sizeof(float[2]);
	loadmodel->data_baseboneposeinverse = (float *)data;data += loadmodel->num_bones * sizeof(float[12]);
	loadmodel->skinscenes = (animscene_t *)data;data += loadmodel->numskins * sizeof(animscene_t);
	loadmodel->data_bones = (aliasbone_t *)data;data += loadmodel->num_bones * sizeof(aliasbone_t);
	loadmodel->animscenes = (animscene_t *)data;data += loadmodel->numframes * sizeof(animscene_t);
	loadmodel->surfmesh.num_blends = 0;
	loadmodel->surfmesh.blends = (unsigned short *)data;data += meshvertices * sizeof(unsigned short);
	if (meshvertices <= 65536)
	{
		loadmodel->surfmesh.data_element3s = (unsigned short *)data;data += meshtriangles * sizeof(unsigned short[3]);
	}
	loadmodel->data_poses6s = (short *)data;data += loadmodel->num_poses * loadmodel->num_bones * sizeof(short[6]);
	loadmodel->surfmesh.data_blendweights = (blendweights_t *)Mem_Alloc(loadmodel->mempool, meshvertices * sizeof(blendweights_t));

	for (i = 0;i < loadmodel->numskins;i++)
	{
		loadmodel->skinscenes[i].firstframe = i;
		loadmodel->skinscenes[i].framecount = 1;
		loadmodel->skinscenes[i].loop = true;
		loadmodel->skinscenes[i].framerate = 10;
	}

	// load the bone info
	bone = (dpmbone_t *) (pbase + pheader->ofs_bones);
	for (i = 0;i < loadmodel->num_bones;i++)
	{
		memcpy(loadmodel->data_bones[i].name, bone[i].name, sizeof(bone[i].name));
		loadmodel->data_bones[i].flags = BigLong(bone[i].flags);
		loadmodel->data_bones[i].parent = BigLong(bone[i].parent);
		if (loadmodel->data_bones[i].parent >= i)
			Host_Error("%s bone[%i].parent >= %i", loadmodel->name, i, i);
	}

	// load the frames
	frames = (dpmframe_t *) (pbase + pheader->ofs_frames);
	// figure out scale of model from root bone, for compatibility with old dpmodel versions
	poses = (float *) (pbase + BigLong(frames[0].ofs_bonepositions));
	tempvec[0] = BigFloat(poses[0]);
	tempvec[1] = BigFloat(poses[1]);
	tempvec[2] = BigFloat(poses[2]);
	modelscale = VectorLength(tempvec);
	biggestorigin = 0;
	for (i = 0;i < loadmodel->numframes;i++)
	{
		memcpy(loadmodel->animscenes[i].name, frames[i].name, sizeof(frames[i].name));
		loadmodel->animscenes[i].firstframe = i;
		loadmodel->animscenes[i].framecount = 1;
		loadmodel->animscenes[i].loop = true;
		loadmodel->animscenes[i].framerate = 10;
		// load the bone poses for this frame
		poses = (float *) (pbase + BigLong(frames[i].ofs_bonepositions));
		for (j = 0;j < loadmodel->num_bones*12;j++)
		{
			f = fabs(BigFloat(poses[j]));
			biggestorigin = max(biggestorigin, f);
		}
		// stuff not processed here: mins, maxs, yawradius, allradius
	}
	loadmodel->num_posescale = biggestorigin / 32767.0f;
	loadmodel->num_poseinvscale = 1.0f / loadmodel->num_posescale;
	for (i = 0;i < loadmodel->numframes;i++)
	{
		const float *frameposes = (float *) (pbase + BigLong(frames[i].ofs_bonepositions));
		for (j = 0;j < loadmodel->num_bones;j++)
		{
			float pose[12];
			matrix4x4_t posematrix;
			for (k = 0;k < 12;k++)
				pose[k] = BigFloat(frameposes[j*12+k]);
			// scale child bones to match the root scale
			if (loadmodel->data_bones[j].parent >= 0)
			{
				pose[3] *= modelscale;
				pose[7] *= modelscale;
				pose[11] *= modelscale;
			}
			// normalize rotation matrix
			VectorNormalize(pose + 0);
			VectorNormalize(pose + 4);
			VectorNormalize(pose + 8);
			Matrix4x4_FromArray12FloatD3D(&posematrix, pose);
			Matrix4x4_ToBonePose6s(&posematrix, loadmodel->num_poseinvscale, loadmodel->data_poses6s + 6*(i*loadmodel->num_bones+j));
		}
	}

	// load the meshes now
	dpmmesh = (dpmmesh_t *) (pbase + pheader->ofs_meshs);
	meshvertices = 0;
	meshtriangles = 0;
	// reconstruct frame 0 matrices to allow reconstruction of the base mesh
	// (converting from weight-blending skeletal animation to
	//  deformation-based skeletal animation)
	poses = (float *) (pbase + BigLong(frames[0].ofs_bonepositions));
	bonepose = (float *)Z_Malloc(loadmodel->num_bones * sizeof(float[12]));
	for (i = 0;i < loadmodel->num_bones;i++)
	{
		float m[12];
		for (k = 0;k < 12;k++)
			m[k] = BigFloat(poses[i*12+k]);
		if (loadmodel->data_bones[i].parent >= 0)
			R_ConcatTransforms(bonepose + 12 * loadmodel->data_bones[i].parent, m, bonepose + 12 * i);
		else
			for (k = 0;k < 12;k++)
				bonepose[12*i+k] = m[k];
	}
	for (i = 0;i < loadmodel->num_surfaces;i++, dpmmesh++)
	{
		const int *inelements;
		int *outelements;
		const float *intexcoord;
		msurface_t *surface;

		loadmodel->sortedmodelsurfaces[i] = i;
		surface = loadmodel->data_surfaces + i;
		surface->texture = loadmodel->data_textures + i;
		surface->num_firsttriangle = meshtriangles;
		surface->num_triangles = BigLong(dpmmesh->num_tris);
		surface->num_firstvertex = meshvertices;
		surface->num_vertices = BigLong(dpmmesh->num_verts);
		meshvertices += surface->num_vertices;
		meshtriangles += surface->num_triangles;

		inelements = (int *) (pbase + BigLong(dpmmesh->ofs_indices));
		outelements = loadmodel->surfmesh.data_element3i + surface->num_firsttriangle * 3;
		for (j = 0;j < surface->num_triangles;j++)
		{
			// swap element order to flip triangles, because Quake uses clockwise (rare) and dpm uses counterclockwise (standard)
			outelements[0] = surface->num_firstvertex + BigLong(inelements[2]);
			outelements[1] = surface->num_firstvertex + BigLong(inelements[1]);
			outelements[2] = surface->num_firstvertex + BigLong(inelements[0]);
			inelements += 3;
			outelements += 3;
		}

		intexcoord = (float *) (pbase + BigLong(dpmmesh->ofs_texcoords));
		for (j = 0;j < surface->num_vertices*2;j++)
			loadmodel->surfmesh.data_texcoordtexture2f[j + surface->num_firstvertex * 2] = BigFloat(intexcoord[j]);

		data = (unsigned char *) (pbase + BigLong(dpmmesh->ofs_verts));
		for (j = surface->num_firstvertex;j < surface->num_firstvertex + surface->num_vertices;j++)
		{
			int weightindex[4] = { 0, 0, 0, 0 };
			float weightinfluence[4] = { 0, 0, 0, 0 };
			int l;
			int numweights = BigLong(((dpmvertex_t *)data)->numbones);
			data += sizeof(dpmvertex_t);
			for (k = 0;k < numweights;k++)
			{
				const dpmbonevert_t *vert = (dpmbonevert_t *) data;
				int boneindex = BigLong(vert->bonenum);
				const float *m = bonepose + 12 * boneindex;
				float influence = BigFloat(vert->influence);
				float relativeorigin[3], relativenormal[3];
				relativeorigin[0] = BigFloat(vert->origin[0]);
				relativeorigin[1] = BigFloat(vert->origin[1]);
				relativeorigin[2] = BigFloat(vert->origin[2]);
				relativenormal[0] = BigFloat(vert->normal[0]);
				relativenormal[1] = BigFloat(vert->normal[1]);
				relativenormal[2] = BigFloat(vert->normal[2]);
				// blend the vertex bone weights into the base mesh
				loadmodel->surfmesh.data_vertex3f[j*3+0] += relativeorigin[0] * m[0] + relativeorigin[1] * m[1] + relativeorigin[2] * m[ 2] + influence * m[ 3];
				loadmodel->surfmesh.data_vertex3f[j*3+1] += relativeorigin[0] * m[4] + relativeorigin[1] * m[5] + relativeorigin[2] * m[ 6] + influence * m[ 7];
				loadmodel->surfmesh.data_vertex3f[j*3+2] += relativeorigin[0] * m[8] + relativeorigin[1] * m[9] + relativeorigin[2] * m[10] + influence * m[11];
				loadmodel->surfmesh.data_normal3f[j*3+0] += relativenormal[0] * m[0] + relativenormal[1] * m[1] + relativenormal[2] * m[ 2];
				loadmodel->surfmesh.data_normal3f[j*3+1] += relativenormal[0] * m[4] + relativenormal[1] * m[5] + relativenormal[2] * m[ 6];
				loadmodel->surfmesh.data_normal3f[j*3+2] += relativenormal[0] * m[8] + relativenormal[1] * m[9] + relativenormal[2] * m[10];
				if (!k)
				{
					// store the first (and often only) weight
					weightinfluence[0] = influence;
					weightindex[0] = boneindex;
				}
				else
				{
					// sort the new weight into this vertex's weight table
					// (which only accepts up to 4 bones per vertex)
					for (l = 0;l < 4;l++)
					{
						if (weightinfluence[l] < influence)
						{
							// move weaker influence weights out of the way first
							int l2;
							for (l2 = 3;l2 > l;l2--)
							{
								weightinfluence[l2] = weightinfluence[l2-1];
								weightindex[l2] = weightindex[l2-1];
							}
							// store the new weight
							weightinfluence[l] = influence;
							weightindex[l] = boneindex;
							break;
						}
					}
				}
				data += sizeof(dpmbonevert_t);
			}
			loadmodel->surfmesh.blends[j] = Mod_Skeletal_CompressBlend(loadmodel, weightindex, weightinfluence);
		}

		// since dpm models do not have named sections, reuse their shader name as the section name
		Mod_BuildAliasSkinsFromSkinFiles(loadmodel->data_textures + i, skinfiles, dpmmesh->shadername, dpmmesh->shadername);

		Mod_ValidateElements(loadmodel->surfmesh.data_element3i + surface->num_firsttriangle * 3, surface->num_triangles, surface->num_firstvertex, surface->num_vertices, __FILE__, __LINE__);
	}
	if (loadmodel->surfmesh.num_blends < meshvertices)
		loadmodel->surfmesh.data_blendweights = (blendweights_t *)Mem_Realloc(loadmodel->mempool, loadmodel->surfmesh.data_blendweights, loadmodel->surfmesh.num_blends * sizeof(blendweights_t));
	Z_Free(bonepose);
	Mod_FreeSkinFiles(skinfiles);
	Mod_MakeSortedSurfaces(loadmodel);

	// compute all the mesh information that was not loaded from the file
	if (loadmodel->surfmesh.data_element3s)
		for (i = 0;i < loadmodel->surfmesh.num_triangles*3;i++)
			loadmodel->surfmesh.data_element3s[i] = loadmodel->surfmesh.data_element3i[i];
	Mod_BuildBaseBonePoses();
	Mod_BuildTextureVectorsFromNormals(0, loadmodel->surfmesh.num_vertices, loadmodel->surfmesh.num_triangles, loadmodel->surfmesh.data_vertex3f, loadmodel->surfmesh.data_texcoordtexture2f, loadmodel->surfmesh.data_normal3f, loadmodel->surfmesh.data_element3i, loadmodel->surfmesh.data_svector3f, loadmodel->surfmesh.data_tvector3f, r_smoothnormals_areaweighting.integer != 0);
	if (loadmodel->surfmesh.data_neighbor3i)
		Mod_BuildTriangleNeighbors(loadmodel->surfmesh.data_neighbor3i, loadmodel->surfmesh.data_element3i, loadmodel->surfmesh.num_triangles);

	loadmodel->surfmesh.isanimated = loadmodel->numframes > 1 || loadmodel->animscenes[0].framecount > 1;

	if (!loadmodel->surfmesh.isanimated)
	{
		Mod_MakeCollisionBIH(loadmodel, true, &loadmodel->collision_bih);
		loadmodel->TraceBox = Mod_CollisionBIH_TraceBox;
		loadmodel->TraceBrush = Mod_CollisionBIH_TraceBrush;
		loadmodel->TraceLine = Mod_CollisionBIH_TraceLine;
		loadmodel->TracePoint = Mod_CollisionBIH_TracePoint_Mesh;
		loadmodel->PointSuperContents = Mod_CollisionBIH_PointSuperContents_Mesh;
	}
}

// no idea why PSK/PSA files contain weird quaternions but they do...
#define PSKQUATNEGATIONS
void Mod_PSKMODEL_Load(dp_model_t *mod, void *buffer, void *bufferend)
{
	int i, j, index, version, recordsize, numrecords, meshvertices, meshtriangles;
	int numpnts, numvtxw, numfaces, nummatts, numbones, numrawweights, numanimbones, numanims, numanimkeys;
	fs_offset_t filesize;
	pskpnts_t *pnts;
	pskvtxw_t *vtxw;
	pskface_t *faces;
	pskmatt_t *matts;
	pskboneinfo_t *bones;
	pskrawweights_t *rawweights;
	//pskboneinfo_t *animbones;
	pskaniminfo_t *anims;
	pskanimkeys_t *animkeys;
	void *animfilebuffer, *animbuffer, *animbufferend;
	unsigned char *data;
	pskchunk_t *pchunk;
	skinfile_t *skinfiles;
	char animname[MAX_QPATH];
	size_t size;
	float biggestorigin;

	pchunk = (pskchunk_t *)buffer;
	if (strcmp(pchunk->id, "ACTRHEAD"))
		Host_Error ("Mod_PSKMODEL_Load: %s is not an Unreal Engine ActorX (.psk + .psa) model", loadmodel->name);

	loadmodel->modeldatatypestring = "PSK";

	loadmodel->type = mod_alias;
	loadmodel->AnimateVertices = Mod_Skeletal_AnimateVertices;
	loadmodel->DrawSky = NULL;
	loadmodel->DrawAddWaterPlanes = NULL;
	loadmodel->Draw = R_Q1BSP_Draw;
	loadmodel->DrawDepth = R_Q1BSP_DrawDepth;
	loadmodel->DrawDebug = R_Q1BSP_DrawDebug;
	loadmodel->DrawPrepass = R_Q1BSP_DrawPrepass;
	loadmodel->CompileShadowMap = R_Q1BSP_CompileShadowMap;
	loadmodel->DrawShadowMap = R_Q1BSP_DrawShadowMap;
	loadmodel->CompileShadowVolume = R_Q1BSP_CompileShadowVolume;
	loadmodel->DrawShadowVolume = R_Q1BSP_DrawShadowVolume;
	loadmodel->DrawLight = R_Q1BSP_DrawLight;
	loadmodel->TraceBox = Mod_MDLMD2MD3_TraceBox;
	loadmodel->TraceLine = Mod_MDLMD2MD3_TraceLine;
	loadmodel->PointSuperContents = NULL;
	loadmodel->synctype = ST_RAND;

	FS_StripExtension(loadmodel->name, animname, sizeof(animname));
	strlcat(animname, ".psa", sizeof(animname));
	animbuffer = animfilebuffer = FS_LoadFile(animname, loadmodel->mempool, false, &filesize);
	animbufferend = (void *)((unsigned char*)animbuffer + (int)filesize);
	if (animbuffer == NULL)
		Host_Error("%s: can't find .psa file (%s)", loadmodel->name, animname);

	numpnts = 0;
	pnts = NULL;
	numvtxw = 0;
	vtxw = NULL;
	numfaces = 0;
	faces = NULL;
	nummatts = 0;
	matts = NULL;
	numbones = 0;
	bones = NULL;
	numrawweights = 0;
	rawweights = NULL;
	numanims = 0;
	anims = NULL;
	numanimkeys = 0;
	animkeys = NULL;

	while (buffer < bufferend)
	{
		pchunk = (pskchunk_t *)buffer;
		buffer = (void *)((unsigned char *)buffer + sizeof(pskchunk_t));
		version = LittleLong(pchunk->version);
		recordsize = LittleLong(pchunk->recordsize);
		numrecords = LittleLong(pchunk->numrecords);
		if (developer_extra.integer)
			Con_DPrintf("%s: %s %x: %i * %i = %i\n", loadmodel->name, pchunk->id, version, recordsize, numrecords, recordsize * numrecords);
		if (version != 0x1e83b9 && version != 0x1e9179 && version != 0x2e && version != 0x12f2bc && version != 0x12f2f0)
			Con_Printf ("%s: chunk %s has unknown version %x (0x1e83b9, 0x1e9179, 0x2e, 0x12f2bc, 0x12f2f0 are currently supported), trying to load anyway!\n", loadmodel->name, pchunk->id, version);
		if (!strcmp(pchunk->id, "ACTRHEAD"))
		{
			// nothing to do
		}
		else if (!strcmp(pchunk->id, "PNTS0000"))
		{
			pskpnts_t *p;
			if (recordsize != sizeof(*p))
				Host_Error("%s: %s has unsupported recordsize", loadmodel->name, pchunk->id);
			// byteswap in place and keep the pointer
			numpnts = numrecords;
			pnts = (pskpnts_t *)buffer;
			for (index = 0, p = (pskpnts_t *)buffer;index < numrecords;index++, p++)
			{
				p->origin[0] = LittleFloat(p->origin[0]);
				p->origin[1] = LittleFloat(p->origin[1]);
				p->origin[2] = LittleFloat(p->origin[2]);
			}
			buffer = p;
		}
		else if (!strcmp(pchunk->id, "VTXW0000"))
		{
			pskvtxw_t *p;
			if (recordsize != sizeof(*p))
				Host_Error("%s: %s has unsupported recordsize", loadmodel->name, pchunk->id);
			// byteswap in place and keep the pointer
			numvtxw = numrecords;
			vtxw = (pskvtxw_t *)buffer;
			for (index = 0, p = (pskvtxw_t *)buffer;index < numrecords;index++, p++)
			{
				p->pntsindex = LittleShort(p->pntsindex);
				p->texcoord[0] = LittleFloat(p->texcoord[0]);
				p->texcoord[1] = LittleFloat(p->texcoord[1]);
				if (p->pntsindex >= numpnts)
				{
					Con_Printf("%s: vtxw->pntsindex %i >= numpnts %i\n", loadmodel->name, p->pntsindex, numpnts);
					p->pntsindex = 0;
				}
			}
			buffer = p;
		}
		else if (!strcmp(pchunk->id, "FACE0000"))
		{
			pskface_t *p;
			if (recordsize != sizeof(*p))
				Host_Error("%s: %s has unsupported recordsize", loadmodel->name, pchunk->id);
			// byteswap in place and keep the pointer
			numfaces = numrecords;
			faces = (pskface_t *)buffer;
			for (index = 0, p = (pskface_t *)buffer;index < numrecords;index++, p++)
			{
				p->vtxwindex[0] = LittleShort(p->vtxwindex[0]);
				p->vtxwindex[1] = LittleShort(p->vtxwindex[1]);
				p->vtxwindex[2] = LittleShort(p->vtxwindex[2]);
				p->group = LittleLong(p->group);
				if (p->vtxwindex[0] >= numvtxw)
				{
					Con_Printf("%s: face->vtxwindex[0] %i >= numvtxw %i\n", loadmodel->name, p->vtxwindex[0], numvtxw);
					p->vtxwindex[0] = 0;
				}
				if (p->vtxwindex[1] >= numvtxw)
				{
					Con_Printf("%s: face->vtxwindex[1] %i >= numvtxw %i\n", loadmodel->name, p->vtxwindex[1], numvtxw);
					p->vtxwindex[1] = 0;
				}
				if (p->vtxwindex[2] >= numvtxw)
				{
					Con_Printf("%s: face->vtxwindex[2] %i >= numvtxw %i\n", loadmodel->name, p->vtxwindex[2], numvtxw);
					p->vtxwindex[2] = 0;
				}
			}
			buffer = p;
		}
		else if (!strcmp(pchunk->id, "MATT0000"))
		{
			pskmatt_t *p;
			if (recordsize != sizeof(*p))
				Host_Error("%s: %s has unsupported recordsize", loadmodel->name, pchunk->id);
			// byteswap in place and keep the pointer
			nummatts = numrecords;
			matts = (pskmatt_t *)buffer;
			for (index = 0, p = (pskmatt_t *)buffer;index < numrecords;index++, p++)
			{
				// nothing to do
			}
			buffer = p;
		}
		else if (!strcmp(pchunk->id, "REFSKELT"))
		{
			pskboneinfo_t *p;
			if (recordsize != sizeof(*p))
				Host_Error("%s: %s has unsupported recordsize", loadmodel->name, pchunk->id);
			// byteswap in place and keep the pointer
			numbones = numrecords;
			bones = (pskboneinfo_t *)buffer;
			for (index = 0, p = (pskboneinfo_t *)buffer;index < numrecords;index++, p++)
			{
				p->numchildren = LittleLong(p->numchildren);
				p->parent = LittleLong(p->parent);
				p->basepose.quat[0] = LittleFloat(p->basepose.quat[0]);
				p->basepose.quat[1] = LittleFloat(p->basepose.quat[1]);
				p->basepose.quat[2] = LittleFloat(p->basepose.quat[2]);
				p->basepose.quat[3] = LittleFloat(p->basepose.quat[3]);
				p->basepose.origin[0] = LittleFloat(p->basepose.origin[0]);
				p->basepose.origin[1] = LittleFloat(p->basepose.origin[1]);
				p->basepose.origin[2] = LittleFloat(p->basepose.origin[2]);
				p->basepose.unknown = LittleFloat(p->basepose.unknown);
				p->basepose.size[0] = LittleFloat(p->basepose.size[0]);
				p->basepose.size[1] = LittleFloat(p->basepose.size[1]);
				p->basepose.size[2] = LittleFloat(p->basepose.size[2]);
#ifdef PSKQUATNEGATIONS
				if (index)
				{
					p->basepose.quat[0] *= -1;
					p->basepose.quat[1] *= -1;
					p->basepose.quat[2] *= -1;
				}
				else
				{
					p->basepose.quat[0] *=  1;
					p->basepose.quat[1] *= -1;
					p->basepose.quat[2] *=  1;
				}
#endif
				if (p->parent < 0 || p->parent >= numbones)
				{
					Con_Printf("%s: bone->parent %i >= numbones %i\n", loadmodel->name, p->parent, numbones);
					p->parent = 0;
				}
			}
			buffer = p;
		}
		else if (!strcmp(pchunk->id, "RAWWEIGHTS"))
		{
			pskrawweights_t *p;
			if (recordsize != sizeof(*p))
				Host_Error("%s: %s has unsupported recordsize", loadmodel->name, pchunk->id);
			// byteswap in place and keep the pointer
			numrawweights = numrecords;
			rawweights = (pskrawweights_t *)buffer;
			for (index = 0, p = (pskrawweights_t *)buffer;index < numrecords;index++, p++)
			{
				p->weight = LittleFloat(p->weight);
				p->pntsindex = LittleLong(p->pntsindex);
				p->boneindex = LittleLong(p->boneindex);
				if (p->pntsindex < 0 || p->pntsindex >= numpnts)
				{
					Con_Printf("%s: weight->pntsindex %i >= numpnts %i\n", loadmodel->name, p->pntsindex, numpnts);
					p->pntsindex = 0;
				}
				if (p->boneindex < 0 || p->boneindex >= numbones)
				{
					Con_Printf("%s: weight->boneindex %i >= numbones %i\n", loadmodel->name, p->boneindex, numbones);
					p->boneindex = 0;
				}
			}
			buffer = p;
		}
	}

	while (animbuffer < animbufferend)
	{
		pchunk = (pskchunk_t *)animbuffer;
		animbuffer = (void *)((unsigned char *)animbuffer + sizeof(pskchunk_t));
		version = LittleLong(pchunk->version);
		recordsize = LittleLong(pchunk->recordsize);
		numrecords = LittleLong(pchunk->numrecords);
		if (developer_extra.integer)
			Con_DPrintf("%s: %s %x: %i * %i = %i\n", animname, pchunk->id, version, recordsize, numrecords, recordsize * numrecords);
		if (version != 0x1e83b9 && version != 0x1e9179 && version != 0x2e && version != 0x12f2bc && version != 0x12f2f0)
			Con_Printf ("%s: chunk %s has unknown version %x (0x1e83b9, 0x1e9179, 0x2e, 0x12f2bc, 0x12f2f0 are currently supported), trying to load anyway!\n", animname, pchunk->id, version);
		if (!strcmp(pchunk->id, "ANIMHEAD"))
		{
			// nothing to do
		}
		else if (!strcmp(pchunk->id, "BONENAMES"))
		{
			pskboneinfo_t *p;
			if (recordsize != sizeof(*p))
				Host_Error("%s: %s has unsupported recordsize", animname, pchunk->id);
			// byteswap in place and keep the pointer
			numanimbones = numrecords;
			//animbones = (pskboneinfo_t *)animbuffer;
			// NOTE: supposedly psa does not need to match the psk model, the
			// bones missing from the psa would simply use their base
			// positions from the psk, but this is hard for me to implement
			// and people can easily make animations that match.
			if (numanimbones != numbones)
				Host_Error("%s: this loader only supports animations with the same bones as the mesh", loadmodel->name);
			for (index = 0, p = (pskboneinfo_t *)animbuffer;index < numrecords;index++, p++)
			{
				p->numchildren = LittleLong(p->numchildren);
				p->parent = LittleLong(p->parent);
				p->basepose.quat[0] = LittleFloat(p->basepose.quat[0]);
				p->basepose.quat[1] = LittleFloat(p->basepose.quat[1]);
				p->basepose.quat[2] = LittleFloat(p->basepose.quat[2]);
				p->basepose.quat[3] = LittleFloat(p->basepose.quat[3]);
				p->basepose.origin[0] = LittleFloat(p->basepose.origin[0]);
				p->basepose.origin[1] = LittleFloat(p->basepose.origin[1]);
				p->basepose.origin[2] = LittleFloat(p->basepose.origin[2]);
				p->basepose.unknown = LittleFloat(p->basepose.unknown);
				p->basepose.size[0] = LittleFloat(p->basepose.size[0]);
				p->basepose.size[1] = LittleFloat(p->basepose.size[1]);
				p->basepose.size[2] = LittleFloat(p->basepose.size[2]);
#ifdef PSKQUATNEGATIONS
				if (index)
				{
					p->basepose.quat[0] *= -1;
					p->basepose.quat[1] *= -1;
					p->basepose.quat[2] *= -1;
				}
				else
				{
					p->basepose.quat[0] *=  1;
					p->basepose.quat[1] *= -1;
					p->basepose.quat[2] *=  1;
				}
#endif
				if (p->parent < 0 || p->parent >= numanimbones)
				{
					Con_Printf("%s: bone->parent %i >= numanimbones %i\n", animname, p->parent, numanimbones);
					p->parent = 0;
				}
				// check that bones are the same as in the base
				if (strcmp(p->name, bones[index].name) || p->parent != bones[index].parent)
					Host_Error("%s: this loader only supports animations with the same bones as the mesh", animname);
			}
			animbuffer = p;
		}
		else if (!strcmp(pchunk->id, "ANIMINFO"))
		{
			pskaniminfo_t *p;
			if (recordsize != sizeof(*p))
				Host_Error("%s: %s has unsupported recordsize", animname, pchunk->id);
			// byteswap in place and keep the pointer
			numanims = numrecords;
			anims = (pskaniminfo_t *)animbuffer;
			for (index = 0, p = (pskaniminfo_t *)animbuffer;index < numrecords;index++, p++)
			{
				p->numbones = LittleLong(p->numbones);
				p->playtime = LittleFloat(p->playtime);
				p->fps = LittleFloat(p->fps);
				p->firstframe = LittleLong(p->firstframe);
				p->numframes = LittleLong(p->numframes);
				if (p->numbones != numbones)
					Con_Printf("%s: animinfo->numbones != numbones, trying to load anyway!\n", animname);
			}
			animbuffer = p;
		}
		else if (!strcmp(pchunk->id, "ANIMKEYS"))
		{
			pskanimkeys_t *p;
			if (recordsize != sizeof(*p))
				Host_Error("%s: %s has unsupported recordsize", animname, pchunk->id);
			numanimkeys = numrecords;
			animkeys = (pskanimkeys_t *)animbuffer;
			for (index = 0, p = (pskanimkeys_t *)animbuffer;index < numrecords;index++, p++)
			{
				p->origin[0] = LittleFloat(p->origin[0]);
				p->origin[1] = LittleFloat(p->origin[1]);
				p->origin[2] = LittleFloat(p->origin[2]);
				p->quat[0] = LittleFloat(p->quat[0]);
				p->quat[1] = LittleFloat(p->quat[1]);
				p->quat[2] = LittleFloat(p->quat[2]);
				p->quat[3] = LittleFloat(p->quat[3]);
				p->frametime = LittleFloat(p->frametime);
#ifdef PSKQUATNEGATIONS
				if (index % numbones)
				{
					p->quat[0] *= -1;
					p->quat[1] *= -1;
					p->quat[2] *= -1;
				}
				else
				{
					p->quat[0] *=  1;
					p->quat[1] *= -1;
					p->quat[2] *=  1;
				}
#endif
			}
			animbuffer = p;
			// TODO: allocate bonepose stuff
		}
		else
			Con_Printf("%s: unknown chunk ID \"%s\"\n", animname, pchunk->id);
	}

	if (!numpnts || !pnts || !numvtxw || !vtxw || !numfaces || !faces || !nummatts || !matts || !numbones || !bones || !numrawweights || !rawweights || !numanims || !anims || !numanimkeys || !animkeys)
		Host_Error("%s: missing required chunks", loadmodel->name);

	loadmodel->numframes = 0;
	for (index = 0;index < numanims;index++)
		loadmodel->numframes += anims[index].numframes;

	if (numanimkeys != numbones * loadmodel->numframes)
		Host_Error("%s: %s has incorrect number of animation keys", animname, pchunk->id);

	meshvertices = numvtxw;
	meshtriangles = numfaces;

	// load external .skin files if present
	skinfiles = Mod_LoadSkinFiles();
	if (loadmodel->numskins < 1)
		loadmodel->numskins = 1;
	loadmodel->num_bones = numbones;
	loadmodel->num_poses = loadmodel->numframes;
	loadmodel->nummodelsurfaces = loadmodel->num_surfaces = nummatts;
	loadmodel->num_textures = loadmodel->num_surfaces * loadmodel->numskins;
	loadmodel->num_texturesperskin = loadmodel->num_surfaces;
	loadmodel->surfmesh.num_vertices = meshvertices;
	loadmodel->surfmesh.num_triangles = meshtriangles;
	// do most allocations as one merged chunk
	size = loadmodel->num_surfaces * sizeof(msurface_t) + loadmodel->num_surfaces * sizeof(int) + loadmodel->num_surfaces * loadmodel->numskins * sizeof(texture_t) + loadmodel->surfmesh.num_triangles * sizeof(int[3]) + (r_enableshadowvolumes.integer ? loadmodel->surfmesh.num_triangles * sizeof(int[3]) : 0)  + loadmodel->surfmesh.num_vertices * sizeof(float[3]) + loadmodel->surfmesh.num_vertices * sizeof(float[3]) + loadmodel->surfmesh.num_vertices * sizeof(float[3]) + loadmodel->surfmesh.num_vertices * sizeof(float[3]) + loadmodel->surfmesh.num_vertices * sizeof(float[2]) + loadmodel->surfmesh.num_vertices * sizeof(unsigned short) + loadmodel->num_poses * loadmodel->num_bones * sizeof(short[6]) + loadmodel->num_bones * sizeof(float[12]) + loadmodel->numskins * sizeof(animscene_t) + loadmodel->num_bones * sizeof(aliasbone_t) + loadmodel->numframes * sizeof(animscene_t) + ((loadmodel->surfmesh.num_vertices <= 65536) ? (loadmodel->surfmesh.num_triangles * sizeof(unsigned short[3])) : 0);
	data = (unsigned char *)Mem_Alloc(loadmodel->mempool, size);
	loadmodel->data_surfaces = (msurface_t *)data;data += loadmodel->num_surfaces * sizeof(msurface_t);
	loadmodel->sortedmodelsurfaces = (int *)data;data += loadmodel->num_surfaces * sizeof(int);
	loadmodel->data_textures = (texture_t *)data;data += loadmodel->num_surfaces * loadmodel->numskins * sizeof(texture_t);
	loadmodel->surfmesh.data_element3i = (int *)data;data += loadmodel->surfmesh.num_triangles * sizeof(int[3]);
	if (r_enableshadowvolumes.integer)
	{
		loadmodel->surfmesh.data_neighbor3i = (int *)data;data += loadmodel->surfmesh.num_triangles * sizeof(int[3]);
	}
	loadmodel->surfmesh.data_vertex3f = (float *)data;data += loadmodel->surfmesh.num_vertices * sizeof(float[3]);
	loadmodel->surfmesh.data_svector3f = (float *)data;data += loadmodel->surfmesh.num_vertices * sizeof(float[3]);
	loadmodel->surfmesh.data_tvector3f = (float *)data;data += loadmodel->surfmesh.num_vertices * sizeof(float[3]);
	loadmodel->surfmesh.data_normal3f = (float *)data;data += loadmodel->surfmesh.num_vertices * sizeof(float[3]);
	loadmodel->surfmesh.data_texcoordtexture2f = (float *)data;data += loadmodel->surfmesh.num_vertices * sizeof(float[2]);
	loadmodel->data_baseboneposeinverse = (float *)data;data += loadmodel->num_bones * sizeof(float[12]);
	loadmodel->skinscenes = (animscene_t *)data;data += loadmodel->numskins * sizeof(animscene_t);
	loadmodel->data_bones = (aliasbone_t *)data;data += loadmodel->num_bones * sizeof(aliasbone_t);
	loadmodel->animscenes = (animscene_t *)data;data += loadmodel->numframes * sizeof(animscene_t);
	loadmodel->surfmesh.num_blends = 0;
	loadmodel->surfmesh.blends = (unsigned short *)data;data += meshvertices * sizeof(unsigned short);
	if (loadmodel->surfmesh.num_vertices <= 65536)
	{
		loadmodel->surfmesh.data_element3s = (unsigned short *)data;data += loadmodel->surfmesh.num_triangles * sizeof(unsigned short[3]);
	}
	loadmodel->data_poses6s = (short *)data;data += loadmodel->num_poses * loadmodel->num_bones * sizeof(short[6]);
	loadmodel->surfmesh.data_blendweights = (blendweights_t *)Mem_Alloc(loadmodel->mempool, loadmodel->surfmesh.num_vertices * sizeof(blendweights_t));

	for (i = 0;i < loadmodel->numskins;i++)
	{
		loadmodel->skinscenes[i].firstframe = i;
		loadmodel->skinscenes[i].framecount = 1;
		loadmodel->skinscenes[i].loop = true;
		loadmodel->skinscenes[i].framerate = 10;
	}

	// create surfaces
	for (index = 0, i = 0;index < nummatts;index++)
	{
		// since psk models do not have named sections, reuse their shader name as the section name
		Mod_BuildAliasSkinsFromSkinFiles(loadmodel->data_textures + index, skinfiles, matts[index].name, matts[index].name);
		loadmodel->sortedmodelsurfaces[index] = index;
		loadmodel->data_surfaces[index].texture = loadmodel->data_textures + index;
		loadmodel->data_surfaces[index].num_firstvertex = 0;
		loadmodel->data_surfaces[index].num_vertices = loadmodel->surfmesh.num_vertices;
	}

	// copy over the vertex locations and texcoords
	for (index = 0;index < numvtxw;index++)
	{
		loadmodel->surfmesh.data_vertex3f[index*3+0] = pnts[vtxw[index].pntsindex].origin[0];
		loadmodel->surfmesh.data_vertex3f[index*3+1] = pnts[vtxw[index].pntsindex].origin[1];
		loadmodel->surfmesh.data_vertex3f[index*3+2] = pnts[vtxw[index].pntsindex].origin[2];
		loadmodel->surfmesh.data_texcoordtexture2f[index*2+0] = vtxw[index].texcoord[0];
		loadmodel->surfmesh.data_texcoordtexture2f[index*2+1] = vtxw[index].texcoord[1];
	}

	// loading the faces is complicated because we need to sort them into surfaces by mattindex
	for (index = 0;index < numfaces;index++)
		loadmodel->data_surfaces[faces[index].mattindex].num_triangles++;
	for (index = 0, i = 0;index < nummatts;index++)
	{
		loadmodel->data_surfaces[index].num_firsttriangle = i;
		i += loadmodel->data_surfaces[index].num_triangles;
		loadmodel->data_surfaces[index].num_triangles = 0;
	}
	for (index = 0;index < numfaces;index++)
	{
		i = (loadmodel->data_surfaces[faces[index].mattindex].num_firsttriangle + loadmodel->data_surfaces[faces[index].mattindex].num_triangles++)*3;
		loadmodel->surfmesh.data_element3i[i+0] = faces[index].vtxwindex[0];
		loadmodel->surfmesh.data_element3i[i+1] = faces[index].vtxwindex[1];
		loadmodel->surfmesh.data_element3i[i+2] = faces[index].vtxwindex[2];
	}

	// copy over the bones
	for (index = 0;index < numbones;index++)
	{
		strlcpy(loadmodel->data_bones[index].name, bones[index].name, sizeof(loadmodel->data_bones[index].name));
		loadmodel->data_bones[index].parent = (index || bones[index].parent > 0) ? bones[index].parent : -1;
		if (loadmodel->data_bones[index].parent >= index)
			Host_Error("%s bone[%i].parent >= %i", loadmodel->name, index, index);
	}

	// sort the psk point weights into the vertex weight tables
	// (which only accept up to 4 bones per vertex)
	for (index = 0;index < numvtxw;index++)
	{
		int weightindex[4] = { 0, 0, 0, 0 };
		float weightinfluence[4] = { 0, 0, 0, 0 };
		int l;
		for (j = 0;j < numrawweights;j++)
		{
			if (rawweights[j].pntsindex == vtxw[index].pntsindex)
			{
				int boneindex = rawweights[j].boneindex;
				float influence = rawweights[j].weight;
				for (l = 0;l < 4;l++)
				{
					if (weightinfluence[l] < influence)
					{
						// move lower influence weights out of the way first
						int l2;
						for (l2 = 3;l2 > l;l2--)
						{
							weightinfluence[l2] = weightinfluence[l2-1];
							weightindex[l2] = weightindex[l2-1];
						}
						// store the new weight
						weightinfluence[l] = influence;
						weightindex[l] = boneindex;
						break;
					}
				}
			}
		}
		loadmodel->surfmesh.blends[index] = Mod_Skeletal_CompressBlend(loadmodel, weightindex, weightinfluence);
	}
	if (loadmodel->surfmesh.num_blends < loadmodel->surfmesh.num_vertices)
		loadmodel->surfmesh.data_blendweights = (blendweights_t *)Mem_Realloc(loadmodel->mempool, loadmodel->surfmesh.data_blendweights, loadmodel->surfmesh.num_blends * sizeof(blendweights_t));

	// set up the animscenes based on the anims
	for (index = 0, i = 0;index < numanims;index++)
	{
		for (j = 0;j < anims[index].numframes;j++, i++)
		{
			dpsnprintf(loadmodel->animscenes[i].name, sizeof(loadmodel->animscenes[i].name), "%s_%d", anims[index].name, j);
			loadmodel->animscenes[i].firstframe = i;
			loadmodel->animscenes[i].framecount = 1;
			loadmodel->animscenes[i].loop = true;
			loadmodel->animscenes[i].framerate = anims[index].fps;
		}
	}

	// calculate the scaling value for bone origins so they can be compressed to short
	biggestorigin = 0;
	for (index = 0;index < numanimkeys;index++)
	{
		pskanimkeys_t *k = animkeys + index;
		biggestorigin = max(biggestorigin, fabs(k->origin[0]));
		biggestorigin = max(biggestorigin, fabs(k->origin[1]));
		biggestorigin = max(biggestorigin, fabs(k->origin[2]));
	}
	loadmodel->num_posescale = biggestorigin / 32767.0f;
	loadmodel->num_poseinvscale = 1.0f / loadmodel->num_posescale;

	// load the poses from the animkeys
	for (index = 0;index < numanimkeys;index++)
	{
		pskanimkeys_t *k = animkeys + index;
		float quat[4];
		Vector4Copy(k->quat, quat);
		if (quat[3] > 0)
			Vector4Negate(quat, quat);
		Vector4Normalize2(quat, quat);
		// compress poses to the short[6] format for longterm storage
		loadmodel->data_poses6s[index*6+0] = k->origin[0] * loadmodel->num_poseinvscale;
		loadmodel->data_poses6s[index*6+1] = k->origin[1] * loadmodel->num_poseinvscale;
		loadmodel->data_poses6s[index*6+2] = k->origin[2] * loadmodel->num_poseinvscale;
		loadmodel->data_poses6s[index*6+3] = quat[0] * 32767.0f;
		loadmodel->data_poses6s[index*6+4] = quat[1] * 32767.0f;
		loadmodel->data_poses6s[index*6+5] = quat[2] * 32767.0f;
	}
	Mod_FreeSkinFiles(skinfiles);
	Mem_Free(animfilebuffer);
	Mod_MakeSortedSurfaces(loadmodel);

	// compute all the mesh information that was not loaded from the file
	// TODO: honor smoothing groups somehow?
	if (loadmodel->surfmesh.data_element3s)
		for (i = 0;i < loadmodel->surfmesh.num_triangles*3;i++)
			loadmodel->surfmesh.data_element3s[i] = loadmodel->surfmesh.data_element3i[i];
	Mod_ValidateElements(loadmodel->surfmesh.data_element3i, loadmodel->surfmesh.num_triangles, 0, loadmodel->surfmesh.num_vertices, __FILE__, __LINE__);
	Mod_BuildBaseBonePoses();
	Mod_BuildNormals(0, loadmodel->surfmesh.num_vertices, loadmodel->surfmesh.num_triangles, loadmodel->surfmesh.data_vertex3f, loadmodel->surfmesh.data_element3i, loadmodel->surfmesh.data_normal3f, r_smoothnormals_areaweighting.integer != 0);
	Mod_BuildTextureVectorsFromNormals(0, loadmodel->surfmesh.num_vertices, loadmodel->surfmesh.num_triangles, loadmodel->surfmesh.data_vertex3f, loadmodel->surfmesh.data_texcoordtexture2f, loadmodel->surfmesh.data_normal3f, loadmodel->surfmesh.data_element3i, loadmodel->surfmesh.data_svector3f, loadmodel->surfmesh.data_tvector3f, r_smoothnormals_areaweighting.integer != 0);
	if (loadmodel->surfmesh.data_neighbor3i)
		Mod_BuildTriangleNeighbors(loadmodel->surfmesh.data_neighbor3i, loadmodel->surfmesh.data_element3i, loadmodel->surfmesh.num_triangles);
	Mod_Alias_CalculateBoundingBox();

	loadmodel->surfmesh.isanimated = loadmodel->numframes > 1 || loadmodel->animscenes[0].framecount > 1;

	if (!loadmodel->surfmesh.isanimated)
	{
		Mod_MakeCollisionBIH(loadmodel, true, &loadmodel->collision_bih);
		loadmodel->TraceBox = Mod_CollisionBIH_TraceBox;
		loadmodel->TraceBrush = Mod_CollisionBIH_TraceBrush;
		loadmodel->TraceLine = Mod_CollisionBIH_TraceLine;
		loadmodel->TracePoint = Mod_CollisionBIH_TracePoint_Mesh;
		loadmodel->PointSuperContents = Mod_CollisionBIH_PointSuperContents_Mesh;
	}
}

void Mod_INTERQUAKEMODEL_Load(dp_model_t *mod, void *buffer, void *bufferend)
{
	unsigned char *data;
	const char *text;
	unsigned char *pbase, *pend;
	iqmheader_t *header;
	skinfile_t *skinfiles;
	int i, j, k, meshvertices, meshtriangles;
	float *vposition = NULL, *vtexcoord = NULL, *vnormal = NULL, *vtangent = NULL;
	unsigned char *vblendindexes = NULL, *vblendweights = NULL;
	iqmjoint1_t *joint1 = NULL;
	iqmjoint_t *joint = NULL;
	iqmpose1_t *pose1 = NULL;
	iqmpose_t *pose = NULL;
	iqmanim_t *anim;
	iqmmesh_t *mesh;
	iqmbounds_t *bounds;
	iqmvertexarray_t *va;
	unsigned short *framedata;
	float biggestorigin;
	const int *inelements;
	int *outelements;
	float *outvertex, *outnormal, *outtexcoord, *outsvector, *outtvector;

	pbase = (unsigned char *)buffer;
	pend = (unsigned char *)bufferend;
	header = (iqmheader_t *)buffer;
	if (memcmp(header->id, "INTERQUAKEMODEL", 16))
		Host_Error ("Mod_INTERQUAKEMODEL_Load: %s is not an Inter-Quake Model", loadmodel->name);
	if (LittleLong(header->version) != 1 && LittleLong(header->version) != 2)
		Host_Error ("Mod_INTERQUAKEMODEL_Load: only version 1 and 2 models are currently supported (name = %s)", loadmodel->name);

	loadmodel->modeldatatypestring = "IQM";

	loadmodel->type = mod_alias;
	loadmodel->synctype = ST_RAND;

	// byteswap header
	header->version = LittleLong(header->version);
	header->filesize = LittleLong(header->filesize);
	header->flags = LittleLong(header->flags);
	header->num_text = LittleLong(header->num_text);
	header->ofs_text = LittleLong(header->ofs_text);
	header->num_meshes = LittleLong(header->num_meshes);
	header->ofs_meshes = LittleLong(header->ofs_meshes);
	header->num_vertexarrays = LittleLong(header->num_vertexarrays);
	header->num_vertexes = LittleLong(header->num_vertexes);
	header->ofs_vertexarrays = LittleLong(header->ofs_vertexarrays);
	header->num_triangles = LittleLong(header->num_triangles);
	header->ofs_triangles = LittleLong(header->ofs_triangles);
	header->ofs_neighbors = LittleLong(header->ofs_neighbors);
	header->num_joints = LittleLong(header->num_joints);
	header->ofs_joints = LittleLong(header->ofs_joints);
	header->num_poses = LittleLong(header->num_poses);
	header->ofs_poses = LittleLong(header->ofs_poses);
	header->num_anims = LittleLong(header->num_anims);
	header->ofs_anims = LittleLong(header->ofs_anims);
	header->num_frames = LittleLong(header->num_frames);
	header->num_framechannels = LittleLong(header->num_framechannels);
	header->ofs_frames = LittleLong(header->ofs_frames);
	header->ofs_bounds = LittleLong(header->ofs_bounds);
	header->num_comment = LittleLong(header->num_comment);
	header->ofs_comment = LittleLong(header->ofs_comment);
	header->num_extensions = LittleLong(header->num_extensions);
	header->ofs_extensions = LittleLong(header->ofs_extensions);

	if (header->num_triangles < 1 || header->num_vertexes < 3 || header->num_vertexarrays < 1 || header->num_meshes < 1)
	{
		Con_Printf("%s has no geometry\n", loadmodel->name);
		return;
	}
	if (header->num_frames < 1 || header->num_anims < 1)
	{
		Con_Printf("%s has no animations\n", loadmodel->name);
		return;
	}

	if (pbase + header->ofs_text + header->num_text > pend ||
		pbase + header->ofs_meshes + header->num_meshes*sizeof(iqmmesh_t) > pend ||
		pbase + header->ofs_vertexarrays + header->num_vertexarrays*sizeof(iqmvertexarray_t) > pend ||
		pbase + header->ofs_triangles + header->num_triangles*sizeof(int[3]) > pend ||
		(header->ofs_neighbors && pbase + header->ofs_neighbors + header->num_triangles*sizeof(int[3]) > pend) ||
		pbase + header->ofs_joints + header->num_joints*sizeof(iqmjoint_t) > pend ||
		pbase + header->ofs_poses + header->num_poses*sizeof(iqmpose_t) > pend ||
		pbase + header->ofs_anims + header->num_anims*sizeof(iqmanim_t) > pend ||
		pbase + header->ofs_frames + header->num_frames*header->num_framechannels*sizeof(unsigned short) > pend ||
		(header->ofs_bounds && pbase + header->ofs_bounds + header->num_frames*sizeof(iqmbounds_t) > pend) ||
		pbase + header->ofs_comment + header->num_comment > pend)
	{
		Con_Printf("%s has invalid size or offset information\n", loadmodel->name);
		return;
	}

	va = (iqmvertexarray_t *)(pbase + header->ofs_vertexarrays);
	for (i = 0;i < (int)header->num_vertexarrays;i++)
	{
		size_t vsize;
		va[i].type = LittleLong(va[i].type);
		va[i].flags = LittleLong(va[i].flags);
		va[i].format = LittleLong(va[i].format);
		va[i].size = LittleLong(va[i].size);
		va[i].offset = LittleLong(va[i].offset);
		vsize = header->num_vertexes*va[i].size;
		switch (va[i].format)
		{ 
		case IQM_FLOAT: vsize *= sizeof(float); break;
		case IQM_UBYTE: vsize *= sizeof(unsigned char); break;
		default: continue;
		}
		if (pbase + va[i].offset + vsize > pend)
		  continue;
		switch (va[i].type)
		{
		case IQM_POSITION:
			if (va[i].format == IQM_FLOAT && va[i].size == 3)
				vposition = (float *)(pbase + va[i].offset);
			break;
		case IQM_TEXCOORD:
			if (va[i].format == IQM_FLOAT && va[i].size == 2)
				vtexcoord = (float *)(pbase + va[i].offset);
			break;
		case IQM_NORMAL:
			if (va[i].format == IQM_FLOAT && va[i].size == 3)
				vnormal = (float *)(pbase + va[i].offset);
			break;
		case IQM_TANGENT:
			if (va[i].format == IQM_FLOAT && va[i].size == 4)
				vtangent = (float *)(pbase + va[i].offset);
			break;
		case IQM_BLENDINDEXES:
			if (va[i].format == IQM_UBYTE && va[i].size == 4)
				vblendindexes = (unsigned char *)(pbase + va[i].offset);
			break;
		case IQM_BLENDWEIGHTS:
			if (va[i].format == IQM_UBYTE && va[i].size == 4)
				vblendweights = (unsigned char *)(pbase + va[i].offset);
			break;
		}
	}
	if (!vposition || !vtexcoord || !vblendindexes || !vblendweights)
	{
		Con_Printf("%s is missing vertex array data\n", loadmodel->name);
		return;
	}

	text = header->num_text && header->ofs_text ? (const char *)(pbase + header->ofs_text) : "";

	loadmodel->AnimateVertices = Mod_Skeletal_AnimateVertices;
	loadmodel->DrawSky = NULL;
	loadmodel->DrawAddWaterPlanes = NULL;
	loadmodel->Draw = R_Q1BSP_Draw;
	loadmodel->DrawDepth = R_Q1BSP_DrawDepth;
	loadmodel->DrawDebug = R_Q1BSP_DrawDebug;
	loadmodel->DrawPrepass = R_Q1BSP_DrawPrepass;
	loadmodel->CompileShadowMap = R_Q1BSP_CompileShadowMap;
	loadmodel->DrawShadowMap = R_Q1BSP_DrawShadowMap;
	loadmodel->CompileShadowVolume = R_Q1BSP_CompileShadowVolume;
	loadmodel->DrawShadowVolume = R_Q1BSP_DrawShadowVolume;
	loadmodel->DrawLight = R_Q1BSP_DrawLight;
	loadmodel->TraceBox = Mod_MDLMD2MD3_TraceBox;
	loadmodel->TraceLine = Mod_MDLMD2MD3_TraceLine;
	loadmodel->PointSuperContents = NULL;

	// load external .skin files if present
	skinfiles = Mod_LoadSkinFiles();
	if (loadmodel->numskins < 1)
		loadmodel->numskins = 1;

	loadmodel->numframes = header->num_anims;
	loadmodel->num_bones = header->num_joints;
	loadmodel->num_poses = header->num_frames;
	loadmodel->nummodelsurfaces = loadmodel->num_surfaces = header->num_meshes;
	loadmodel->num_textures = loadmodel->num_surfaces * loadmodel->numskins;
	loadmodel->num_texturesperskin = loadmodel->num_surfaces;

	meshvertices = header->num_vertexes;
	meshtriangles = header->num_triangles;

	// do most allocations as one merged chunk
	data = (unsigned char *)Mem_Alloc(loadmodel->mempool, loadmodel->num_surfaces * sizeof(msurface_t) + loadmodel->num_surfaces * sizeof(int) + loadmodel->num_surfaces * loadmodel->numskins * sizeof(texture_t) + meshtriangles * sizeof(int[3]) + (meshvertices <= 65536 ? meshtriangles * sizeof(unsigned short[3]) : 0) + (r_enableshadowvolumes.integer ? meshtriangles * sizeof(int[3]) : 0) + meshvertices * (sizeof(float[14]) + sizeof(unsigned short)) + loadmodel->num_poses * loadmodel->num_bones * sizeof(short[6]) + loadmodel->num_bones * sizeof(float[12]) + loadmodel->numskins * sizeof(animscene_t) + loadmodel->num_bones * sizeof(aliasbone_t) + loadmodel->numframes * sizeof(animscene_t));
	loadmodel->data_surfaces = (msurface_t *)data;data += loadmodel->num_surfaces * sizeof(msurface_t);
	loadmodel->sortedmodelsurfaces = (int *)data;data += loadmodel->num_surfaces * sizeof(int);
	loadmodel->data_textures = (texture_t *)data;data += loadmodel->num_surfaces * loadmodel->numskins * sizeof(texture_t);
	loadmodel->surfmesh.num_vertices = meshvertices;
	loadmodel->surfmesh.num_triangles = meshtriangles;
	loadmodel->surfmesh.data_element3i = (int *)data;data += meshtriangles * sizeof(int[3]);
	if (r_enableshadowvolumes.integer)
	{
		loadmodel->surfmesh.data_neighbor3i = (int *)data;data += meshtriangles * sizeof(int[3]);
	}
	loadmodel->surfmesh.data_vertex3f = (float *)data;data += meshvertices * sizeof(float[3]);
	loadmodel->surfmesh.data_svector3f = (float *)data;data += meshvertices * sizeof(float[3]);
	loadmodel->surfmesh.data_tvector3f = (float *)data;data += meshvertices * sizeof(float[3]);
	loadmodel->surfmesh.data_normal3f = (float *)data;data += meshvertices * sizeof(float[3]);
	loadmodel->surfmesh.data_texcoordtexture2f = (float *)data;data += meshvertices * sizeof(float[2]);
	loadmodel->data_baseboneposeinverse = (float *)data;data += loadmodel->num_bones * sizeof(float[12]);
	loadmodel->skinscenes = (animscene_t *)data;data += loadmodel->numskins * sizeof(animscene_t);
	loadmodel->data_bones = (aliasbone_t *)data;data += loadmodel->num_bones * sizeof(aliasbone_t);
	loadmodel->animscenes = (animscene_t *)data;data += loadmodel->numframes * sizeof(animscene_t);
	loadmodel->surfmesh.num_blends = 0;
	loadmodel->surfmesh.blends = (unsigned short *)data;data += meshvertices * sizeof(unsigned short);
	if (meshvertices <= 65536)
	{
		loadmodel->surfmesh.data_element3s = (unsigned short *)data;data += meshtriangles * sizeof(unsigned short[3]);
	}
	loadmodel->data_poses6s = (short *)data;data += loadmodel->num_poses * loadmodel->num_bones * sizeof(short[6]);
	loadmodel->surfmesh.data_blendweights = (blendweights_t *)Mem_Alloc(loadmodel->mempool, meshvertices * sizeof(blendweights_t));

	for (i = 0;i < loadmodel->numskins;i++)
	{
		loadmodel->skinscenes[i].firstframe = i;
		loadmodel->skinscenes[i].framecount = 1;
		loadmodel->skinscenes[i].loop = true;
		loadmodel->skinscenes[i].framerate = 10;
	}

	// load the bone info
	if (header->version == 1)
	{
		joint1 = (iqmjoint1_t *) (pbase + header->ofs_joints);
		for (i = 0;i < loadmodel->num_bones;i++)
		{
			matrix4x4_t relbase, relinvbase, pinvbase, invbase;
			joint1[i].name = LittleLong(joint1[i].name);
			joint1[i].parent = LittleLong(joint1[i].parent);
			for (j = 0;j < 3;j++)
			{
				joint1[i].origin[j] = LittleFloat(joint1[i].origin[j]);
				joint1[i].rotation[j] = LittleFloat(joint1[i].rotation[j]);
				joint1[i].scale[j] = LittleFloat(joint1[i].scale[j]);
			}
			strlcpy(loadmodel->data_bones[i].name, &text[joint1[i].name], sizeof(loadmodel->data_bones[i].name));
			loadmodel->data_bones[i].parent = joint1[i].parent;
			if (loadmodel->data_bones[i].parent >= i)
				Host_Error("%s bone[%i].parent >= %i", loadmodel->name, i, i);
			Matrix4x4_FromDoom3Joint(&relbase, joint1[i].origin[0], joint1[i].origin[1], joint1[i].origin[2], joint1[i].rotation[0], joint1[i].rotation[1], joint1[i].rotation[2]);
			Matrix4x4_Invert_Simple(&relinvbase, &relbase);
			if (loadmodel->data_bones[i].parent >= 0)
			{
				Matrix4x4_FromArray12FloatD3D(&pinvbase, loadmodel->data_baseboneposeinverse + 12*loadmodel->data_bones[i].parent);
				Matrix4x4_Concat(&invbase, &relinvbase, &pinvbase);
				Matrix4x4_ToArray12FloatD3D(&invbase, loadmodel->data_baseboneposeinverse + 12*i);
			}
			else Matrix4x4_ToArray12FloatD3D(&relinvbase, loadmodel->data_baseboneposeinverse + 12*i);
		}
	}
	else
	{
		joint = (iqmjoint_t *) (pbase + header->ofs_joints);
		for (i = 0;i < loadmodel->num_bones;i++)
		{
			matrix4x4_t relbase, relinvbase, pinvbase, invbase;
			joint[i].name = LittleLong(joint[i].name);
			joint[i].parent = LittleLong(joint[i].parent);
			for (j = 0;j < 3;j++)
			{
				joint[i].origin[j] = LittleFloat(joint[i].origin[j]);
				joint[i].rotation[j] = LittleFloat(joint[i].rotation[j]);
				joint[i].scale[j] = LittleFloat(joint[i].scale[j]);
			}
			joint[i].rotation[3] = LittleFloat(joint[i].rotation[3]);
			strlcpy(loadmodel->data_bones[i].name, &text[joint[i].name], sizeof(loadmodel->data_bones[i].name));
			loadmodel->data_bones[i].parent = joint[i].parent;
			if (loadmodel->data_bones[i].parent >= i)
				Host_Error("%s bone[%i].parent >= %i", loadmodel->name, i, i);
            if (joint[i].rotation[3] > 0)
                Vector4Negate(joint[i].rotation, joint[i].rotation);
            Vector4Normalize2(joint[i].rotation, joint[i].rotation);
			Matrix4x4_FromDoom3Joint(&relbase, joint[i].origin[0], joint[i].origin[1], joint[i].origin[2], joint[i].rotation[0], joint[i].rotation[1], joint[i].rotation[2]);
			Matrix4x4_Invert_Simple(&relinvbase, &relbase);
			if (loadmodel->data_bones[i].parent >= 0)
			{
				Matrix4x4_FromArray12FloatD3D(&pinvbase, loadmodel->data_baseboneposeinverse + 12*loadmodel->data_bones[i].parent);
				Matrix4x4_Concat(&invbase, &relinvbase, &pinvbase);
				Matrix4x4_ToArray12FloatD3D(&invbase, loadmodel->data_baseboneposeinverse + 12*i);
			}	
			else Matrix4x4_ToArray12FloatD3D(&relinvbase, loadmodel->data_baseboneposeinverse + 12*i);
		}
	}

	// set up the animscenes based on the anims
	anim = (iqmanim_t *) (pbase + header->ofs_anims);
	for (i = 0;i < (int)header->num_anims;i++)
	{
		anim[i].name = LittleLong(anim[i].name);
		anim[i].first_frame = LittleLong(anim[i].first_frame);
		anim[i].num_frames = LittleLong(anim[i].num_frames);
		anim[i].framerate = LittleFloat(anim[i].framerate);
		anim[i].flags = LittleLong(anim[i].flags);
		strlcpy(loadmodel->animscenes[i].name, &text[anim[i].name], sizeof(loadmodel->animscenes[i].name));
		loadmodel->animscenes[i].firstframe = anim[i].first_frame;
		loadmodel->animscenes[i].framecount = anim[i].num_frames;
		loadmodel->animscenes[i].loop = ((anim[i].flags & IQM_LOOP) != 0);
		loadmodel->animscenes[i].framerate = anim[i].framerate;
	}
	
	biggestorigin = 0;
	if (header->version == 1)
	{
		pose1 = (iqmpose1_t *) (pbase + header->ofs_poses);
		for (i = 0;i < (int)header->num_poses;i++)
		{
			float f;
			pose1[i].parent = LittleLong(pose1[i].parent);
			pose1[i].channelmask = LittleLong(pose1[i].channelmask);
			for (j = 0;j < 9;j++)
			{
				pose1[i].channeloffset[j] = LittleFloat(pose1[i].channeloffset[j]);
				pose1[i].channelscale[j] = LittleFloat(pose1[i].channelscale[j]);
			}
			f = fabs(pose1[i].channeloffset[0]); biggestorigin = max(biggestorigin, f);
			f = fabs(pose1[i].channeloffset[1]); biggestorigin = max(biggestorigin, f);
			f = fabs(pose1[i].channeloffset[2]); biggestorigin = max(biggestorigin, f);
			f = fabs(pose1[i].channeloffset[0] + 0xFFFF*pose1[i].channelscale[0]); biggestorigin = max(biggestorigin, f);
			f = fabs(pose1[i].channeloffset[1] + 0xFFFF*pose1[i].channelscale[1]); biggestorigin = max(biggestorigin, f);
			f = fabs(pose1[i].channeloffset[2] + 0xFFFF*pose1[i].channelscale[2]); biggestorigin = max(biggestorigin, f);
		}
	}
	else
	{
		pose = (iqmpose_t *) (pbase + header->ofs_poses);
		for (i = 0;i < (int)header->num_poses;i++)
		{
			float f;
			pose[i].parent = LittleLong(pose[i].parent);
			pose[i].channelmask = LittleLong(pose[i].channelmask);
			for (j = 0;j < 10;j++)
			{
				pose[i].channeloffset[j] = LittleFloat(pose[i].channeloffset[j]);
				pose[i].channelscale[j] = LittleFloat(pose[i].channelscale[j]);
			}
			f = fabs(pose[i].channeloffset[0]); biggestorigin = max(biggestorigin, f);
			f = fabs(pose[i].channeloffset[1]); biggestorigin = max(biggestorigin, f);
			f = fabs(pose[i].channeloffset[2]); biggestorigin = max(biggestorigin, f);
			f = fabs(pose[i].channeloffset[0] + 0xFFFF*pose[i].channelscale[0]); biggestorigin = max(biggestorigin, f);
			f = fabs(pose[i].channeloffset[1] + 0xFFFF*pose[i].channelscale[1]); biggestorigin = max(biggestorigin, f);
			f = fabs(pose[i].channeloffset[2] + 0xFFFF*pose[i].channelscale[2]); biggestorigin = max(biggestorigin, f);
		}
	}
	loadmodel->num_posescale = biggestorigin / 32767.0f;
	loadmodel->num_poseinvscale = 1.0f / loadmodel->num_posescale;

	// load the pose data
	framedata = (unsigned short *) (pbase + header->ofs_frames);
	if (header->version == 1)
	{
		for (i = 0, k = 0;i < (int)header->num_frames;i++)
		{
			for (j = 0;j < (int)header->num_poses;j++, k++)
			{
				loadmodel->data_poses6s[k*6 + 0] = loadmodel->num_poseinvscale * (pose1[j].channeloffset[0] + (pose1[j].channelmask&1 ? (unsigned short)LittleShort(*framedata++) * pose1[j].channelscale[0] : 0));
				loadmodel->data_poses6s[k*6 + 1] = loadmodel->num_poseinvscale * (pose1[j].channeloffset[1] + (pose1[j].channelmask&2 ? (unsigned short)LittleShort(*framedata++) * pose1[j].channelscale[1] : 0));
				loadmodel->data_poses6s[k*6 + 2] = loadmodel->num_poseinvscale * (pose1[j].channeloffset[2] + (pose1[j].channelmask&4 ? (unsigned short)LittleShort(*framedata++) * pose1[j].channelscale[2] : 0));
				loadmodel->data_poses6s[k*6 + 3] = 32767.0f * (pose1[j].channeloffset[3] + (pose1[j].channelmask&8 ? (unsigned short)LittleShort(*framedata++) * pose1[j].channelscale[3] : 0));
				loadmodel->data_poses6s[k*6 + 4] = 32767.0f * (pose1[j].channeloffset[4] + (pose1[j].channelmask&16 ? (unsigned short)LittleShort(*framedata++) * pose1[j].channelscale[4] : 0));
				loadmodel->data_poses6s[k*6 + 5] = 32767.0f * (pose1[j].channeloffset[5] + (pose1[j].channelmask&32 ? (unsigned short)LittleShort(*framedata++) * pose1[j].channelscale[5] : 0));
				// skip scale data for now
				if(pose1[j].channelmask&64) framedata++;
				if(pose1[j].channelmask&128) framedata++;
				if(pose1[j].channelmask&256) framedata++;
			}
		}
	}
	else
	{
		for (i = 0, k = 0;i < (int)header->num_frames;i++)	
		{
			for (j = 0;j < (int)header->num_poses;j++, k++)
			{
				float rot[4];
				loadmodel->data_poses6s[k*6 + 0] = loadmodel->num_poseinvscale * (pose[j].channeloffset[0] + (pose[j].channelmask&1 ? (unsigned short)LittleShort(*framedata++) * pose[j].channelscale[0] : 0));
				loadmodel->data_poses6s[k*6 + 1] = loadmodel->num_poseinvscale * (pose[j].channeloffset[1] + (pose[j].channelmask&2 ? (unsigned short)LittleShort(*framedata++) * pose[j].channelscale[1] : 0));
				loadmodel->data_poses6s[k*6 + 2] = loadmodel->num_poseinvscale * (pose[j].channeloffset[2] + (pose[j].channelmask&4 ? (unsigned short)LittleShort(*framedata++) * pose[j].channelscale[2] : 0));
				rot[0] = pose[j].channeloffset[3] + (pose[j].channelmask&8 ? (unsigned short)LittleShort(*framedata++) * pose[j].channelscale[3] : 0);
				rot[1] = pose[j].channeloffset[4] + (pose[j].channelmask&16 ? (unsigned short)LittleShort(*framedata++) * pose[j].channelscale[4] : 0);
				rot[2] = pose[j].channeloffset[5] + (pose[j].channelmask&32 ? (unsigned short)LittleShort(*framedata++) * pose[j].channelscale[5] : 0);
				rot[3] = pose[j].channeloffset[6] + (pose[j].channelmask&64 ? (unsigned short)LittleShort(*framedata++) * pose[j].channelscale[6] : 0);
				if (rot[3] > 0)
					Vector4Negate(rot, rot);
				Vector4Normalize2(rot, rot);
				loadmodel->data_poses6s[k*6 + 3] = 32767.0f * rot[0];
				loadmodel->data_poses6s[k*6 + 4] = 32767.0f * rot[1];
				loadmodel->data_poses6s[k*6 + 5] = 32767.0f * rot[2];
				// skip scale data for now
				if(pose[j].channelmask&128) framedata++;
				if(pose[j].channelmask&256) framedata++;
				if(pose[j].channelmask&512) framedata++;
			}
		}
	}

	// load bounding box data
	if (header->ofs_bounds)
	{
		float xyradius = 0, radius = 0;
		bounds = (iqmbounds_t *) (pbase + header->ofs_bounds);
		VectorClear(loadmodel->normalmins);
		VectorClear(loadmodel->normalmaxs);
		for (i = 0; i < (int)header->num_frames;i++)
		{
			bounds[i].mins[0] = LittleFloat(bounds[i].mins[0]);
			bounds[i].mins[1] = LittleFloat(bounds[i].mins[1]);
			bounds[i].mins[2] = LittleFloat(bounds[i].mins[2]);
			bounds[i].maxs[0] = LittleFloat(bounds[i].maxs[0]);			
			bounds[i].maxs[1] = LittleFloat(bounds[i].maxs[1]);	
			bounds[i].maxs[2] = LittleFloat(bounds[i].maxs[2]);	
			bounds[i].xyradius = LittleFloat(bounds[i].xyradius);
			bounds[i].radius = LittleFloat(bounds[i].radius);
			if (!i)
			{
				VectorCopy(bounds[i].mins, loadmodel->normalmins);
				VectorCopy(bounds[i].maxs, loadmodel->normalmaxs);
			}
			else
			{
				if (loadmodel->normalmins[0] > bounds[i].mins[0]) loadmodel->normalmins[0] = bounds[i].mins[0];
				if (loadmodel->normalmins[1] > bounds[i].mins[1]) loadmodel->normalmins[1] = bounds[i].mins[1];
				if (loadmodel->normalmins[2] > bounds[i].mins[2]) loadmodel->normalmins[2] = bounds[i].mins[2];
				if (loadmodel->normalmaxs[0] < bounds[i].maxs[0]) loadmodel->normalmaxs[0] = bounds[i].maxs[0];
				if (loadmodel->normalmaxs[1] < bounds[i].maxs[1]) loadmodel->normalmaxs[1] = bounds[i].maxs[1];
				if (loadmodel->normalmaxs[2] < bounds[i].maxs[2]) loadmodel->normalmaxs[2] = bounds[i].maxs[2];
			}
			if (bounds[i].xyradius > xyradius)
				xyradius = bounds[i].xyradius;
			if (bounds[i].radius > radius)
				radius = bounds[i].radius;
		}
		loadmodel->yawmins[0] = loadmodel->yawmins[1] = -xyradius;
		loadmodel->yawmaxs[0] = loadmodel->yawmaxs[1] = xyradius;
		loadmodel->yawmins[2] = loadmodel->normalmins[2];
		loadmodel->yawmaxs[2] = loadmodel->normalmaxs[2];
		loadmodel->rotatedmins[0] = loadmodel->rotatedmins[1] = loadmodel->rotatedmins[2] = -radius;
		loadmodel->rotatedmaxs[0] = loadmodel->rotatedmaxs[1] = loadmodel->rotatedmaxs[2] = radius;
		loadmodel->radius = radius;
		loadmodel->radius2 = radius * radius;
	}

	// load triangle data
	inelements = (const int *) (pbase + header->ofs_triangles);
	outelements = loadmodel->surfmesh.data_element3i;
	for (i = 0;i < (int)header->num_triangles;i++)
	{
		outelements[0] = LittleLong(inelements[0]);		
		outelements[1] = LittleLong(inelements[1]);
		outelements[2] = LittleLong(inelements[2]);
		outelements += 3;
		inelements += 3;
	}
	Mod_ValidateElements(loadmodel->surfmesh.data_element3i, loadmodel->surfmesh.num_triangles, 0, header->num_vertexes, __FILE__, __LINE__);

	if (header->ofs_neighbors && loadmodel->surfmesh.data_neighbor3i)
	{
		inelements = (const int *) (pbase + header->ofs_neighbors);
		outelements = loadmodel->surfmesh.data_neighbor3i;
		for (i = 0;i < (int)header->num_triangles;i++)
		{
			outelements[0] = LittleLong(inelements[0]);
			outelements[1] = LittleLong(inelements[1]);
			outelements[2] = LittleLong(inelements[2]);
			outelements += 3;
			inelements += 3;
		}
	}

	// load vertex data
	outvertex = loadmodel->surfmesh.data_vertex3f;
	for (i = 0;i < (int)header->num_vertexes;i++)
	{
		outvertex[0] = LittleFloat(vposition[0]);
		outvertex[1] = LittleFloat(vposition[1]);
		outvertex[2] = LittleFloat(vposition[2]);
		vposition += 3;
		outvertex += 3;
	}

	outtexcoord = loadmodel->surfmesh.data_texcoordtexture2f;
	for (i = 0;i < (int)header->num_vertexes;i++)
	{
		outtexcoord[0] = LittleFloat(vtexcoord[0]);
		outtexcoord[1] = LittleFloat(vtexcoord[1]);
		vtexcoord += 2;
		outtexcoord += 2;
	}

	if(vnormal)
	{
		outnormal = loadmodel->surfmesh.data_normal3f;
		for (i = 0;i < (int)header->num_vertexes;i++)
		{
			outnormal[0] = LittleFloat(vnormal[0]);
			outnormal[1] = LittleFloat(vnormal[1]);
			outnormal[2] = LittleFloat(vnormal[2]);
			vnormal += 3;
			outnormal += 3;
		}
	}

	if(vnormal && vtangent)
	{
		outnormal = loadmodel->surfmesh.data_normal3f;
		outsvector = loadmodel->surfmesh.data_svector3f;
		outtvector = loadmodel->surfmesh.data_tvector3f;
		for (i = 0;i < (int)header->num_vertexes;i++)
		{
			outsvector[0] = LittleFloat(vtangent[0]);
			outsvector[1] = LittleFloat(vtangent[1]);
			outsvector[2] = LittleFloat(vtangent[2]);
			if(LittleFloat(vtangent[3]) < 0)
				CrossProduct(outsvector, outnormal, outtvector);
			else
				CrossProduct(outnormal, outsvector, outtvector);
			vtangent += 4;
			outnormal += 3;
			outsvector += 3;
			outtvector += 3;
		}
	}

	for (i = 0; i < (int)header->num_vertexes;i++)
	{
		blendweights_t weights;
		memcpy(weights.index, vblendindexes + i*4, 4);
		memcpy(weights.influence, vblendweights + i*4, 4);
		loadmodel->surfmesh.blends[i] = Mod_Skeletal_AddBlend(loadmodel, &weights);
	}

	// load meshes
	mesh = (iqmmesh_t *) (pbase + header->ofs_meshes);
	for (i = 0;i < (int)header->num_meshes;i++)
	{
		msurface_t *surface;

		mesh[i].name = LittleLong(mesh[i].name);
		mesh[i].material = LittleLong(mesh[i].material);
		mesh[i].first_vertex = LittleLong(mesh[i].first_vertex);
		mesh[i].num_vertexes = LittleLong(mesh[i].num_vertexes);
		mesh[i].first_triangle = LittleLong(mesh[i].first_triangle);
		mesh[i].num_triangles = LittleLong(mesh[i].num_triangles);

		loadmodel->sortedmodelsurfaces[i] = i;
		surface = loadmodel->data_surfaces + i;
		surface->texture = loadmodel->data_textures + i;
		surface->num_firsttriangle = mesh[i].first_triangle;
		surface->num_triangles = mesh[i].num_triangles;
		surface->num_firstvertex = mesh[i].first_vertex;
		surface->num_vertices = mesh[i].num_vertexes;

		Mod_BuildAliasSkinsFromSkinFiles(loadmodel->data_textures + i, skinfiles, &text[mesh[i].name], &text[mesh[i].material]);
	}

	Mod_FreeSkinFiles(skinfiles);
	Mod_MakeSortedSurfaces(loadmodel);

	// compute all the mesh information that was not loaded from the file
	if (loadmodel->surfmesh.data_element3s)
		for (i = 0;i < loadmodel->surfmesh.num_triangles*3;i++)
			loadmodel->surfmesh.data_element3s[i] = loadmodel->surfmesh.data_element3i[i];
	if (!vnormal)
		Mod_BuildNormals(0, loadmodel->surfmesh.num_vertices, loadmodel->surfmesh.num_triangles, loadmodel->surfmesh.data_vertex3f, loadmodel->surfmesh.data_element3i, loadmodel->surfmesh.data_normal3f, r_smoothnormals_areaweighting.integer != 0);
	if (!vnormal || !vtangent)
		Mod_BuildTextureVectorsFromNormals(0, loadmodel->surfmesh.num_vertices, loadmodel->surfmesh.num_triangles, loadmodel->surfmesh.data_vertex3f, loadmodel->surfmesh.data_texcoordtexture2f, loadmodel->surfmesh.data_normal3f, loadmodel->surfmesh.data_element3i, loadmodel->surfmesh.data_svector3f, loadmodel->surfmesh.data_tvector3f, r_smoothnormals_areaweighting.integer != 0);
	if (!header->ofs_neighbors && loadmodel->surfmesh.data_neighbor3i)
		Mod_BuildTriangleNeighbors(loadmodel->surfmesh.data_neighbor3i, loadmodel->surfmesh.data_element3i, loadmodel->surfmesh.num_triangles);
	if (!header->ofs_bounds)
		Mod_Alias_CalculateBoundingBox();

	loadmodel->surfmesh.isanimated = loadmodel->numframes > 1 || loadmodel->animscenes[0].framecount > 1;

	if (!loadmodel->surfmesh.isanimated)
	{
		Mod_MakeCollisionBIH(loadmodel, true, &loadmodel->collision_bih);
		loadmodel->TraceBox = Mod_CollisionBIH_TraceBox;
		loadmodel->TraceBrush = Mod_CollisionBIH_TraceBrush;
		loadmodel->TraceLine = Mod_CollisionBIH_TraceLine;
		loadmodel->TracePoint = Mod_CollisionBIH_TracePoint_Mesh;
		loadmodel->PointSuperContents = Mod_CollisionBIH_PointSuperContents_Mesh;
	}
}

			
