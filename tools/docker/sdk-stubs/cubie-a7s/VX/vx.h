/*
 * Stub header for Khronos OpenVX 1.3
 *
 * Compilation stub only. Real header on board: /usr/include/VX/vx.h
 * OpenVX is an open standard from Khronos: https://www.khronos.org/registry/OpenVX/
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Opaque types ──────────────────────────────────────────────────────────── */
typedef struct _vx_context*        vx_context;
typedef struct _vx_graph*          vx_graph;
typedef struct _vx_node*           vx_node;
typedef struct _vx_tensor*         vx_tensor;
typedef struct _vx_image*          vx_image;
typedef struct _vx_scalar*         vx_scalar;
typedef struct _vx_array*          vx_array;
typedef struct _vx_reference*      vx_reference;
typedef struct _vx_delay*          vx_delay;
typedef struct _vx_pyramid*        vx_pyramid;
typedef struct _vx_remap*          vx_remap;
typedef struct _vx_matrix*         vx_matrix;
typedef struct _vx_distribution*   vx_distribution;
typedef struct _vx_threshold*      vx_threshold;
typedef struct _vx_convolution*    vx_convolution;
typedef struct _vx_lut*            vx_lut;
typedef struct _vx_object_array*   vx_object_array;
typedef struct _vx_meta_format*    vx_meta_format;

/* ── Status codes ──────────────────────────────────────────────────────────── */
typedef enum {
    VX_SUCCESS = 0,
    VX_FAILURE = -1,
    VX_ERROR_INVALID_PARAMETERS = -2,
    VX_ERROR_NOT_SUFFICIENT = -3,
    VX_ERROR_NOT_ALLOCATED = -4,
    VX_ERROR_NOT_IMPLEMENTED = -5,
    VX_ERROR_NOT_SUPPORTED = -6,
    VX_ERROR_GRAPH_ABANDONED = -7,
    VX_ERROR_GRAPH_SCHEDULED = -8,
    VX_ERROR_INVALID_NODE = -9,
    VX_ERROR_INVALID_GRAPH = -10,
    VX_ERROR_INVALID_TYPE = -11,
    VX_ERROR_INVALID_VALUE = -12,
    VX_ERROR_INVALID_DIMENSION = -13,
    VX_ERROR_INVALID_FORMAT = -14,
    VX_ERROR_INVALID_LINK = -15,
    VX_ERROR_INVALID_REFERENCE = -16,
    VX_ERROR_INVALID_SCOPE = -17,
    VX_ERROR_MULTIPLY_WRITERS = -18,
    VX_ERROR_OPTIMIZED_AWAY = -19,
    VX_ERROR_NO_MEMORY = -20,
    VX_ERROR_NO_RESOURCES = -21,
    VX_ERROR_NOT_READY = -22,
} vx_status;

/* ── Tensor data types ─────────────────────────────────────────────────────── */
typedef enum {
    VX_TYPE_UINT8 = 0x0,
    VX_TYPE_INT8 = 0x1,
    VX_TYPE_UINT16 = 0x2,
    VX_TYPE_INT16 = 0x3,
    VX_TYPE_UINT32 = 0x4,
    VX_TYPE_INT32 = 0x5,
    VX_TYPE_INT64 = 0x6,
    VX_TYPE_UINT64 = 0x7,
    VX_TYPE_FLOAT32 = 0x8,
    VX_TYPE_FLOAT64 = 0x9,
    VX_TYPE_ENUM = 0x10,
    VX_TYPE_SIZE = 0x11,
    VX_TYPE_DF_IMAGE = 0x12,
    VX_TYPE_BOOL = 0x13,
    VX_TYPE_KEYPOINT = 0x14,
    VX_TYPE_RECTANGLE = 0x15,
    VX_TYPE_COORDINATES2D = 0x16,
    VX_TYPE_COORDINATES3D = 0x17,
} vx_type_e;

/* ── Type aliases ──────────────────────────────────────────────────────────── */
typedef size_t vx_size;
typedef uint32_t vx_enum;

/* ── Tensor dimensions ─────────────────────────────────────────────────────── */
#define VX_TENSOR_MAX_DIMS 4

/* ── API functions ─────────────────────────────────────────────────────────── */
vx_context vxCreateContext();
vx_status vxReleaseContext(vx_context* context);

vx_graph vxCreateGraph(vx_context context);
vx_status vxReleaseGraph(vx_graph* graph);
vx_status vxVerifyGraph(vx_graph graph);
vx_status vxProcessGraph(vx_graph graph);

vx_tensor vxCreateTensor(vx_context context, vx_size num_of_dims, const vx_size dims[], vx_type_e data_type);
vx_status vxReleaseTensor(vx_tensor* tensor);
vx_status vxCopyTensorPatch(vx_tensor tensor, const vx_size view_start[], const vx_size view_end[], vx_size stride[], void* ptr, vx_enum usage, vx_enum mem_type);
vx_status vxAccessTensorPatch(vx_tensor tensor, const vx_size view_start[], const vx_size view_end[], vx_size stride[], void** ptr, vx_enum usage);
vx_status vxCommitTensorPatch(vx_tensor tensor, const vx_size view_start[], const vx_size view_end[], vx_size stride[], void* ptr, vx_enum mem_type);

vx_node vxTensorMultiplyNode(vx_graph graph, vx_tensor inputs[], vx_tensor outputs[]);
vx_node vxTensorAddNode(vx_graph graph, vx_tensor inputs[], vx_tensor outputs[]);
vx_node vxSoftmaxNode(vx_graph graph, vx_tensor inputs[], vx_tensor outputs[]);
vx_node vxConvolutionNode(vx_graph graph, vx_tensor inputs[], vx_tensor outputs[], vx_size kernel_size);

vx_status vxQueryTensor(vx_tensor tensor, vx_enum attribute, void* ptr, vx_size size);

/* ── Memory types ──────────────────────────────────────────────────────────── */
typedef enum {
    VX_MEMORY_TYPE_NONE = 0,
    VX_MEMORY_TYPE_HOST = 1,
    VX_MEMORY_TYPE_NEST = 2,
    VX_MEMORY_TYPE_DMABUF = 3,
} vx_memory_type_e;

/* ── Usage flags ───────────────────────────────────────────────────────────── */
typedef enum {
    VX_READ_ONLY = 0,
    VX_WRITE_ONLY = 1,
    VX_READ_AND_WRITE = 2,
} vx_access_e;

#ifdef __cplusplus
}
#endif
