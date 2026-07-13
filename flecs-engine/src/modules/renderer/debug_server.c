#include "renderer.h"
#include "render_batch.h"
#include "batches/common/common.h"
#include "flecs_engine.h"

#define FLECS_ENGINE_DEBUG_PORT 8000

static ecs_http_server_t *debug_server;
static ecs_world_t *debug_world;

/* ---- HTML helpers ---- */

static void html_head(ecs_strbuf_t *b, const char *title)
{
    ecs_strbuf_append(b,
        "<!DOCTYPE html><html><head>"
        "<meta charset='UTF-8'>"
        "<title>%s</title>"
        "<style>"
        "body{background:#0f1115;color:#e6e9ef;font-family:monospace;"
        "margin:0;padding:24px 32px;}"
        "h1{color:#7dc4ff;margin:0 0 4px;}"
        "h2{color:#b5e3a8;border-bottom:1px solid #2a2f3a;padding-bottom:6px;"
        "margin-top:28px;}"
        "a{color:#7dc4ff;}"
        "table{border-collapse:collapse;margin:10px 0;}"
        "th,td{border:1px solid #2a2f3a;padding:6px 12px;text-align:left;}"
        "th{background:#171a21;color:#b5e3a8;}"
        "td.num{text-align:right;}"
        ".muted{color:#8b93a7;}"
        "nav{margin-bottom:20px;}"
        "nav a{margin-right:16px;}"
        ".bc7{color:#ffc86b;} .rgba{color:#7dc4ff;}"
        /* details / tree styles */
        "details{margin:4px 0 4px 16px;}"
        "summary{padding:4px 0;cursor:pointer;}"
        "summary b{color:#7dc4ff;}"
        "summary .info{color:#b5e3a8;}"
        "summary .sub{color:#8b93a7;}"
        "details.empty>summary{opacity:0.4;}"
        "details.leaf>summary::marker{color:transparent;}"
        "details.leaf>summary::-webkit-details-marker{color:transparent;}"
        ".query{margin:4px 0 8px 0;color:#8b93a7;font-size:0.9em;}"
        ".query code{background:#171a21;padding:2px 6px;"
        "border:1px solid #2a2f3a;border-radius:3px;}"
        "th.sortable{cursor:pointer;user-select:none;}"
        "th.sortable:hover{color:#7dc4ff;}"
        "</style></head><body>", title);

    ecs_strbuf_appendlit(b,
        "<nav>"
        "<a href='/'>Bindings</a>"
        "<a href='/textures'>Texture Arrays</a>"
        "<a href='/views'>Views</a>"
        "</nav>");
}

static void html_tail(ecs_strbuf_t *b)
{
    ecs_strbuf_appendlit(b,
        "<script>"
        "function sortTable(id,col){"
        "var t=document.getElementById(id),"
        "rows=Array.from(t.rows).slice(1),"
        "asc=t.dataset['s'+col]!='1';"
        "t.dataset['s'+col]=asc?'1':'0';"
        "rows.sort(function(a,b){"
        "var av=a.cells[col].textContent.trim(),"
        "bv=b.cells[col].textContent.trim(),"
        "an=parseFloat(av),bn=parseFloat(bv);"
        "if(!isNaN(an)&&!isNaN(bn))return asc?an-bn:bn-an;"
        "return asc?av.localeCompare(bv):bv.localeCompare(av);"
        "});"
        "var tb=t.tBodies[0];"
        "rows.forEach(function(r){tb.appendChild(r);});"
        "}"
        "</script>"
        "</body></html>");
}

/* ---- /  (Bindings page) ---- */

static void page_bindings(ecs_strbuf_t *b, const FlecsEngineImpl *impl)
{
    html_head(b, "Bindings");
    ecs_strbuf_appendlit(b, "<h1>Bind Group Layout</h1>");

    /* Group 0 — scene globals */
    ecs_strbuf_appendlit(b, "<h2>Group 0 — Scene Globals</h2>");
    ecs_strbuf_appendlit(b,
        "<table><tr><th>Binding</th><th>Type</th><th>Description</th></tr>");

    const char *g0[][3] = {
        {"0",  "uniform",          "Frame uniforms (FlecsGpuUniforms)"},
        {"1",  "texture_cube",     "IBL prefiltered env cubemap"},
        {"2",  "sampler",          "IBL sampler"},
        {"4",  "texture_cube",     "IBL irradiance cubemap"},
        {"5",  "texture_depth_2d_array", "Shadow depth (CSM)"},
        {"6",  "sampler_comparison","Shadow sampler"},
        {"7",  "uniform",          "Cluster info"},
        {"8",  "storage (ro)",     "Cluster grid"},
        {"9",  "storage (ro)",     "Light indices"},
        {"10", "storage (ro)",     "Lights (point + spot)"},
        {"11", "storage (ro)",     "Materials (FlecsGpuMaterial)"},
    };
    for (int i = 0; i < 11; i++) {
        ecs_strbuf_append(b,
            "<tr><td class='num'>%s</td><td>%s</td><td>%s</td></tr>",
            g0[i][0], g0[i][1], g0[i][2]);
    }
    ecs_strbuf_appendlit(b, "</table>");

    /* Group 1 — PBR texture arrays */
    ecs_strbuf_appendlit(b, "<h2>Group 1 — PBR Texture Arrays</h2>");
    ecs_strbuf_appendlit(b,
        "<table><tr><th>Binding</th><th>Channel</th><th>Bucket</th>"
        "<th>Dimensions</th><th>Format</th><th>Layers</th>"
        "<th>Mips</th><th>Status</th></tr>");

    static const char *channel_names[4] = {
        "albedo", "emissive", "mr", "normal"
    };

    for (int ch = 0; ch < 4; ch++) {
        for (int b_idx = 0; b_idx < FLECS_ENGINE_TEXTURE_BUCKET_COUNT; b_idx++) {
            int binding = ch * FLECS_ENGINE_TEXTURE_BUCKET_COUNT + b_idx;
            const flecsEngine_texture_bucket_t *bk =
                &impl->textures.buckets[b_idx];

            const char *fmt_class = bk->is_bc7 ? "bc7" : "rgba";
            const char *fmt_name = bk->is_bc7 ? "BC7" : "RGBA8";
            uint32_t lc = bk->layer_counts[ch];
            bool has_view = bk->texture_array_views[ch] != NULL;

            ecs_strbuf_append(b,
                "<tr><td class='num'>%d</td>"
                "<td>%s</td>"
                "<td class='num'>%d</td>",
                binding, channel_names[ch], b_idx);

            if (bk->width && lc) {
                ecs_strbuf_append(b,
                    "<td class='num'>%ux%u</td>"
                    "<td class='%s'>%s</td>"
                    "<td class='num'>%u</td>"
                    "<td class='num'>%u</td>"
                    "<td>%s</td>",
                    bk->width, bk->height,
                    fmt_class, fmt_name,
                    lc, bk->mip_count,
                    has_view ? "bound" : "fallback");
            } else {
                ecs_strbuf_appendlit(b,
                    "<td class='muted'>-</td>"
                    "<td class='muted'>-</td>"
                    "<td class='muted'>-</td>"
                    "<td class='muted'>-</td>"
                    "<td class='muted'>fallback (1x1)</td>");
            }
            ecs_strbuf_appendlit(b, "</tr>");
        }
    }

    ecs_strbuf_appendlit(b, "</table>");

    ecs_strbuf_appendlit(b, "<h2>Group 1 — Samplers</h2>");
    ecs_strbuf_append(b,
        "<p>Binding 12: aniso sampler (albedo + normal, %ux aniso, repeat) — %s</p>",
        (unsigned)impl->textures.applied_max_aniso,
        impl->textures.pbr_sampler ? "created" : "not created");
    ecs_strbuf_append(b,
        "<p>Binding 13: trilinear sampler (emissive + metal-rough, 1x aniso, repeat) — %s</p>",
        impl->textures.pbr_low_sampler ? "created" : "not created");

    html_tail(b);
}

/* ---- /textures  (Texture Arrays page) ---- */

static void page_textures(
    ecs_strbuf_t *b,
    const FlecsEngineImpl *impl,
    ecs_world_t *world)
{
    html_head(b, "Texture Arrays");
    ecs_strbuf_appendlit(b, "<h1>Texture Array State</h1>");

    ecs_strbuf_append(b,
        "<p>Material buffer: %u materials, capacity %u</p>",
        impl->materials.count, impl->materials.buffer_capacity);
    ecs_strbuf_append(b,
        "<p>Bind group: %s</p>",
        impl->textures.fallback_bind_group ? "built" : "not built");

    static const char *channel_names[4] = {
        "albedo", "emissive", "mr", "normal"
    };

    for (int b_idx = 0; b_idx < FLECS_ENGINE_TEXTURE_BUCKET_COUNT; b_idx++) {
        const flecsEngine_texture_bucket_t *bk =
            &impl->textures.buckets[b_idx];

        uint32_t total_layers = 0;
        for (int ch = 0; ch < 4; ch++) {
            total_layers += bk->layer_counts[ch];
        }

        ecs_strbuf_append(b, "<h2>Bucket %d", b_idx);
        if (bk->width) {
            const char *fmt_class = bk->is_bc7 ? "bc7" : "rgba";
            const char *fmt_name = bk->is_bc7 ? "BC7" : "RGBA8";
            ecs_strbuf_append(b,
                " — %ux%u <span class='%s'>%s</span>",
                bk->width, bk->height, fmt_class, fmt_name);
        }
        ecs_strbuf_appendlit(b, "</h2>");

        if (!bk->width) {
            ecs_strbuf_appendlit(b, "<p class='muted'>Not allocated</p>");
            continue;
        }

        ecs_strbuf_append(b,
            "<p>Mip levels: %u, Total layers across channels: %u</p>",
            bk->mip_count, total_layers);

        for (int ch = 0; ch < 4; ch++) {
            uint32_t lc = bk->layer_counts[ch];

            float mem_mb = 0;
            if (lc && bk->texture_arrays[ch]) {
                float bpp = bk->is_bc7 ? 1.0f : 4.0f;
                mem_mb = (float)bk->width * (float)bk->height
                    * bpp * 1.333f * (float)lc / (1024.0f * 1024.0f);
            }

            bool ch_empty = !lc;
            const char *ch_cls = ch_empty
                ? "channel empty leaf" : "channel";
            ecs_strbuf_append(b, "<details class='%s'><summary>", ch_cls);
            if (ch_empty) {
                ecs_strbuf_append(b,
                    "<b>%s</b> <span class='sub'>(empty)</span>",
                    channel_names[ch]);
            } else {
                ecs_strbuf_append(b,
                    "<b>%s</b> <span class='sub'>(%u layers, %.1f MiB)</span>",
                    channel_names[ch], lc, (double)mem_mb);
            }
            ecs_strbuf_appendlit(b, "</summary>");

            if (lc && impl->textures.query) {
                ecs_strbuf_appendlit(b,
                    "<table><tr><th>Slot</th><th>Path</th>"
                    "<th>Source</th><th>Actual</th></tr>");

                ecs_iter_t it = ecs_query_iter(
                    world, impl->textures.query);
                while (ecs_query_next(&it)) {
                    const FlecsPbrTextures *textures =
                        ecs_field(&it, FlecsPbrTextures, 0);
                    const FlecsMaterialId *mat_ids =
                        ecs_field(&it, FlecsMaterialId, 1);

                    for (int32_t i = 0; i < it.count; i++) {
                        uint32_t mat_id = mat_ids[i].value;
                        if (mat_id >= impl->materials.count) continue;

                        const FlecsGpuMaterial *gm =
                            &impl->materials.cpu_materials[mat_id];
                        if (impl->materials.cpu_buckets[mat_id] !=
                            (int8_t)b_idx) continue;

                        ecs_entity_t tex_entities[4] = {
                            textures[i].albedo,
                            textures[i].emissive,
                            textures[i].roughness,
                            textures[i].normal
                        };
                        uint32_t slots[4] = {
                            gm->layer_albedo,
                            gm->layer_emissive,
                            gm->layer_mr,
                            gm->layer_normal
                        };

                        ecs_entity_t tex_e = tex_entities[ch];
                        uint32_t slot = slots[ch];
                        if (!tex_e || slot == 0) continue;

                        const FlecsTexture *tex =
                            ecs_get(world, tex_e, FlecsTexture);
                        const FlecsTextureInfo *info =
                            ecs_get(world, tex_e, FlecsTextureInfo);

                        const char *path = tex && tex->path
                            ? tex->path : "?";

                        ecs_strbuf_append(b,
                            "<tr><td class='num'>%u</td>"
                            "<td>%s</td>", slot, path);

                        if (info && info->source.format) {
                            ecs_strbuf_append(b,
                                "<td>%ux%u %s (%u mips)</td>",
                                info->source.width,
                                info->source.height,
                                info->source.format,
                                info->source.mip_count);
                        } else {
                            ecs_strbuf_appendlit(b,
                                "<td class='muted'>?</td>");
                        }

                        const char *a_fmt = bk->is_bc7 ? "BC7" : "RGBA8";
                        ecs_strbuf_append(b,
                            "<td>%ux%u %s (%u mips)</td>",
                            bk->width, bk->height, a_fmt, bk->mip_count);

                        ecs_strbuf_appendlit(b, "</tr>");
                    }
                }

                ecs_strbuf_appendlit(b, "</table>");
            }

            ecs_strbuf_appendlit(b, "</details>");
        }
    }

    /* Total memory estimate */
    ecs_strbuf_appendlit(b, "<h2>Total Memory Estimate</h2>");
    float total_mib = 0;
    for (int b_idx = 0; b_idx < FLECS_ENGINE_TEXTURE_BUCKET_COUNT; b_idx++) {
        const flecsEngine_texture_bucket_t *bk =
            &impl->textures.buckets[b_idx];
        if (!bk->width) continue;
        float bpp = bk->is_bc7 ? 1.0f : 4.0f;
        for (int ch = 0; ch < 4; ch++) {
            if (!bk->layer_counts[ch]) continue;
            total_mib += (float)bk->width * (float)bk->height
                * bpp * 1.333f * (float)bk->layer_counts[ch]
                / (1024.0f * 1024.0f);
        }
    }
    ecs_strbuf_append(b,
        "<p>Material texture arrays: <b>%.1f MiB</b> (%.2f GiB)</p>",
        (double)total_mib, (double)(total_mib / 1024.0f));

    ecs_strbuf_appendlit(b, "<h2>Source Memory (hypothetical)</h2>");
    if (impl->textures.query) {
        uint32_t seen_cap = (impl->materials.count + 1) * 4;
        ecs_entity_t *seen = ecs_os_calloc_n(ecs_entity_t, seen_cap);
        uint32_t seen_count = 0;
        float source_mib = 0;

        ecs_iter_t it = ecs_query_iter(world, impl->textures.query);
        while (ecs_query_next(&it)) {
            const FlecsPbrTextures *textures =
                ecs_field(&it, FlecsPbrTextures, 0);

            for (int32_t i = 0; i < it.count; i++) {
                ecs_entity_t tex_ents[4] = {
                    textures[i].albedo,
                    textures[i].emissive,
                    textures[i].roughness,
                    textures[i].normal
                };

                for (int ch = 0; ch < 4; ch++) {
                    ecs_entity_t e = tex_ents[ch];
                    if (!e) continue;

                    /* Deduplicate */
                    bool already = false;
                    for (uint32_t s = 0; s < seen_count; s++) {
                        if (seen[s] == e) { already = true; break; }
                    }
                    if (already) continue;
                    if (seen_count < seen_cap) {
                        seen[seen_count++] = e;
                    }

                    const FlecsTextureInfo *info =
                        ecs_get(world, e, FlecsTextureInfo);
                    if (!info || !info->source.width) continue;

                    float bpp;
                    const char *fmt = info->source.format;
                    if (fmt && (
                        !ecs_os_strcmp(fmt, "BC7RGBAUnorm") ||
                        !ecs_os_strcmp(fmt, "BC7RGBAUnormSrgb") ||
                        !ecs_os_strcmp(fmt, "BC3RGBAUnorm") ||
                        !ecs_os_strcmp(fmt, "BC3RGBAUnormSrgb")))
                    {
                        bpp = 1.0f;
                    } else if (fmt && (
                        !ecs_os_strcmp(fmt, "BC1RGBAUnorm") ||
                        !ecs_os_strcmp(fmt, "BC1RGBAUnormSrgb")))
                    {
                        bpp = 0.5f;
                    } else {
                        bpp = 4.0f; /* RGBA8 */
                    }

                    float mip_factor = info->source.mip_count > 1
                        ? 1.333f : 1.0f;
                    source_mib += (float)info->source.width
                        * (float)info->source.height
                        * bpp * mip_factor / (1024.0f * 1024.0f);
                }
            }
        }

        ecs_os_free(seen);

        ecs_strbuf_append(b,
            "<p>If stored at source format/dimensions: "
            "<b>%.1f MiB</b> (%.2f GiB)</p>",
            (double)source_mib, (double)(source_mib / 1024.0f));
        ecs_strbuf_append(b,
            "<p>Overhead from normalization: <b>%.1f MiB</b> "
            "(%.1fx of source)</p>",
            (double)(total_mib - source_mib),
            total_mib > 0 ? (double)(total_mib / source_mib) : 0.0);
    }

    html_tail(b);
}

/* ---- /views  (Views page) ---- */

/* Build a path from doc names by walking up the entity hierarchy. */
static char* flecsEngine_doc_path(
    ecs_world_t *world,
    ecs_entity_t entity)
{
    /* Collect ancestors (entity first, root last) */
    ecs_entity_t chain[64];
    int32_t depth = 0;
    for (ecs_entity_t e = entity; e && depth < 64; depth++) {
        chain[depth] = e;
        e = ecs_get_parent(world, e);
    }

    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    for (int32_t i = depth - 1; i >= 0; i--) {
        const char *name = ecs_doc_get_name(world, chain[i]);
        if (!name) name = ecs_get_name(world, chain[i]);
        if (!name) name = "?";
        if (i < depth - 1) {
            ecs_strbuf_appendlit(&buf, "/");
        }
        ecs_strbuf_appendstr(&buf, name);
    }

    return ecs_strbuf_get(&buf);
}

static int page_views_sortTableId;

static void page_views_batchSet(
    ecs_strbuf_t *b,
    ecs_world_t *world,
    const FlecsEngineImpl *impl,
    const FlecsRenderBatchSet *batch_set,
    int depth);

static void page_views_batch(
    ecs_strbuf_t *b,
    ecs_world_t *world,
    const FlecsEngineImpl *impl,
    ecs_entity_t batch_entity,
    int depth)
{
    const FlecsRenderBatch *batch = ecs_get(
        world, batch_entity, FlecsRenderBatch);
    if (!batch) return;

    const char *batch_name = ecs_get_name(world, batch_entity);
    if (!batch_name) batch_name = "(unnamed)";

    /* Count groups and total instances */
    int32_t group_count = 0;
    int32_t total_instances = 0;
    if (batch->query) {
        const ecs_map_t *groups = ecs_query_get_groups(batch->query);
        if (groups) {
            ecs_map_iter_t git = ecs_map_iter(groups);
            while (ecs_map_next(&git)) {
                uint64_t group_id = ecs_map_key(&git);
                if (!group_id) continue;
                group_count++;
                flecsEngine_batch_group_t *ctx =
                    ecs_query_get_group_ctx(batch->query, group_id);
                if (ctx) {
                    total_instances += ctx->view.count;
                }
            }
        }
    }

    bool empty = (total_instances == 0);
    bool leaf = (group_count == 0);
    const char *cls;
    if (empty && leaf) cls = "batch empty leaf";
    else if (empty) cls = "batch empty";
    else if (leaf) cls = "batch leaf";
    else cls = "batch";

    ecs_strbuf_append(b, "<details class='%s'>", cls);
    ecs_strbuf_append(b,
        "<summary><b>%s</b> <span class='sub'>(%d groups, %d instances)</span></summary>",
        batch_name, group_count, total_instances);

    /* Group detail table */
    if (group_count > 0 && batch->query) {
        int tid = page_views_sortTableId++;
        ecs_strbuf_append(b,
            "<table id='t%d'><tr>"
            "<th onclick='sortTable(\"t%d\",0)' class='sortable'>Instances</th>"
            "<th onclick='sortTable(\"t%d\",1)' class='sortable'>Vertices</th>"
            "<th onclick='sortTable(\"t%d\",2)' class='sortable'>Indices</th>"
            "<th onclick='sortTable(\"t%d\",3)' class='sortable'>Mesh</th>"
            "</tr>",
            tid, tid, tid, tid, tid);

        const ecs_map_t *groups = ecs_query_get_groups(batch->query);
        ecs_map_iter_t git = ecs_map_iter(groups);
        while (ecs_map_next(&git)) {
            uint64_t group_id = ecs_map_key(&git);
            if (!group_id) continue;

            flecsEngine_batch_group_t *ctx =
                ecs_query_get_group_ctx(batch->query, group_id);
            if (!ctx) continue;

            char *mesh_label = flecsEngine_doc_path(
                world, (ecs_entity_t)group_id);

            ecs_strbuf_append(b,
                "<tr><td class='num'>%d</td>"
                "<td class='num'>%d</td>"
                "<td class='num'>%d</td>"
                "<td>%s</td></tr>",
                ctx->view.count,
                ctx->mesh.vertex_count,
                ctx->mesh.index_count,
                mesh_label ? mesh_label : "(unnamed)");

            ecs_os_free(mesh_label);
        }

        ecs_strbuf_appendlit(b, "</table>");
    }

    /* Query string (below the table) */
    if (batch->query) {
        char *query_str = ecs_query_str(batch->query);
        if (query_str) {
            ecs_strbuf_append(b,
                "<p class='query'>query: <code>%s</code></p>",
                query_str);
            ecs_os_free(query_str);
        }
    }

    ecs_strbuf_appendlit(b, "</details>");
}

static void page_views_batchSet(
    ecs_strbuf_t *b,
    ecs_world_t *world,
    const FlecsEngineImpl *impl,
    const FlecsRenderBatchSet *batch_set,
    int depth)
{
    int32_t count = ecs_vec_count(&batch_set->batches);
    ecs_entity_t *batches = ecs_vec_first(&batch_set->batches);

    for (int32_t i = 0; i < count; i++) {
        ecs_entity_t e = batches[i];
        if (!e) continue;

        const FlecsRenderBatchSet *nested = ecs_get(
            world, e, FlecsRenderBatchSet);
        if (nested) {
            const char *set_name = ecs_get_name(world, e);
            if (!set_name) set_name = "(unnamed set)";

            /* Check if any batch in the set has instances */
            bool set_empty = true;
            int32_t entry_count = (int)ecs_vec_count(&nested->batches);
            ecs_entity_t *entries = ecs_vec_first(&nested->batches);
            for (int32_t j = 0; j < entry_count && set_empty; j++) {
                if (!entries[j]) continue;
                const FlecsRenderBatch *rb = ecs_get(
                    world, entries[j], FlecsRenderBatch);
                if (!rb || !rb->query) continue;
                const ecs_map_t *grps = ecs_query_get_groups(rb->query);
                if (!grps) continue;
                ecs_map_iter_t gi = ecs_map_iter(grps);
                while (ecs_map_next(&gi)) {
                    uint64_t gid = ecs_map_key(&gi);
                    if (!gid) continue;
                    flecsEngine_batch_group_t *c =
                        ecs_query_get_group_ctx(rb->query, gid);
                    if (c && c->view.count > 0) { set_empty = false; break; }
                }
            }

            const char *cls = set_empty ? "batchset empty" : "batchset";
            ecs_strbuf_append(b, "<details class='%s' open>", cls);
            ecs_strbuf_append(b,
                "<summary><b>%s</b> <span class='info'>(%d entries)</span></summary>",
                set_name, entry_count);
            page_views_batchSet(b, world, impl, nested, depth + 1);
            ecs_strbuf_appendlit(b, "</details>");
        } else {
            page_views_batch(b, world, impl, e, depth);
        }
    }
}

static void page_views(
    ecs_strbuf_t *b,
    ecs_world_t *world,
    const FlecsEngineImpl *impl)
{
    html_head(b, "Views");
    ecs_strbuf_appendlit(b, "<h1>Render Views</h1>");

    page_views_sortTableId = 0;

    if (!impl->view_query) {
        ecs_strbuf_appendlit(b, "<p class='muted'>No view query</p>");
        html_tail(b);
        return;
    }

    ecs_iter_t it = ecs_query_iter(world, impl->view_query);
    while (ecs_query_next(&it)) {
        const FlecsRenderView *views =
            ecs_field(&it, FlecsRenderView, 0);
        const FlecsRenderViewImpl *view_impls =
            ecs_field(&it, FlecsRenderViewImpl, 1);

        for (int32_t i = 0; i < it.count; i++) {
            ecs_entity_t view_entity = it.entities[i];
            const char *view_name = ecs_get_name(world, view_entity);
            if (!view_name) view_name = "(unnamed)";

            /* Camera name */
            const char *camera_name = NULL;
            if (views[i].camera) {
                camera_name = ecs_get_name(world, views[i].camera);
            }
            if (!camera_name) camera_name = "(none)";

            /* Light name */
            const char *light_name = NULL;
            if (view_impls[i].main_light) {
                light_name = ecs_get_name(world, view_impls[i].main_light);
            }
            if (!light_name) light_name = "(none)";

            /* Effect count */
            int32_t effect_count = ecs_vec_count(&views[i].effects);

            ecs_strbuf_append(b,
                "<h2>%s</h2>"
                "<p>Camera: %s, Light: %s, Effects: %d</p>",
                view_name, camera_name, light_name, effect_count);

            const FlecsRenderBatchSet *batch_set = ecs_get(
                world, view_entity, FlecsRenderBatchSet);
            if (batch_set) {
                page_views_batchSet(b, world, impl, batch_set, 0);
            } else {
                ecs_strbuf_appendlit(b,
                    "<p class='muted'>No batch set</p>");
            }
        }
    }

    html_tail(b);
}

/* ---- Request handler ---- */

static bool flecsEngine_debug_onRequest(
    const ecs_http_request_t *request,
    ecs_http_reply_t *reply,
    void *ctx)
{
    (void)ctx;

    if (request->method != EcsHttpGet) {
        return false;
    }

    if (!debug_world) {
        return false;
    }

    const FlecsEngineImpl *impl =
        ecs_singleton_get(debug_world, FlecsEngineImpl);
    if (!impl) {
        return false;
    }

    reply->content_type = "text/html";

    if (request->path[0] == '\0' ||
        !ecs_os_strcmp(request->path, "bindings"))
    {
        page_bindings(&reply->body, impl);
        return true;
    }

    if (!ecs_os_strcmp(request->path, "textures")) {
        page_textures(&reply->body, impl, debug_world);
        return true;
    }

    if (!ecs_os_strcmp(request->path, "views")) {
        page_views(&reply->body, debug_world, impl);
        return true;
    }

    return false;
}

/* ---- Public API ---- */

void flecsEngine_debugServer_init(ecs_world_t *world)
{
    debug_world = world;

    debug_server = ecs_http_server_init(&(ecs_http_server_desc_t){
        .callback = flecsEngine_debug_onRequest,
        .port = FLECS_ENGINE_DEBUG_PORT
    });

    if (debug_server) {
        ecs_http_server_start(debug_server);
        ecs_trace("debug server started on port %d", FLECS_ENGINE_DEBUG_PORT);
    }
}

void flecsEngine_debugServer_fini(void)
{
    if (debug_server) {
        ecs_http_server_stop(debug_server);
        ecs_http_server_fini(debug_server);
        debug_server = NULL;
    }
    debug_world = NULL;
}

void flecsEngine_debugServer_dequeue(float delta_time)
{
    if (debug_server) {
        ecs_http_server_dequeue(debug_server, delta_time);
    }
}
