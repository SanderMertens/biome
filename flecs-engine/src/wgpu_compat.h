/* Compatibility header bridging wgpu-native v27 API and emscripten's WebGPU. */
#ifndef FLECS_ENGINE_WGPU_COMPAT_H
#define FLECS_ENGINE_WGPU_COMPAT_H

#ifdef __EMSCRIPTEN__
#define WGPU_DEPTH_SLICE
#else
#define WGPU_DEPTH_SLICE .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
#endif

/* WGPUTextureViewDescriptor.usage — not present in emscripten's header.
   Usage: WGPU_COMPAT_TEXTURE_VIEW_USAGE(WGPUTextureUsage_TextureBinding) */
#ifdef __EMSCRIPTEN__
#define WGPU_TEXTURE_VIEW_USAGE(u)
#else
#define WGPU_TEXTURE_VIEW_USAGE(u) .usage = (u),
#endif

#ifdef __EMSCRIPTEN__

/* ---- Type renames (wgpu-native v27 name → emscripten name) ---- */

/* Shader source */
typedef WGPUShaderModuleWGSLDescriptor WGPUShaderSourceWGSL;
#define WGPUSType_ShaderSourceWGSL WGPUSType_ShaderModuleWGSLDescriptor

/* Texel copy (renamed in newer spec) */
typedef WGPUImageCopyTexture WGPUTexelCopyTextureInfo;
typedef WGPUImageCopyBuffer  WGPUTexelCopyBufferInfo;
typedef WGPUTextureDataLayout WGPUTexelCopyBufferLayout;

/* Buffer map status */
typedef WGPUBufferMapAsyncStatus WGPUMapAsyncStatus;
#define WGPUMapAsyncStatus_Success WGPUBufferMapAsyncStatus_Success
#define WGPUMapAsyncStatus_Unknown WGPUBufferMapAsyncStatus_Unknown

/* ---- WGPUStringView compat ---- */

typedef struct WGPUStringView {
    char const *data;
    size_t length;
} WGPUStringView;
#define WGPU_STRLEN SIZE_MAX

#define WGPU_STR(s) (s)

/* Shader source code field — emscripten struct uses `const char *code` */
#define WGPU_SHADER_CODE(s) (s)

/* ---- Multisample mask compat ---- */

#define WGPU_MULTISAMPLE_DEFAULT { .count = 1, .mask = 0xFFFFFFFF }
#define WGPU_MULTISAMPLE(n) { .count = (n), .mask = 0xFFFFFFFF }

/* ---- depthSlice compat ---- */

#define WGPU_DEPTH_SLICE_UNDEFINED 0

/* ---- WGPUOptionalBool compat ---- */

/* Emscripten's DepthStencilState uses plain bool for depthWriteEnabled,
   while wgpu-native v27 uses WGPUOptionalBool. */
typedef enum WGPUOptionalBool {
    WGPUOptionalBool_False = 0,
    WGPUOptionalBool_True = 1,
    WGPUOptionalBool_Undefined = 2,
} WGPUOptionalBool;

/* ---- Surface / SwapChain compat ---- */

typedef enum WGPUSurfaceGetCurrentTextureStatus {
    WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal = 0,
    WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal = 1,
    WGPUSurfaceGetCurrentTextureStatus_Error = 2,
} WGPUSurfaceGetCurrentTextureStatus;

/* WGPUSurfaceConfiguration is used in FlecsEngineImpl even on emscripten
   (to store width/height/format). Define it so the struct compiles. */
typedef struct WGPUSurfaceConfiguration {
    WGPUDevice device;
    WGPUTextureUsageFlags usage;
    WGPUTextureFormat format;
    uint32_t width;
    uint32_t height;
    WGPUPresentMode presentMode;
    /* alphaMode not available in emscripten SwapChain API */
} WGPUSurfaceConfiguration;

/* ---- Timestamp writes compat ---- */

/* wgpu-native v27 attaches a single WGPU{Render,Compute}PassTimestampWrites
   struct to a pass descriptor (begin/end write indices); emscripten's older
   header instead uses a counted array of WGPU{Render,Compute}PassTimestampWrite
   (queryIndex + location). GPU timing is optional, so on emscripten we define
   the v27 structs for signature compatibility and disable timestamp attachment
   (see WGPU_TIMESTAMP_WRITES below). */
#ifndef WGPU_QUERY_SET_INDEX_UNDEFINED
#define WGPU_QUERY_SET_INDEX_UNDEFINED 0xFFFFFFFFu
#endif

typedef struct WGPURenderPassTimestampWrites {
    WGPUQuerySet querySet;
    uint32_t beginningOfPassWriteIndex;
    uint32_t endOfPassWriteIndex;
} WGPURenderPassTimestampWrites;

typedef struct WGPUComputePassTimestampWrites {
    WGPUQuerySet querySet;
    uint32_t beginningOfPassWriteIndex;
    uint32_t endOfPassWriteIndex;
} WGPUComputePassTimestampWrites;

#define WGPU_TIMESTAMP_WRITES(ptr) NULL

#else /* native / wgpu-native v27 */

#define WGPU_MULTISAMPLE_DEFAULT { .count = 1 }
#define WGPU_MULTISAMPLE(n) { .count = (n) }

#define WGPU_TIMESTAMP_WRITES(ptr) (ptr)

/* On native, WGPUStringView already exists. These macros build the
   struct literal that wgpu-native expects. */
#define WGPU_STR(s) \
    (WGPUStringView){ .data = (s), .length = WGPU_STRLEN }

/* Shader code — native uses WGPUStringView for .code as well */
#define WGPU_SHADER_CODE(s) \
    (WGPUStringView){ .data = (s), .length = WGPU_STRLEN }

#endif /* __EMSCRIPTEN__ */

#endif /* FLECS_ENGINE_WGPU_COMPAT_H */
