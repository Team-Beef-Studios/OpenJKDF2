#include "sithRenderSky.h"

#include "General/stdMath.h"
#include "Engine/sithCamera.h"
#include "Engine/sithIntersect.h"
#include "World/sithSector.h"
#include "World/sithSurface.h"
#include "World/sithWorld.h"
#include "Raster/rdCache.h"
#include "Engine/rdroid.h"
#include "Primitives/rdMatrix.h"
#include "jk.h"

#ifdef PLATFORM_VR
// ---------------------------------------------------------------------------
// Real world-fixed VR sky dome.
//
// The legacy sky (sithRenderSky_TransformHorizontal/Vertical) is a 2D screen-space
// backdrop that has no correct stereo-VR equivalent. For VR (rdCamera_bGpuProjection)
// we instead render a real tessellated dome of unit directions, world-fixed at infinity,
// textured with the level's sky material. The dome verts are emitted in VIEW space so the
// GPU per-eye projection (default_v.glsl) handles stereo automatically. The sky surfaces
// are skipped (see sithRender.c) and the dome sits just inside the far plane, so the world
// geometry naturally occludes it everywhere except through the sky openings.
// ---------------------------------------------------------------------------
extern int rdCamera_bGpuProjection;

#define SKYDOME_NLAT   16   // latitude (elevation) bands: -90..+90
#define SKYDOME_NLONG  32   // longitude (azimuth) bands: 0..360
#define SKYDOME_NVERTS ((SKYDOME_NLAT + 1) * (SKYDOME_NLONG + 1))

static rdVector3 sithRenderSky_domeDir[SKYDOME_NVERTS];   // unit world directions (x=east, y=north, z=up)
static flex_t    sithRenderSky_domeAzim[SKYDOME_NVERTS];  // degrees
static flex_t    sithRenderSky_domeElev[SKYDOME_NVERTS];  // degrees
static int       sithRenderSky_domeReady = 0;

static void sithRenderSky_GenDome(void)
{
    for (int i = 0; i <= SKYDOME_NLAT; i++)
    {
        flex_t elev = -90.0 + 180.0 * (flex_t)i / (flex_t)SKYDOME_NLAT;
        flex_t se, ce;
        stdMath_SinCos(elev, &se, &ce);
        for (int j = 0; j <= SKYDOME_NLONG; j++)
        {
            flex_t azim = 360.0 * (flex_t)j / (flex_t)SKYDOME_NLONG;
            flex_t sa, ca;
            stdMath_SinCos(azim, &sa, &ca);
            int v = i * (SKYDOME_NLONG + 1) + j;
            sithRenderSky_domeDir[v].x = ce * sa;
            sithRenderSky_domeDir[v].y = ce * ca;
            sithRenderSky_domeDir[v].z = se;
            sithRenderSky_domeAzim[v] = azim;
            sithRenderSky_domeElev[v] = elev;
        }
    }
    sithRenderSky_domeReady = 1;
}

void sithRenderSky_DrawVRDome(void)
{
    if (!rdCamera_bGpuProjection)
        return;
    if (!sithRenderSky_domeReady)
        sithRenderSky_GenDome();

    sithWorld* pWorld = sithWorld_pCurrentWorld;
    if (!pWorld || !pWorld->surfaces)
        return;

    // Grab the horizon and ceiling sky materials from the level's sky surfaces.
    rdMaterial* pHorizonMat = NULL;
    rdMaterial* pCeilingMat = NULL;
    for (int s = 0; s < pWorld->numSurfaces; s++)
    {
        uint32_t flags = pWorld->surfaces[s].surfaceFlags;
        if (!pHorizonMat && (flags & SITH_SURFACE_HORIZON_SKY))
            pHorizonMat = pWorld->surfaces[s].surfaceInfo.face.material;
        if (!pCeilingMat && (flags & SITH_SURFACE_CEILING_SKY))
            pCeilingMat = pWorld->surfaces[s].surfaceInfo.face.material;
        if (pHorizonMat && pCeilingMat)
            break;
    }
    if (!pHorizonMat && !pCeilingMat)
        return;

    // Added: the dome is queued into the shared face cache BEFORE sithRender_Draw sets up the
    // frame's render state, so it used to be flushed under whatever the previous pass (often the
    // depth-less HUD) left behind. When that happened the dome painted over the world above its
    // horizon for one frame - the "white flash". Set the depth mode we need, and flush the dome
    // on its own so no later state change can reach it.
    rdSetZBufferMethod(RD_ZBUFFER_READ_WRITE);

    flex_t farY = rdCamera_pCurCamera->pClipFrustum->zFar - 0.2;
    flex_t ppr  = sithSector_horizontalPixelsPerRev_idk;  // pixels-per-degree (pixelsPerRev/360)
    rdVector2 hOff = pWorld->horizontalSkyOffs;
    rdVector2 cOff = pWorld->ceilingSkyOffs;
    rdVector3 camPos = sithCamera_currentCamera->vec3_1;

    // Above this elevation, use the CEILING sky (material + the original spherical ceiling mapping,
    // which is smooth at the zenith). The horizon band below keeps the cylindrical mapping. If the
    // level has no ceiling sky, the whole dome stays horizon/cylindrical (same as before -> no regress).
    const flex_t SKYDOME_CEIL_ELEV = 25.0;

    for (int i = 0; i < SKYDOME_NLAT; i++)
    {
        for (int j = 0; j < SKYDOME_NLONG; j++)
        {
            int v00 = i * (SKYDOME_NLONG + 1) + j;
            int v01 = v00 + 1;
            int v10 = v00 + (SKYDOME_NLONG + 1);
            int v11 = v10 + 1;
            int idx[4] = { v00, v01, v11, v10 };

            // Per-quad scheme: ceiling cap if a ceiling material exists and we're high enough (or there's
            // no horizon material at all -> ceiling-only level uses ceiling everywhere).
            int bCeil = (pCeilingMat != NULL) && (!pHorizonMat || sithRenderSky_domeElev[v00] >= SKYDOME_CEIL_ELEV);
            rdMaterial* mat = bCeil ? pCeilingMat : pHorizonMat;
            if (!mat)
                continue;

            rdProcEntry* pe = rdCache_GetProcEntry();
            if (!pe)
                continue;
            pe->geometryMode      = RD_GEOMODE_TEXTURED;
            pe->lightingMode      = RD_LIGHTMODE_FULLYLIT;
            pe->textureMode       = RD_TEXTUREMODE_PERSPECTIVE;
            pe->material          = mat;
            pe->wallCel           = 0;
            pe->type              = 0;
            pe->extralight        = 0.0;
            pe->ambientLight      = 1.0;
            pe->light_level_static = 1.0;

            for (int k = 0; k < 4; k++)
            {
                int vi = idx[k];
                rdVector3 vView;
                // World-fixed dome at infinity: only the view ROTATION applies (translation drops).
                rdMatrix_TransformVector34(&vView, &sithRenderSky_domeDir[vi], &rdCamera_pCurCamera->view_matrix);
                pe->vertices[k].x = vView.x * farY;
                pe->vertices[k].y = vView.y * farY;
                pe->vertices[k].z = vView.z * farY;

                if (bCeil)
                {
                    // Original ceiling-sky mapping: intersect the view ray with the sky sphere, UV = hit.xy*16.
                    // This is smooth at the zenith (no pole pinch), and is what the level was authored for.
                    rdVector3 a1a = sithRenderSky_domeDir[vi];
                    flex_t tmp;
                    if (!sithIntersect_SphereHit(&camPos, &a1a, 1000.0, 0.0, &sithSector_surfaceNormal, &sithSector_zMaxVec, &tmp, 0))
                        tmp = 1000.0;
                    rdVector_Scale3Acc(&a1a, tmp);
                    rdVector_Add3Acc(&a1a, &camPos);
                    pe->vertexUVs[k].x = a1a.x * 16.0 + cOff.x;
                    pe->vertexUVs[k].y = a1a.y * 16.0 + cOff.y;
                }
                else
                {
                    // Cylindrical horizon UV (azimuth->u, elevation->v); REPEAT tiles seamlessly.
                    pe->vertexUVs[k].x =  sithRenderSky_domeAzim[vi] * ppr + hOff.x;
                    pe->vertexUVs[k].y = -sithRenderSky_domeElev[vi] * ppr + hOff.y;
                }
                pe->vertexIntensities[k] = 1.0;
            }
            rdCache_AddProcFace(0, 4, 7);
        }
    }

    // The dome fills half of RDCACHE_MAX_TRIS; flushing here also frees those slots for the world.
    rdCache_Flush();
}
#endif // PLATFORM_VR

int sithRenderSky_Open(flex_t horizontalPixelsPerRev, flex_t horizontalDist, flex_t ceilingSky)
{
    sithSector_horizontalPixelsPerRev_idk = horizontalPixelsPerRev * 0.0027777778;
    sithSector_horizontalDist = horizontalDist;
    sithSector_ceilingSky = ceilingSky;
    sithSector_zMaxVec.x = 0.0;
    sithSector_zMaxVec.y = 0.0;
    sithSector_zMaxVec.z = ceilingSky;
    sithSector_horizontalPixelsPerRev = horizontalPixelsPerRev;
    sithSector_zMinVec.x = 0.0;
    sithSector_zMinVec.y = 0.0;
    sithSector_zMinVec.z = -ceilingSky;
    return 1;
}

void sithRenderSky_Close()
{
}

void sithRenderSky_Update()
{
    rdVector3 viewAngles = sithCamera_currentCamera->viewPYR;
#ifdef PLATFORM_VR
    // VR multiview (GPU per-eye projection): the true view orientation is the game camera combined
    // with the HMD head pose, baked into the center view (rdCamera_camMatrix). The game camera's
    // viewPYR alone omits the HMD rotation, so the horizontal sky's texture scroll stays pinned to
    // the head while the geometry tracks it -> the sky looks "locked to your head". Use the center
    // view's angles so the scroll/roll matches the head-aware geometry.
    if (rdCamera_bGpuProjection) {
        rdMatrix_ExtractAngles34(&rdCamera_camMatrix, &viewAngles);
    }
#endif
    sithSector_flt_8553C0 = sithSector_horizontalDist / rdCamera_pCurCamera->fovDx;
    stdMath_SinCos(viewAngles.z, &sithSector_flt_8553F4, &sithSector_flt_8553C8);
    sithSector_flt_8553B8 = -(viewAngles.y * sithSector_horizontalPixelsPerRev_idk);
    sithSector_flt_8553C4 = -(viewAngles.x * sithSector_horizontalPixelsPerRev_idk);
}

// As seen in: Return Home to Sulon
void sithRenderSky_TransformHorizontal(rdProcEntry *pProcEntry, sithSurfaceInfo *pSurfaceInfo, uint32_t num_vertices)
{
    rdVector2 *pVertUV;
    rdVector3 *pVertXYZ;
    flex_d_t tmp1;
    flex_d_t tmp2;

    pProcEntry->geometryMode = sithRender_geoMode > RD_GEOMODE_TEXTURED ? RD_GEOMODE_TEXTURED : sithRender_geoMode;
    pProcEntry->lightingMode = sithRender_lightMode > RD_LIGHTMODE_FULLYLIT ? RD_LIGHTMODE_FULLYLIT : sithRender_lightMode;
    pProcEntry->textureMode = sithRender_texMode > RD_TEXTUREMODE_AFFINE ? RD_TEXTUREMODE_AFFINE : sithRender_texMode;
    
    pVertUV = pProcEntry->vertexUVs;
    pVertXYZ = pProcEntry->vertices;

    while ( num_vertices )
    {
#ifdef TARGET_TWL
        rdVector3 proj;
        rdCamera_pCurCamera->fnProjectLstClip(&proj, pVertXYZ, 1);
        tmp1 = (proj.x - rdCamera_pCurCamera->canvas->half_screen_width) * sithSector_flt_8553C0;
        tmp2 = (proj.y - rdCamera_pCurCamera->canvas->half_screen_height) * sithSector_flt_8553C0;

        flex_t prev_z = pVertXYZ->y;
        pVertXYZ->y = rdCamera_pCurCamera->pClipFrustum->zFar - 0.1;

        pVertXYZ->x /= prev_z;
        pVertXYZ->x *= pVertXYZ->y;
        pVertXYZ->z /= prev_z;
        pVertXYZ->z *= pVertXYZ->y;
#else
#ifdef PLATFORM_VR
        if (rdCamera_bGpuProjection) {
            // GPU per-eye projection: pVertXYZ is VIEW-space (x=right, y=forward, z=up), not the
            // CPU-projected screen coords this path normally expects. The old screen-space UV
            // reduces EXACTLY to a view-direction ratio (the fovDx cancels), so compute it from
            // view space directly; then push the vertex out along its view ray to the far plane
            // (preserving direction) so the GPU projects it per-eye behind everything. This mirrors
            // the TARGET_TWL branch above, which is also a view-space (HW-projected) path.
            flex_t fwd = pVertXYZ->y;
            if (fwd == 0.0) fwd = 0.000001;
            tmp1 =  (pVertXYZ->x / fwd) * sithSector_horizontalDist;
            tmp2 = -(pVertXYZ->z / fwd) * sithSector_horizontalDist;
            flex_t farY = rdCamera_pCurCamera->pClipFrustum->zFar - 0.1;
            pVertXYZ->x = pVertXYZ->x / fwd * farY;
            pVertXYZ->z = pVertXYZ->z / fwd * farY;
            pVertXYZ->y = farY;
        } else
#endif
        {
            pVertXYZ->z = rdCamera_pCurCamera->pClipFrustum->zFar; // zFar
            tmp1 = (pVertXYZ->x - rdCamera_pCurCamera->canvas->half_screen_width) * sithSector_flt_8553C0;
            tmp2 = (pVertXYZ->y - rdCamera_pCurCamera->canvas->half_screen_height) * sithSector_flt_8553C0;
        }
#endif

        pVertUV->x = tmp1 * sithSector_flt_8553C8 - tmp2 * sithSector_flt_8553F4 + sithSector_flt_8553B8;
        pVertUV->y = tmp2 * sithSector_flt_8553C8 + tmp1 * sithSector_flt_8553F4 + sithSector_flt_8553C4;
        rdVector_Add2Acc(pVertUV, &sithWorld_pCurrentWorld->horizontalSkyOffs);
        rdVector_Add2Acc(pVertUV, &pSurfaceInfo->face.clipIdk);

        ++pVertXYZ;
        ++pVertUV;
        --num_vertices;
    }
}

// As seen in: Canyon Oasis, Droidworks' `Pulley`
void sithRenderSky_TransformVertical(rdProcEntry *pProcEntry, sithSurfaceInfo *pSurfaceInfo, rdVector3 *pUntransformedVerts, uint32_t num_vertices)
{
    rdVector2 *pVertUV;
    rdVector3 a1a;
    rdVector3 a2a;
    rdVector3 vertex_out;

    pProcEntry->geometryMode = sithRender_geoMode > RD_GEOMODE_TEXTURED ? RD_GEOMODE_TEXTURED : sithRender_geoMode;
    pProcEntry->lightingMode = sithRender_lightMode > RD_LIGHTMODE_FULLYLIT ? RD_LIGHTMODE_FULLYLIT : sithRender_lightMode;
    // Weird, no texture mode, though idk if the affine mode even worked
#ifdef TARGET_TWL
    pProcEntry->textureMode = sithRender_texMode > RD_TEXTUREMODE_AFFINE ? RD_TEXTUREMODE_AFFINE : sithRender_texMode;
#endif

    // Jones3D does this, idk
#ifdef QOL_IMPROVEMENTS
    //float invMatWidth = 1.0f / (float)pSurfaceInfo->face.material->texinfos[0]->texture_ptr->texture_struct[0]->format.width;
    //float invMatHeight = 1.0f / (float)pSurfaceInfo->face.material->texinfos[0]->texture_ptr->texture_struct[0]->format.height;
#endif

    // TODO: Clamp vertices to horizon? Would be easier to just have a skybox tbh
#ifdef QOL_IMPROVEMENTS
    //BOOL bHitTestFailed = false;
#endif

    for (uint32_t i = 0; i < num_vertices; i++)
    {
        rdMatrix_TransformPoint34(&a2a, &pUntransformedVerts[i], &rdCamera_camMatrix);
        rdVector_Sub3Acc(&a2a, &sithCamera_currentCamera->vec3_1);

        // This seems to bug out when a2a.z < 0.0 (not sure how that's even happening)
        rdVector_Normalize3(&a1a, &a2a);

        const flex_t hitTestMaxZ = 1000.0;
        flex_t tmp = 0.0;
        if (!sithIntersect_SphereHit(&sithCamera_currentCamera->vec3_1, &a1a, hitTestMaxZ, 0.0, &sithSector_surfaceNormal, &sithSector_zMaxVec, &tmp, 0)) {
            tmp = hitTestMaxZ;
#ifdef QOL_IMPROVEMENTS
            /*bHitTestFailed = true;
            break;*/
#endif
        }
        rdVector_Scale3Acc(&a1a, tmp);
        pVertUV = &pProcEntry->vertexUVs[i];
        rdVector_Add3Acc(&a1a, &sithCamera_currentCamera->vec3_1);
        rdVector_Scale2(pVertUV, (rdVector2*)&a1a, 16.0);

#ifdef QOL_IMPROVEMENTS
        //pVertUV->x *= invMatWidth;
        //pVertUV->y *= invMatHeight;
#endif

        rdVector_Add2Acc(pVertUV, &sithWorld_pCurrentWorld->ceilingSkyOffs);
        rdVector_Add2Acc(pVertUV, &pSurfaceInfo->face.clipIdk);
        rdMatrix_TransformPoint34(&vertex_out, &a1a, &sithCamera_currentCamera->rdCam.view_matrix);

#ifdef TARGET_TWL
        flex_t prev_z = pProcEntry->vertices[i].y;
        vertex_out.y *= 0.15;
        pProcEntry->vertices[i].y = vertex_out.y;
        pProcEntry->vertices[i].y = stdMath_Clamp(pProcEntry->vertices[i].y, 0.0f, rdCamera_pCurCamera->pClipFrustum->zFar - 0.1);
        pProcEntry->vertices[i].x /= prev_z;
        pProcEntry->vertices[i].x *= pProcEntry->vertices[i].y;
        pProcEntry->vertices[i].z /= prev_z;
        pProcEntry->vertices[i].z *= pProcEntry->vertices[i].y;
#else
#ifdef PLATFORM_VR
        if (rdCamera_bGpuProjection) {
            // GPU per-eye projection: emit the full VIEW-space sky-dome point. It is colinear with
            // the surface vertex (same direction from the camera), so it lands at the same screen
            // position when the GPU projects it per-eye; clamp its depth to just inside the far
            // plane so it isn't far-clipped, preserving direction.
            flex_t fwd = vertex_out.y;
            if (fwd < 0.000001) fwd = 0.000001;
            flex_t farY = rdCamera_pCurCamera->pClipFrustum->zFar - 0.1;
            flex_t s = (fwd > farY) ? (farY / fwd) : 1.0;
            pProcEntry->vertices[i].x = vertex_out.x * s;
            pProcEntry->vertices[i].y = fwd * s;
            pProcEntry->vertices[i].z = vertex_out.z * s;
        } else
#endif
        {
            pProcEntry->vertices[i].z = vertex_out.y;
        }
#endif
        // TODO: There's a bug where facing a vertical wall of sky starts dividing strangely
    }

#ifdef QOL_IMPROVEMENTS
    /*if (bHitTestFailed) {
        pProcEntry->geometryMode = RD_GEOMODE_SOLIDCOLOR;
    }*/
#endif
}