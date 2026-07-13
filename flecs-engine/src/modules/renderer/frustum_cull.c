#include <math.h>
#include "frustum_cull.h"

void flecsEngine_frustum_extractPlanes(
    const float m[4][4],
    float planes[6][4])
{
    /* cglm is column-major: m[col][row].
     * Row i of the matrix = m[0][i], m[1][i], m[2][i], m[3][i] */

    /* Left:   row3 + row0 */
    planes[0][0] = m[0][3] + m[0][0];
    planes[0][1] = m[1][3] + m[1][0];
    planes[0][2] = m[2][3] + m[2][0];
    planes[0][3] = m[3][3] + m[3][0];

    /* Right:  row3 - row0 */
    planes[1][0] = m[0][3] - m[0][0];
    planes[1][1] = m[1][3] - m[1][0];
    planes[1][2] = m[2][3] - m[2][0];
    planes[1][3] = m[3][3] - m[3][0];

    /* Bottom: row3 + row1 */
    planes[2][0] = m[0][3] + m[0][1];
    planes[2][1] = m[1][3] + m[1][1];
    planes[2][2] = m[2][3] + m[2][1];
    planes[2][3] = m[3][3] + m[3][1];

    /* Top:    row3 - row1 */
    planes[3][0] = m[0][3] - m[0][1];
    planes[3][1] = m[1][3] - m[1][1];
    planes[3][2] = m[2][3] - m[2][1];
    planes[3][3] = m[3][3] - m[3][1];

    /* Near:   row3 + row2 */
    planes[4][0] = m[0][3] + m[0][2];
    planes[4][1] = m[1][3] + m[1][2];
    planes[4][2] = m[2][3] + m[2][2];
    planes[4][3] = m[3][3] + m[3][2];

    /* Far:    row3 - row2 */
    planes[5][0] = m[0][3] - m[0][2];
    planes[5][1] = m[1][3] - m[1][2];
    planes[5][2] = m[2][3] - m[2][2];
    planes[5][3] = m[3][3] - m[3][2];

    /* Normalize each plane */
    for (int i = 0; i < 6; i ++) {
        float len = sqrtf(
            planes[i][0] * planes[i][0] +
            planes[i][1] * planes[i][1] +
            planes[i][2] * planes[i][2]);
        if (len > 1e-8f) {
            float inv = 1.0f / len;
            planes[i][0] *= inv;
            planes[i][1] *= inv;
            planes[i][2] *= inv;
            planes[i][3] *= inv;
        }
    }
}

void flecsEngine_computeWorldAABB(
    const FlecsWorldTransform3 *wt,
    FlecsAABB *aabb,
    int32_t count)
{
    for (int32_t n = 0; n < count; n ++) {
        float smin[3] = { aabb[n].min[0], aabb[n].min[1], aabb[n].min[2] };
        float smax[3] = { aabb[n].max[0], aabb[n].max[1], aabb[n].max[2] };

        for (int i = 0; i < 3; i ++) {
            aabb[n].min[i] = aabb[n].max[i] = wt[n].m[3][i];
            for (int j = 0; j < 3; j ++) {
                float e = wt[n].m[j][i] * smin[j];
                float f = wt[n].m[j][i] * smax[j];
                if (e < f) {
                    aabb[n].min[i] += e;
                    aabb[n].max[i] += f;
                } else {
                    aabb[n].min[i] += f;
                    aabb[n].max[i] += e;
                }
            }
        }
    }
}

bool flecsEngine_testScreenSize(
    const float camera_pos[3],
    const float world_min[3],
    const float world_max[3],
    float screen_cull_factor,
    float threshold)
{
    float cx = (world_min[0] + world_max[0]) * 0.5f;
    float cy = (world_min[1] + world_max[1]) * 0.5f;
    float cz = (world_min[2] + world_max[2]) * 0.5f;

    float hx = (world_max[0] - world_min[0]) * 0.5f;
    float hy = (world_max[1] - world_min[1]) * 0.5f;
    float hz = (world_max[2] - world_min[2]) * 0.5f;

    /* Use the max axis half-extent instead of bounding-sphere radius — tighter
     * and more intuitive (a 1m cube has max=0.5, not √0.75≈0.866). */
    float m = hx > hy ? hx : hy;
    if (hz > m) m = hz;
    float r_sq = m * m;

    float dx = cx - camera_pos[0];
    float dy = cy - camera_pos[1];
    float dz = cz - camera_pos[2];
    float d_sq = dx * dx + dy * dy + dz * dz;

    /* `threshold` is the minimum projected diameter in pixels. Squared on
     * both sides lets us avoid a sqrt. */
    return r_sq * screen_cull_factor >= threshold * threshold * d_sq;
}

bool flecsEngine_testAABBFrustum(
    const float planes[6][4],
    const float world_min[3],
    const float world_max[3])
{
    for (int p = 0; p < 6; p ++) {
        float a = planes[p][0];
        float b = planes[p][1];
        float c = planes[p][2];
        float d = planes[p][3];

        float px = (a >= 0.0f) ? world_max[0] : world_min[0];
        float py = (b >= 0.0f) ? world_max[1] : world_min[1];
        float pz = (c >= 0.0f) ? world_max[2] : world_min[2];

        if (a * px + b * py + c * pz + d < 0.0f) {
            return false;
        }
    }

    return true;
}
