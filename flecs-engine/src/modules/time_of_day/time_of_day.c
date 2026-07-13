#define FLECS_ENGINE_TIME_OF_DAY_IMPL
#include "time_of_day.h"

#include <math.h>
#include "../../tracy_hooks.h"

static float flecsEngine_smoothstep01(float t) {
    if (t < 0.0f) return 0.0f;
    if (t > 1.0f) return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

static float flecsEngine_sunTransmittance(float altitude_rad) {
    if (altitude_rad <= 0.0f) {
        return 0.0f;
    }

    float altitude_deg = altitude_rad * (180.0f / GLM_PIf);
    float s = sinf(altitude_rad);
    float ky = 0.50572f *
        powf(fmaxf(altitude_deg + 6.07995f, 0.01f), -1.6364f);
    float air_mass = 1.0f / fmaxf(s + ky, 1e-4f);

    float trans = expf(-0.1f * (air_mass - 1.0f));
    if (trans > 1.0f) trans = 1.0f;
    if (trans < 0.0f) trans = 0.0f;
    return trans;
}

static void flecsEngine_sunColor(float altitude_rad, float *r, float *g, float *b) {
    float altitude_deg = altitude_rad * (180.0f / GLM_PIf);
    float t = flecsEngine_smoothstep01((altitude_deg - 2.0f) / 18.0f);

    const float low[3]  = { 1.00f, 0.55f, 0.28f };
    const float high[3] = { 1.00f, 0.97f, 0.92f };

    *r = low[0] * (1.0f - t) + high[0] * t;
    *g = low[1] * (1.0f - t) + high[1] * t;
    *b = low[2] * (1.0f - t) + high[2] * t;
}

static float flecsEngine_wrap24(float h) {
    while (h >= 24.0f) h -= 24.0f;
    while (h < 0.0f) h += 24.0f;
    return h;
}

static float flecsEngine_normDeg(float d) {
    d = fmodf(d, 360.0f);
    if (d < 0.0f) d += 360.0f;
    return d;
}

static void flecsEngine_moonPosition(
    float hour,
    float day_of_year,
    float latitude_deg,
    float position_offset_hours,
    float *out_altitude,
    float *out_azimuth,
    float *out_illum_fraction)
{
    float moon_hour = hour + position_offset_hours;
    float d = (day_of_year - 1.0f) + (moon_hour - 12.0f) / 24.0f;

    float N = glm_rad(flecsEngine_normDeg(125.1228f - 0.0529538083f * d));
    float inc = glm_rad(5.1454f);
    float w = glm_rad(flecsEngine_normDeg(318.0634f + 0.1643573223f * d));
    float a = 60.2666f;
    float e = 0.054900f;
    float M = glm_rad(flecsEngine_normDeg(115.3654f + 13.0649929509f * d));

    float E = M + e * sinf(M) * (1.0f + e * cosf(M));
    E = E - (E - e * sinf(E) - M) / (1.0f - e * cosf(E));

    float xv = a * (cosf(E) - e);
    float yv = a * sqrtf(1.0f - e * e) * sinf(E);
    float v = atan2f(yv, xv);
    float r = sqrtf(xv * xv + yv * yv);

    float cos_N = cosf(N), sin_N = sinf(N);
    float cos_vw = cosf(v + w), sin_vw = sinf(v + w);
    float cos_i = cosf(inc), sin_i = sinf(inc);
    float xh = r * (cos_N * cos_vw - sin_N * sin_vw * cos_i);
    float yh = r * (sin_N * cos_vw + cos_N * sin_vw * cos_i);
    float zh = r * (sin_vw * sin_i);

    float moon_ecl_lon = atan2f(yh, xh);

    float ecl = glm_rad(23.4393f - 3.563e-7f * d);
    float cos_ecl = cosf(ecl), sin_ecl = sinf(ecl);
    float xe = xh;
    float ye = yh * cos_ecl - zh * sin_ecl;
    float ze = yh * sin_ecl + zh * cos_ecl;
    float moon_ra = atan2f(ye, xe);
    float moon_dec = atan2f(ze, sqrtf(xe * xe + ye * ye));

    float M_sun = glm_rad(flecsEngine_normDeg(356.0470f + 0.9856002585f * d));
    float w_sun = glm_rad(flecsEngine_normDeg(282.9404f + 4.70935e-5f * d));
    float e_sun = 0.016709f - 1.151e-9f * d;
    float E_sun = M_sun + e_sun * sinf(M_sun) * (1.0f + e_sun * cosf(M_sun));
    float xv_sun = cosf(E_sun) - e_sun;
    float yv_sun = sqrtf(fmaxf(0.0f, 1.0f - e_sun * e_sun)) * sinf(E_sun);
    float v_sun = atan2f(yv_sun, xv_sun);
    float sun_ecl_lon = v_sun + w_sun;

    float sun_ra = atan2f(sinf(sun_ecl_lon) * cos_ecl, cosf(sun_ecl_lon));

    float sun_ha_rad = glm_rad(15.0f * (moon_hour - 12.0f));
    float lst = sun_ra + sun_ha_rad;
    float moon_ha = lst - moon_ra;

    float lat_rad = glm_rad(latitude_deg);
    float sin_lat = sinf(lat_rad), cos_lat = cosf(lat_rad);
    float sin_dec = sinf(moon_dec), cos_dec = cosf(moon_dec);
    float cos_ha = cosf(moon_ha), sin_ha = sinf(moon_ha);

    float sin_alt = sin_lat * sin_dec + cos_lat * cos_dec * cos_ha;
    if (sin_alt > 1.0f) sin_alt = 1.0f;
    if (sin_alt < -1.0f) sin_alt = -1.0f;
    float altitude = asinf(sin_alt);
    float cos_alt = cosf(altitude);

    float azimuth = 0.0f;
    if (cos_alt > 1e-4f && cos_lat > 1e-4f) {
        float sin_az = -cos_dec * sin_ha / cos_alt;
        float cos_az = (sin_dec - sin_lat * sin_alt) / (cos_lat * cos_alt);
        azimuth = atan2f(sin_az, cos_az);
    }

    float elong = moon_ecl_lon - sun_ecl_lon;
    float illum = (1.0f - cosf(elong)) * 0.5f;
    if (illum < 0.0f) illum = 0.0f;
    if (illum > 1.0f) illum = 1.0f;

    *out_altitude = altitude;
    *out_azimuth = azimuth;
    *out_illum_fraction = illum;
}

static void flecsEngine_solarPosition(
    float hour,
    float day_of_year,
    float latitude_deg,
    float *out_altitude,
    float *out_azimuth)
{
    float day = day_of_year < 1.0f ? 1.0f : day_of_year;

    float decl_rad = glm_rad(23.44f) *
        sinf((GLM_PIf * 2.0f / 365.0f) * (day - 81.0f));
    float hour_rad = glm_rad(15.0f * (hour - 12.0f));
    float lat_rad = glm_rad(latitude_deg);

    float sin_lat = sinf(lat_rad);
    float cos_lat = cosf(lat_rad);
    float sin_decl = sinf(decl_rad);
    float cos_decl = cosf(decl_rad);

    float sin_alt = sin_lat * sin_decl + cos_lat * cos_decl * cosf(hour_rad);
    if (sin_alt > 1.0f) sin_alt = 1.0f;
    if (sin_alt < -1.0f) sin_alt = -1.0f;
    float altitude = asinf(sin_alt);
    float cos_alt = cosf(altitude);

    float azimuth;
    if (cos_alt > 1e-4f && cos_lat > 1e-4f) {
        float sin_az = -cos_decl * sinf(hour_rad) / cos_alt;
        float cos_az = (sin_decl - sin_lat * sin_alt) / (cos_lat * cos_alt);
        azimuth = atan2f(sin_az, cos_az);
    } else {
        azimuth = 0.0f;
    }

    *out_altitude = altitude;
    *out_azimuth = azimuth;
}

static void FlecsAdvanceTimeOfDay(ecs_iter_t *it) {
    FlecsTimeOfDay *tod = ecs_field(it, FlecsTimeOfDay, 0);
    float dt = it->delta_time;
    ecs_world_t *world = it->world;

    for (int32_t i = 0; i < it->count; i ++) {
        FlecsTimeOfDay *t = &tod[i];

        if (t->time_scale != 0.0f) {
            t->hour = flecsEngine_wrap24(t->hour + t->time_scale * dt);
        }

        float altitude, azimuth;
        flecsEngine_solarPosition(
            t->hour, t->day_of_year, t->latitude, &altitude, &azimuth);

        float north_rad = glm_rad(t->north_offset);
        float pitch = -altitude;
        float yaw = azimuth + north_rad + GLM_PIf;

        const float sun_disk_radius_rad = 0.00465f;
        float sun_disk_fade = flecsEngine_smoothstep01(
            (altitude + sun_disk_radius_rad) / (2.0f * sun_disk_radius_rad));
        float altitude_for_trans = altitude > sun_disk_radius_rad
            ? altitude : sun_disk_radius_rad;

        float atm_trans_rgb[3] = { 0.0f, 0.0f, 0.0f };
        const FlecsAtmosphere *atm_settings = NULL;

        if (t->atmosphere) {
            atm_settings = ecs_get(
                world, t->atmosphere, FlecsAtmosphere);
            if (atm_settings) {
                flecsEngine_atmos_sunTransmittance(
                    atm_settings, sinf(altitude_for_trans), atm_trans_rgb);
            }
        }

        if (t->light) {
            FlecsRotation3 *rot = ecs_get_mut(
                world, t->light, FlecsRotation3);
            if (rot) {
                rot->x = pitch;
                rot->y = yaw;
                rot->z = 0.0f;
                ecs_modified(world, t->light, FlecsRotation3);
            }

            const FlecsCelestialLight *cl = ecs_get(
                world, t->light, FlecsCelestialLight);
            float toa = cl ? cl->toa_intensity : 0.0f;
            float toa_r = cl ? flecsEngine_colorChannelToFloat(cl->toa_color.r) : 1.0f;
            float toa_g = cl ? flecsEngine_colorChannelToFloat(cl->toa_color.g) : 1.0f;
            float toa_b = cl ? flecsEngine_colorChannelToFloat(cl->toa_color.b) : 1.0f;

            float light_intensity;
            float light_r, light_g, light_b;
            if (atm_settings) {
                float m = atm_trans_rgb[0];
                if (atm_trans_rgb[1] > m) m = atm_trans_rgb[1];
                if (atm_trans_rgb[2] > m) m = atm_trans_rgb[2];
                light_intensity = toa * m * sun_disk_fade;
                if (m > 1e-6f) {
                    light_r = toa_r * atm_trans_rgb[0] / m;
                    light_g = toa_g * atm_trans_rgb[1] / m;
                    light_b = toa_b * atm_trans_rgb[2] / m;
                } else {
                    light_r = light_g = light_b = 0.0f;
                }
            } else {
                float transmittance = flecsEngine_sunTransmittance(
                    altitude_for_trans);
                light_intensity = toa * transmittance * sun_disk_fade;
                float sr, sg, sb;
                flecsEngine_sunColor(altitude_for_trans, &sr, &sg, &sb);
                light_r = toa_r * sr;
                light_g = toa_g * sg;
                light_b = toa_b * sb;
            }

            FlecsDirectionalLight *dl = ecs_get_mut(
                world, t->light, FlecsDirectionalLight);
            if (dl) {
                dl->intensity = light_intensity;
                ecs_modified(world, t->light, FlecsDirectionalLight);
            }

            FlecsRgba *rgba = ecs_get_mut(world, t->light, FlecsRgba);
            if (rgba) {
                rgba->r = (uint8_t)(glm_clamp(light_r, 0.0f, 1.0f) * 255.0f);
                rgba->g = (uint8_t)(glm_clamp(light_g, 0.0f, 1.0f) * 255.0f);
                rgba->b = (uint8_t)(glm_clamp(light_b, 0.0f, 1.0f) * 255.0f);
                rgba->a = 255;
                ecs_modified(world, t->light, FlecsRgba);
            }
        }

        if (t->moon_light) {
            float moon_alt, moon_az, illum;
            flecsEngine_moonPosition(
                t->hour, t->day_of_year, t->latitude,
                t->moon_position_offset,
                &moon_alt, &moon_az, &illum);

            float moon_pitch = -moon_alt;
            float moon_yaw = moon_az + north_rad + GLM_PIf;

            FlecsRotation3 *mrot = ecs_get_mut(
                world, t->moon_light, FlecsRotation3);
            if (mrot) {
                mrot->x = moon_pitch;
                mrot->y = moon_yaw;
                mrot->z = 0.0f;
                ecs_modified(world, t->moon_light, FlecsRotation3);
            }

            float moon_altitude_for_trans = moon_alt > sun_disk_radius_rad
                ? moon_alt : sun_disk_radius_rad;
            float moon_disk_fade = flecsEngine_smoothstep01(
                (moon_alt + sun_disk_radius_rad)
                    / (2.0f * sun_disk_radius_rad));

            float moon_trans_rgb[3] = { 1.0f, 1.0f, 1.0f };
            if (atm_settings) {
                flecsEngine_atmos_sunTransmittance(
                    atm_settings, sinf(moon_altitude_for_trans),
                    moon_trans_rgb);
            } else {
                float tr = flecsEngine_sunTransmittance(
                    moon_altitude_for_trans);
                moon_trans_rgb[0] = moon_trans_rgb[1] = moon_trans_rgb[2] = tr;
            }

            float m = moon_trans_rgb[0];
            if (moon_trans_rgb[1] > m) m = moon_trans_rgb[1];
            if (moon_trans_rgb[2] > m) m = moon_trans_rgb[2];

            const FlecsCelestialLight *mcl = ecs_get(
                world, t->moon_light, FlecsCelestialLight);
            float moon_toa = mcl ? mcl->toa_intensity : 0.0f;
            float mtoa_r = mcl ? flecsEngine_colorChannelToFloat(mcl->toa_color.r) : 1.0f;
            float mtoa_g = mcl ? flecsEngine_colorChannelToFloat(mcl->toa_color.g) : 1.0f;
            float mtoa_b = mcl ? flecsEngine_colorChannelToFloat(mcl->toa_color.b) : 1.0f;

            float moon_intensity = moon_toa * illum * m * moon_disk_fade;

            float mr = 1.0f, mg = 1.0f, mb = 1.0f;
            if (m > 1e-6f) {
                mr = mtoa_r * moon_trans_rgb[0] / m;
                mg = mtoa_g * moon_trans_rgb[1] / m;
                mb = mtoa_b * moon_trans_rgb[2] / m;
            }

            FlecsDirectionalLight *mdl = ecs_get_mut(
                world, t->moon_light, FlecsDirectionalLight);
            if (mdl) {
                mdl->intensity = moon_intensity;
                ecs_modified(world, t->moon_light, FlecsDirectionalLight);
            }

            FlecsRgba *mrgba = ecs_get_mut(
                world, t->moon_light, FlecsRgba);
            if (mrgba) {
                mrgba->r = (uint8_t)(glm_clamp(mr, 0.0f, 1.0f) * 255.0f);
                mrgba->g = (uint8_t)(glm_clamp(mg, 0.0f, 1.0f) * 255.0f);
                mrgba->b = (uint8_t)(glm_clamp(mb, 0.0f, 1.0f) * 255.0f);
                mrgba->a = 255;
                ecs_modified(world, t->moon_light, FlecsRgba);
            }
        }

        if (t->stars) {
            const float sidereal_rate = 2.0f * GLM_PIf / 23.9344696f;

            FlecsRotation3 *srot = ecs_get_mut(
                world, t->stars, FlecsRotation3);
            if (srot) {
                srot->x = glm_rad(t->latitude);
                srot->y = -sidereal_rate * t->hour;
                srot->z = north_rad;
                ecs_modified(world, t->stars, FlecsRotation3);
            }
        }
    }
}

void FlecsEngineTime_of_dayImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, FlecsEngineTime_of_day);

    ecs_set_name_prefix(world, "Flecs");

    ECS_META_COMPONENT(world, FlecsTimeOfDay);
    ECS_SYSTEM(world, FlecsAdvanceTimeOfDay, EcsPreStore, [inout] TimeOfDay);
}
