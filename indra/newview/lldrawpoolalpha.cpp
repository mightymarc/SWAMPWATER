/** 
 * @file lldrawpoolalpha.cpp
 * @brief LLDrawPoolAlpha class implementation
 *
 * $LicenseInfo:firstyear=2002&license=viewergpl$
 * 
 * Copyright (c) 2002-2009, Linden Research, Inc.
 * 
 * Second Life Viewer Source Code
 * The source code in this file ("Source Code") is provided by Linden Lab
 * to you under the terms of the GNU General Public License, version 2.0
 * ("GPL"), unless you have obtained a separate licensing agreement
 * ("Other License"), formally executed by you and Linden Lab.  Terms of
 * the GPL can be found in doc/GPL-license.txt in this distribution, or
 * online at http://secondlifegrid.net/programs/open_source/licensing/gplv2
 * 
 * There are special exceptions to the terms and conditions of the GPL as
 * it is applied to this Source Code. View the full text of the exception
 * in the file doc/FLOSS-exception.txt in this software distribution, or
 * online at
 * http://secondlifegrid.net/programs/open_source/licensing/flossexception
 * 
 * By copying, modifying or distributing this software, you acknowledge
 * that you have read and understood your obligations described above,
 * and agree to abide by those obligations.
 * 
 * ALL LINDEN LAB SOURCE CODE IS PROVIDED "AS IS." LINDEN LAB MAKES NO
 * WARRANTIES, EXPRESS, IMPLIED OR OTHERWISE, REGARDING ITS ACCURACY,
 * COMPLETENESS OR PERFORMANCE.
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "lldrawpoolalpha.h"

#include "llglheaders.h"
#include "llviewercontrol.h"
#include "llcriticaldamp.h"
#include "llfasttimer.h"
#include "llrender.h"

#include "llcubemap.h"
#include "llsky.h"
#include "lldrawable.h"
#include "llface.h"
#include "llviewercamera.h"
#include "llviewertexturelist.h"	// For debugging
#include "llviewerobjectlist.h" // For debugging
#include "llviewerwindow.h"
#include "pipeline.h"
#include "llviewershadermgr.h"
#include "llviewerregion.h"
#include "lldrawpoolwater.h"
#include "llspatialpartition.h"

BOOL LLDrawPoolAlpha::sShowDebugAlpha = FALSE;

static BOOL deferred_render = FALSE;

LLDrawPoolAlpha::LLDrawPoolAlpha(U32 type) :
		LLRenderPass(type), current_shader(NULL), target_shader(NULL),
		simple_shader(NULL), fullbright_shader(NULL), emissive_shader(NULL),
		mColorSFactor(LLRender::BF_UNDEF), mColorDFactor(LLRender::BF_UNDEF),
		mAlphaSFactor(LLRender::BF_UNDEF), mAlphaDFactor(LLRender::BF_UNDEF)
{

}

LLDrawPoolAlpha::~LLDrawPoolAlpha()
{
}


void LLDrawPoolAlpha::prerender()
{
	mVertexShaderLevel = LLViewerShaderMgr::instance()->getVertexShaderLevel(LLViewerShaderMgr::SHADER_OBJECT);
}

S32 LLDrawPoolAlpha::getNumPostDeferredPasses() 
{ 
	static const LLCachedControl<bool> render_depth_of_field("RenderDepthOfField");
	if (LLPipeline::sImpostorRender)
	{ //skip depth buffer filling pass when rendering impostors
		return 1;
	}
	else if (render_depth_of_field)
	{
		return 2; 
	}
	else
	{
		return 1;
	}
}

void LLDrawPoolAlpha::beginPostDeferredPass(S32 pass) 
{ 
	LLFastTimer t(FTM_RENDER_ALPHA);

	if (pass == 0)
	{
		if (LLPipeline::sImpostorRender)
		{
			simple_shader = &gDeferredAlphaImpostorProgram;
			fullbright_shader = &gDeferredFullbrightProgram;
		}
		else if (LLPipeline::sUnderWaterRender)
		{
			simple_shader = &gDeferredAlphaWaterProgram;
			fullbright_shader = &gDeferredFullbrightWaterProgram;
		}
		else
		{
			simple_shader = &gDeferredAlphaProgram;
			fullbright_shader = &gDeferredFullbrightProgram;
		}
		
		//F32 gamma = gSavedSettings.getF32("RenderDeferredDisplayGamma");

		fullbright_shader->bind();
		fullbright_shader->uniform1f(LLShaderMgr::TEXTURE_GAMMA, 2.2f); 
		//fullbright_shader->uniform1f(LLShaderMgr::DISPLAY_GAMMA, (gamma > 0.1f) ? 1.0f / gamma : (1.0f/2.2f));
		fullbright_shader->unbind();

		//prime simple shader (loads shadow relevant uniforms)
		gPipeline.bindDeferredShader(*simple_shader);

		//simple_shader->uniform1f(LLShaderMgr::DISPLAY_GAMMA, (gamma > 0.1f) ? 1.0f / gamma : (1.0f/2.2f));
	}
	else if (!LLPipeline::sImpostorRender)
	{
		//update depth buffer sampler
		gPipeline.mScreen.flush();
		gPipeline.mDeferredDepth.copyContents(gPipeline.mDeferredScreen, 0, 0, gPipeline.mDeferredScreen.getWidth(), gPipeline.mDeferredScreen.getHeight(),
							0, 0, gPipeline.mDeferredDepth.getWidth(), gPipeline.mDeferredDepth.getHeight(), GL_DEPTH_BUFFER_BIT, GL_NEAREST);	
		gPipeline.mDeferredDepth.bindTarget();
		simple_shader = fullbright_shader = &gObjectFullbrightAlphaMaskProgram;
		gObjectFullbrightAlphaMaskProgram.bind();
		gObjectFullbrightAlphaMaskProgram.setMinimumAlpha(0.33f);
	}


    if (LLPipeline::sRenderDeferred)
    {
		emissive_shader = &gDeferredEmissiveProgram;
    }
    else
    {
		if (LLPipeline::sUnderWaterRender)
		{
			emissive_shader = &gObjectEmissiveWaterProgram;
		}
		else
		{
			emissive_shader = &gObjectEmissiveProgram;
		}
    }

	deferred_render = TRUE;

	// Start out with no shaders.
	current_shader = target_shader = NULL;

	LLGLSLShader::bindNoShader();

	gPipeline.enableLightsDynamic();
}

void LLDrawPoolAlpha::endPostDeferredPass(S32 pass) 
{ 
	if (pass == 1 && !LLPipeline::sImpostorRender)
	{
		gPipeline.mDeferredDepth.flush();
		gPipeline.mScreen.bindTarget();
		gObjectFullbrightAlphaMaskProgram.unbind();
	}

	deferred_render = FALSE;
	endRenderPass(pass);
}

void LLDrawPoolAlpha::renderPostDeferred(S32 pass) 
{ 
	render(pass); 
}

void LLDrawPoolAlpha::beginRenderPass(S32 pass)
{
	LLFastTimer t(FTM_RENDER_ALPHA);
	
	if (LLPipeline::sImpostorRender)
	{
		simple_shader = &gObjectSimpleImpostorProgram;
		fullbright_shader = &gObjectFullbrightProgram;
		emissive_shader = &gObjectEmissiveProgram;
	}
	else if (LLPipeline::sUnderWaterRender)
	{
		simple_shader = &gObjectSimpleWaterProgram;
		fullbright_shader = &gObjectFullbrightWaterProgram;
		emissive_shader = &gObjectEmissiveWaterProgram;
	}
	else
	{
		simple_shader = &gObjectSimpleProgram;
		fullbright_shader = &gObjectFullbrightProgram;
		emissive_shader = &gObjectEmissiveProgram;
	}

	// Start out with no shaders.
	current_shader = target_shader = NULL;

	if (mVertexShaderLevel > 0)
	{
		LLGLSLShader::bindNoShader();
	}

	gPipeline.enableLightsDynamic();
}

void LLDrawPoolAlpha::endRenderPass( S32 pass )
{
	LLFastTimer t(FTM_RENDER_ALPHA);
	LLRenderPass::endRenderPass(pass);

	if(mVertexShaderLevel > 0)	//Singu Note: Unbind if shaders are enabled at all, not just windlight atmospherics..
	{
		LLGLSLShader::bindNoShader();
	}
}

void LLDrawPoolAlpha::render(S32 pass)
{
	LLFastTimer t(FTM_RENDER_ALPHA);

	LLGLSPipelineAlpha gls_pipeline_alpha;

	if (deferred_render && pass == 1)
	{ //depth only
		gGL.setColorMask(false, false);
	}
	else
	{
		gGL.setColorMask(true, true);
	}
	
	bool write_depth = LLDrawPoolWater::sSkipScreenCopy
						 || (deferred_render && pass == 1)
						 // we want depth written so that rendered alpha will
						 // contribute to the alpha mask used for impostors
						 || LLPipeline::sImpostorRenderAlphaDepthPass;

	LLGLDepthTest depth(GL_TRUE, write_depth ? GL_TRUE : GL_FALSE);

	if (deferred_render && pass == 1)
	{
		gGL.blendFunc(LLRender::BF_SOURCE_ALPHA, LLRender::BF_ONE_MINUS_SOURCE_ALPHA);
	}
	else
	{
		mColorSFactor = LLRender::BF_SOURCE_ALPHA;           // } regular alpha blend
		mColorDFactor = LLRender::BF_ONE_MINUS_SOURCE_ALPHA; // }
		mAlphaSFactor = LLRender::BF_ZERO;                         // } glow suppression
		mAlphaDFactor = LLRender::BF_ONE_MINUS_SOURCE_ALPHA;       // }
		gGL.blendFunc(mColorSFactor, mColorDFactor, mAlphaSFactor, mAlphaDFactor);

		if (mVertexShaderLevel > 0)
		{
			if (LLPipeline::sImpostorRender)
			{
				fullbright_shader->bind();
				fullbright_shader->setMinimumAlpha(0.5f);
				simple_shader->bind();
				simple_shader->setMinimumAlpha(0.5f);
			}				
			else
			{
				fullbright_shader->bind();
				fullbright_shader->setMinimumAlpha(0.f);
				simple_shader->bind();
				simple_shader->setMinimumAlpha(0.f);
			}
		}
		else
		{
			if (LLPipeline::sImpostorRender)
			{
				gGL.setAlphaRejectSettings(LLRender::CF_GREATER, 0.5f); //OK
			}
			else
			{
				gGL.setAlphaRejectSettings(LLRender::CF_DEFAULT); //OK
			}
		}
	}

	renderAlpha(getVertexDataMask(), pass);	//getVertexDataMask base mask if fixed-function.

	gGL.setColorMask(true, false);

	if (deferred_render && pass == 1)
	{
		gGL.setSceneBlendType(LLRender::BT_ALPHA);
	}

	if (sShowDebugAlpha)
	{
		BOOL shaders = gPipeline.canUseVertexShaders();
		if(shaders) 
		{
			gHighlightProgram.bind();
		}
		else
		{
			gPipeline.enableLightsFullbright(LLColor4(1,1,1,1));
		}

		gGL.diffuseColor4f(0.9,0,0,0.4);
				
		LLViewerFetchedTexture::sSmokeImagep->addTextureStats(1024.f*1024.f);
		gGL.getTexUnit(0)->bind(LLViewerFetchedTexture::sSmokeImagep, TRUE) ;
		renderAlphaHighlight(LLVertexBuffer::MAP_VERTEX |
							LLVertexBuffer::MAP_TEXCOORD0);

		pushBatches(LLRenderPass::PASS_ALPHA_MASK, LLVertexBuffer::MAP_VERTEX | LLVertexBuffer::MAP_TEXCOORD0, FALSE);
		pushBatches(LLRenderPass::PASS_FULLBRIGHT_ALPHA_MASK, LLVertexBuffer::MAP_VERTEX | LLVertexBuffer::MAP_TEXCOORD0, FALSE);
		pushBatches(LLRenderPass::PASS_ALPHA_INVISIBLE, LLVertexBuffer::MAP_VERTEX | LLVertexBuffer::MAP_TEXCOORD0, FALSE);

		if(shaders) 
		{
			gHighlightProgram.unbind();
		}
	}
	gGL.setSceneBlendType(LLRender::BT_ALPHA);
}

void LLDrawPoolAlpha::renderAlphaHighlight(U32 mask)
{
	for (LLCullResult::sg_iterator i = gPipeline.beginAlphaGroups(); i != gPipeline.endAlphaGroups(); ++i)
	{
		LLSpatialGroup* group = *i;
		if (group->mSpatialPartition->mRenderByGroup &&
			!group->isDead())
		{
			LLSpatialGroup::drawmap_elem_t& draw_info = group->mDrawMap[LLRenderPass::PASS_ALPHA];	

			for (LLSpatialGroup::drawmap_elem_t::iterator k = draw_info.begin(); k != draw_info.end(); ++k)	
			{
				LLDrawInfo& params = **k;
				
				if (params.mParticle)
				{
					continue;
				}

				LLRenderPass::applyModelMatrix(params);
				if (params.mGroup)
				{
					params.mGroup->rebuildMesh();
				}
				params.mVertexBuffer->setBuffer(mask);
				params.mVertexBuffer->drawRange(params.mDrawMode, params.mStart, params.mEnd, params.mCount, params.mOffset);
				gPipeline.addTrianglesDrawn(params.mCount, params.mDrawMode);
			}
		}
	}
}

void LLDrawPoolAlpha::renderAlpha(U32 mask, S32 pass)
{
	BOOL initialized_lighting = FALSE;
	BOOL light_enabled = TRUE;
	
	BOOL use_shaders = gPipeline.canUseVertexShaders();

	BOOL depth_only = (pass == 1 && !LLPipeline::sImpostorRender);
		
	for (LLCullResult::sg_iterator i = gPipeline.beginAlphaGroups(); i != gPipeline.endAlphaGroups(); ++i)
	{
		LLSpatialGroup* group = *i;
		llassert(group);
		llassert(group->mSpatialPartition);

		if (group->mSpatialPartition->mRenderByGroup &&
		    !group->isDead())
		{
			bool is_particle_or_hud_particle = group->mSpatialPartition->mPartitionType == LLViewerRegion::PARTITION_PARTICLE
													  || group->mSpatialPartition->mPartitionType == LLViewerRegion::PARTITION_CLOUD
													  || group->mSpatialPartition->mPartitionType == LLViewerRegion::PARTITION_HUD_PARTICLE;

			bool draw_glow_for_this_partition = !depth_only && mVertexShaderLevel > 0; // no shaders = no glow.

			static LLFastTimer::DeclareTimer FTM_RENDER_ALPHA_GROUP_LOOP("Alpha Group");
			LLFastTimer t(FTM_RENDER_ALPHA_GROUP_LOOP);

			bool disable_cull = is_particle_or_hud_particle;
			LLGLDisable cull(disable_cull ? GL_CULL_FACE : 0);

			LLSpatialGroup::drawmap_elem_t& draw_info = group->mDrawMap[LLRenderPass::PASS_ALPHA];

			for (LLSpatialGroup::drawmap_elem_t::iterator k = draw_info.begin(); k != draw_info.end(); ++k)	
			{
				LLDrawInfo& params = **k;

				/*if ((params.mVertexBuffer->getTypeMask() & mask) != mask)
				{ //FIXME!
					llwarns << "Missing required components, skipping render batch." << llendl;
					continue;
				}*/

				// Fix for bug - NORSPEC-271
				// If the face is more than 90% transparent, then don't update the Depth buffer for Dof
				// We don't want the nearly invisible objects to cause of DoF effects
				if(depth_only)
				{
					LLFace*	face = params.mFace;
					if(face)
					{
						const LLTextureEntry* tep = face->getTextureEntry();
						if(tep)
						{
							if(tep->getColor().mV[3] < 0.1f)
								continue;
						}
					}
				}

				LLRenderPass::applyModelMatrix(params);

				// Singu Note: Logic reordered here. Removed a fair bit of unneeded condition checks (mostly related to deferred mode), and made
				// shader selection more explicit.
				
				if (params.mGroup)
				{
					params.mGroup->rebuildMesh();
				}
				
				LLMaterial* mat = deferred_render ? params.mMaterial.get() : NULL;

				if (!use_shaders)
				{
					llassert_always(!target_shader);
					llassert_always(!current_shader);
					llassert_always(!LLGLSLShader::sNoFixedFunction);
					llassert_always(!LLGLSLShader::sCurBoundShaderPtr);

					bool fullbright = depth_only || params.mFullbright;
					if(fullbright == !!light_enabled || !initialized_lighting)
					{
						light_enabled = !fullbright;
						initialized_lighting = true;

						if (light_enabled)	// Turn off lighting if it hasn't already been so.
						{
							gPipeline.enableLightsDynamic();
						}
						else	// Turn on lighting if it isn't already.
						{
							gPipeline.enableLightsFullbright(LLColor4(1,1,1,1));
						}
					}
				}
				else
				{
					if (mat)
					{
						U32 mask = params.mShaderMask;

						llassert(mask < LLMaterial::SHADER_COUNT);
						target_shader = LLPipeline::sUnderWaterRender ? &(gDeferredMaterialWaterProgram[mask]) : &(gDeferredMaterialProgram[mask]);

						// If we need shaders, and we're not ALREADY using the proper shader, then bind it
						// (this way we won't rebind shaders unnecessarily).
						if (current_shader != target_shader)
						{
							gPipeline.bindDeferredShader(*target_shader);
						}
					}
					else
					{
						target_shader = params.mFullbright ? fullbright_shader : simple_shader;

						// If we need shaders, and we're not ALREADY using the proper shader, then bind it
						// (this way we won't rebind shaders unnecessarily).
						if(current_shader != target_shader)
						{
							target_shader->bind();
						}
					}
					current_shader = target_shader;

					if(mat)
					{
						current_shader->uniform4f(LLShaderMgr::SPECULAR_COLOR, params.mSpecColor.mV[0], params.mSpecColor.mV[1], params.mSpecColor.mV[2], params.mSpecColor.mV[3]);						
						current_shader->uniform1f(LLShaderMgr::ENVIRONMENT_INTENSITY, params.mEnvIntensity);
						current_shader->uniform1f(LLShaderMgr::EMISSIVE_BRIGHTNESS, params.mFullbright ? 1.f : 0.f);

						if (params.mNormalMap)
						{
							params.mNormalMap->addTextureStats(params.mVSize);
							current_shader->bindTexture(LLShaderMgr::BUMP_MAP, params.mNormalMap);
						} 
						
						if (params.mSpecularMap)
						{
							params.mSpecularMap->addTextureStats(params.mVSize);
							current_shader->bindTexture(LLShaderMgr::SPECULAR_MAP, params.mSpecularMap);
						}
					}
					/*else if(deferred_render && current_shader == simple_shader)
					{
						current_shader->uniform4f(LLShaderMgr::SPECULAR_COLOR, 1.0f, 1.0f, 1.0f, 1.0f);						
						current_shader->uniform1f(LLShaderMgr::ENVIRONMENT_INTENSITY, 0.0f);			
						LLViewerFetchedTexture::sFlatNormalImagep->addTextureStats(params.mVSize);
						current_shader->bindTexture(LLShaderMgr::BUMP_MAP, LLViewerFetchedTexture::sFlatNormalImagep);						
						LLViewerFetchedTexture::sWhiteImagep->addTextureStats(params.mVSize);
						current_shader->bindTexture(LLShaderMgr::SPECULAR_MAP, LLViewerFetchedTexture::sWhiteImagep);
					}*/

					if (params.mTextureList.size() > 1)
					{
						for (U32 i = 0; i < params.mTextureList.size(); ++i)
						{
							if (params.mTextureList[i].notNull())
							{
								gGL.getTexUnit(i)->bind(params.mTextureList[i], TRUE);
							}
						}
					}
				}

				bool tex_setup = false;
				if(!use_shaders || params.mTextureList.size() <= 1)
				{ //not batching textures or batch has only 1 texture -- might need a texture matrix
					if (params.mTexture.notNull())
					{
						params.mTexture->addTextureStats(params.mVSize);
						if (mat)
						{
							current_shader->bindTexture(LLShaderMgr::DIFFUSE_MAP, params.mTexture);
						}
						else
						{
							gGL.getTexUnit(0)->bind(params.mTexture, TRUE);
						}
						
						if (params.mTextureMatrix)
						{
							tex_setup = true;
							gGL.getTexUnit(0)->activate();
							gGL.matrixMode(LLRender::MM_TEXTURE);
							gGL.loadMatrix((GLfloat*) params.mTextureMatrix->mMatrix);
							gPipeline.mTextureMatrixOps++;
						}
					}
					else
					{
						gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
					}
				}

				static LLFastTimer::DeclareTimer FTM_RENDER_ALPHA_PUSH("Alpha Push Verts");
				{
					LLFastTimer t(FTM_RENDER_ALPHA_PUSH);

					gGL.blendFunc((LLRender::eBlendFactor) params.mBlendFuncSrc, (LLRender::eBlendFactor) params.mBlendFuncDst, mAlphaSFactor, mAlphaDFactor);
					// Singu Note: If using shaders, pull the attribute mask from it, else used passed base mask.
					params.mVertexBuffer->setBuffer(current_shader ? current_shader->mAttributeMask : mask);
                
					params.mVertexBuffer->drawRange(params.mDrawMode, params.mStart, params.mEnd, params.mCount, params.mOffset);
					gPipeline.addTrianglesDrawn(params.mCount, params.mDrawMode);
				}
				
				// If this alpha mesh has glow, then draw it a second time to add the destination-alpha (=glow).  Interleaving these state-changing calls could be expensive, but glow must be drawn Z-sorted with alpha.
				if (current_shader && 
					draw_glow_for_this_partition &&
					(!is_particle_or_hud_particle || params.mHasGlow) &&	//only do this second pass for batches that actually have glow
					params.mVertexBuffer->hasDataType(LLVertexBuffer::TYPE_EMISSIVE))
				{
					// install glow-accumulating blend mode
					gGL.blendFunc(LLRender::BF_ZERO, LLRender::BF_ONE, // don't touch color
						      LLRender::BF_ONE, LLRender::BF_ONE); // add to alpha (glow)

					emissive_shader->bind();
					
					// glow doesn't use vertex colors from the mesh data
					// Singu Note: Pull attribs from shader, since we always have one here.
					params.mVertexBuffer->setBuffer(emissive_shader->mAttributeMask);
					
					// do the actual drawing, again
					params.mVertexBuffer->drawRange(params.mDrawMode, params.mStart, params.mEnd, params.mCount, params.mOffset);
					gPipeline.addTrianglesDrawn(params.mCount, params.mDrawMode);

					// restore our alpha blend mode
					gGL.blendFunc(mColorSFactor, mColorDFactor, mAlphaSFactor, mAlphaDFactor);

					current_shader->bind();
				}
			
				if (tex_setup)
				{
					gGL.getTexUnit(0)->activate();
					gGL.loadIdentity();
					gGL.matrixMode(LLRender::MM_MODELVIEW);
				}
			}
		}
	}

	gGL.setSceneBlendType(LLRender::BT_ALPHA);

	LLVertexBuffer::unbind();	
		
	if (!light_enabled)
	{
		gPipeline.enableLightsDynamic();
	}
}
