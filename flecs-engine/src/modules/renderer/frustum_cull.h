#ifndef FLECS_ENGINE_FRUSTUM_CULL_H
#define FLECS_ENGINE_FRUSTUM_CULL_H

#include "../../types.h"

/* Extract 6 normalized frustum planes from a view-projection matrix. */
void flecsEngine_frustum_extractPlanes(
    const float m[4][4],
    float planes[6][4]);

/* Compute world-space AABBs from local AABBs + world transforms
 * using the Arvo method (18 multiplies per instance). */
void flecsEngine_computeWorldAABB(
    const FlecsWorldTransform3 *wt,
    FlecsAABB *aabb,
    int32_t count);

/* Test a world-space AABB against 6 frustum planes (P-vertex approach).
 * Returns true if the AABB is inside or intersects the frustum. */
bool flecsEngine_testAABBFrustum(
    const float planes[6][4],
    const float world_min[3],
    const float world_max[3]);

bool flecsEngine_testScreenSize(
    const float camera_pos[3],
    const float world_min[3],
    const float world_max[3],
    float screen_cull_factor,
    float threshold);

#endif
