#include <array>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/gxm.h>
#include <psp2/io/dirent.h> 
#include <psp2/kernel/sysmem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <psp2/kernel/processmgr.h> 

#define ALIGN(x, a) (((x) + ((a)-1)) & ~((a)-1))
#define abs(x) (((x) < 0) ? -(x) : (x))

#define ANALOG_THRESHOLD 20
#define DISPLAY_BUFFER_COUNT 2
#define DISPLAY_WIDTH 960
#define DISPLAY_HEIGHT 544
#define DISPLAY_STRIDE 1024
#define DISPLAY_COLOR_FORMAT SCE_GXM_COLOR_FORMAT_A8B8G8R8
#define DISPLAY_PIXEL_FORMAT SCE_DISPLAY_PIXELFORMAT_A8B8G8R8

struct display_queue_callback_data {
  void *addr;
};

extern unsigned char _binary_output_vertex0_gxp_start;
extern unsigned char _binary_output_vertex1_gxp_start;
extern unsigned char _binary_output_vertex2_gxp_start;
extern unsigned char _binary_va_test_program_gxp_start;

static SceGxmContext *gxm_context;
static SceUID vdm_ring_buffer_uid;
static void *vdm_ring_buffer_addr;
static SceUID vertex_ring_buffer_uid;
static void *vertex_ring_buffer_addr;
static SceUID fragment_ring_buffer_uid;
static void *fragment_ring_buffer_addr;
static SceUID fragment_usse_ring_buffer_uid;
static void *fragment_usse_ring_buffer_addr;
static SceGxmRenderTarget *gxm_render_target;
static SceGxmColorSurface gxm_color_surfaces[DISPLAY_BUFFER_COUNT];
static SceUID gxm_color_surfaces_uid[DISPLAY_BUFFER_COUNT];
static void *gxm_color_surfaces_addr[DISPLAY_BUFFER_COUNT];
static SceGxmSyncObject *gxm_sync_objects[DISPLAY_BUFFER_COUNT];
static SceGxmShaderPatcher *gxm_shader_patcher;
static SceUID gxm_shader_patcher_buffer_uid;
static void *gxm_shader_patcher_buffer_addr;
static SceUID gxm_shader_patcher_vertex_usse_uid;
static void *gxm_shader_patcher_vertex_usse_addr;
static SceUID gxm_shader_patcher_fragment_usse_uid;
static void *gxm_shader_patcher_fragment_usse_addr;
static SceGxmDepthStencilSurface gxm_depth_stencil_surface;
static void *gpu_alloc_map(SceKernelMemBlockType type, int gpu_attrib,
                           size_t size, SceUID *uid);
static void gpu_unmap_free(SceUID uid);
static void *gpu_vertex_usse_alloc_map(size_t size, SceUID *uid,
                                       unsigned int *usse_offset);
static void gpu_vertex_usse_unmap_free(SceUID uid);
static void *gpu_fragment_usse_alloc_map(size_t size, SceUID *uid,
                                         unsigned int *usse_offset);
static void gpu_fragment_usse_unmap_free(SceUID uid);
static void *shader_patcher_host_alloc_cb(void *user_data, unsigned int size);
static void shader_patcher_host_free_cb(void *user_data, void *mem);
static void display_queue_callback(const void *callbackData);

static const SceGxmProgram *const gxm_program_output_vertex0_gxp_start =
    (SceGxmProgram *)&_binary_output_vertex0_gxp_start;
static const SceGxmProgram *const gxm_program_output_vertex1_gxp_start =
    (SceGxmProgram *)&_binary_output_vertex1_gxp_start;
static const SceGxmProgram *const gxm_program_output_vertex2_gxp_start =
    (SceGxmProgram *)&_binary_output_vertex2_gxp_start;

static unsigned int gxm_front_buffer_index;
static unsigned int gxm_back_buffer_index;

struct Output {
  int vo0;
  int vo1;
  int vo2;
  int vo3;
};

void test_program(const SceGxmProgram *vert_program, float *input,
                  struct Output *output, int von) {
  SceGxmShaderPatcherId vp_id;
  SceGxmShaderPatcherId fp_id;
  int err;
  SceGxmVertexProgram *final_vertex_program;
  SceGxmFragmentProgram *final_fragment_program;
  err = sceGxmShaderPatcherRegisterProgram(gxm_shader_patcher, vert_program,
                                           &vp_id);
  const SceGxmProgram *frag_program;
  if (von == 0) {
    frag_program = gxm_program_output_vertex0_gxp_start;
  } else if (von == 1) {
    frag_program = gxm_program_output_vertex1_gxp_start;
  } else {
    frag_program = gxm_program_output_vertex2_gxp_start;
  }
  err = sceGxmShaderPatcherRegisterProgram(gxm_shader_patcher, frag_program,
                                           &fp_id);
  const SceGxmProgram *vertex_program =
      sceGxmShaderPatcherGetProgramFromId(vp_id);
  const SceGxmProgram *fragment_program =
      sceGxmShaderPatcherGetProgramFromId(fp_id);

  int i;
  SceGxmVertexAttribute vas[9];
  const char *va_name[9] = {
      "pos", "va0", "va1", "va2", "va3", "va4", "va5", "va6", "va7",
  };
  const char *sa_name[8] = {
      "sa0", "sa4", "sa8", "sa12", "sa16", "sa20", "sa24", "sa28"
  };
  const SceGxmProgramParameter *param;
  for (i = 0; i < 9; i++) {
    param = sceGxmProgramFindParameterByName(vertex_program, va_name[i]);
    vas[i].streamIndex = 0;
    vas[i].offset = 16 * i;
    vas[i].format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
    vas[i].componentCount = 4;
    vas[i].regIndex = sceGxmProgramParameterGetResourceIndex(param);
  }
  SceGxmVertexStream vstream;
  vstream.stride = 16 * 9;
  vstream.indexSource = SCE_GXM_INDEX_SOURCE_INDEX_16BIT;
  err = sceGxmShaderPatcherCreateVertexProgram(
      gxm_shader_patcher, vp_id, vas, 9, &vstream, 1, &final_vertex_program);
  SceGxmBlendInfo disable_color_buffer_blend_info;
  memset(&disable_color_buffer_blend_info, 0,
         sizeof(disable_color_buffer_blend_info));
  disable_color_buffer_blend_info.colorMask = SCE_GXM_COLOR_MASK_NONE;
  disable_color_buffer_blend_info.colorFunc = SCE_GXM_BLEND_FUNC_NONE;
  disable_color_buffer_blend_info.alphaFunc = SCE_GXM_BLEND_FUNC_NONE;
  disable_color_buffer_blend_info.colorSrc = SCE_GXM_BLEND_FACTOR_ZERO;
  disable_color_buffer_blend_info.colorDst = SCE_GXM_BLEND_FACTOR_ZERO;
  disable_color_buffer_blend_info.alphaSrc = SCE_GXM_BLEND_FACTOR_ZERO;
  disable_color_buffer_blend_info.alphaDst = SCE_GXM_BLEND_FACTOR_ZERO;
  err = sceGxmShaderPatcherCreateFragmentProgram(
      gxm_shader_patcher, fp_id, SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,
      SCE_GXM_MULTISAMPLE_NONE, NULL, vertex_program, &final_fragment_program);
  sceGxmBeginScene(gxm_context, 0, gxm_render_target, NULL, NULL,
                   gxm_sync_objects[gxm_back_buffer_index],
                   &gxm_color_surfaces[gxm_back_buffer_index], NULL);
  SceUID indices_uid;
  unsigned short *const indices = (unsigned short *const)gpu_alloc_map(
      SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, SCE_GXM_MEMORY_ATTRIB_READ,
      4 * sizeof(unsigned short), &indices_uid);

  indices[0] = 0;
  indices[1] = 1;
  indices[2] = 2;
  indices[3] = 3;

  sceGxmSetFrontDepthFunc(gxm_context, SCE_GXM_DEPTH_FUNC_ALWAYS);
  sceGxmSetVertexProgram(gxm_context, final_vertex_program);
  sceGxmSetFragmentProgram(gxm_context, final_fragment_program);

  void *buffer;
  sceGxmReserveVertexDefaultUniformBuffer(gxm_context, &buffer);

  for (i = 0; i < 8; i++) {
    param = sceGxmProgramFindParameterByName(vertex_program, sa_name[i]);
    sceGxmSetUniformDataF(buffer, param, 0, 4, &input[i*4]);
  }

  sceGxmSetVertexStream(gxm_context, 0, input);
  err = sceGxmDraw(gxm_context, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP,
                   SCE_GXM_INDEX_FORMAT_U16, indices, 4);
  sceGxmEndScene(gxm_context, NULL, NULL);
  sceGxmFinish(gxm_context);
  int *pixels = (int *)gxm_color_surfaces_addr[gxm_back_buffer_index];
  int vo0 = pixels[0];
  int vo1 = pixels[DISPLAY_WIDTH - 1];
  int vo2 = pixels[DISPLAY_WIDTH * (DISPLAY_HEIGHT - 1)];
  int vo3 = pixels[DISPLAY_WIDTH * DISPLAY_HEIGHT - 1];
  output->vo0 = vo0;
  output->vo1 = vo1;
  output->vo2 = vo2;
  output->vo3 = vo3;

	sceGxmShaderPatcherReleaseVertexProgram(gxm_shader_patcher,
		final_vertex_program);
	sceGxmShaderPatcherReleaseFragmentProgram(gxm_shader_patcher,
		final_fragment_program);


	sceGxmShaderPatcherUnregisterProgram(gxm_shader_patcher,
		vp_id);
	sceGxmShaderPatcherUnregisterProgram(gxm_shader_patcher,
		fp_id);
}

std::array<int, 4> unpack_4xu8(int x) {
  int a = (x & 0xFF000000) >> 4 * 6;
  int b = (x & 0xFF0000) >> 4 * 4;
  int g = (x & 0xFF00) >> 4 * 2;
  int r = x & 0xFF;
  return {r, g, b, a};
}

int main(int argc, char *argv[]) {
  int i;
  sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
  SceGxmInitializeParams gxm_init_params;
  memset(&gxm_init_params, 0, sizeof(gxm_init_params));
  gxm_init_params.flags = 0;
  gxm_init_params.displayQueueMaxPendingCount = 1;
  gxm_init_params.displayQueueCallback = display_queue_callback;
  gxm_init_params.displayQueueCallbackDataSize =
      sizeof(struct display_queue_callback_data);
  gxm_init_params.parameterBufferSize = SCE_GXM_DEFAULT_PARAMETER_BUFFER_SIZE;

  sceGxmInitialize(&gxm_init_params);

  vdm_ring_buffer_addr = gpu_alloc_map(
      SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, SCE_GXM_MEMORY_ATTRIB_READ,
      SCE_GXM_DEFAULT_VDM_RING_BUFFER_SIZE, &vdm_ring_buffer_uid);

  vertex_ring_buffer_addr = gpu_alloc_map(
      SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, SCE_GXM_MEMORY_ATTRIB_READ,
      SCE_GXM_DEFAULT_VERTEX_RING_BUFFER_SIZE, &vertex_ring_buffer_uid);

  fragment_ring_buffer_addr = gpu_alloc_map(
      SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, SCE_GXM_MEMORY_ATTRIB_READ,
      SCE_GXM_DEFAULT_FRAGMENT_RING_BUFFER_SIZE, &fragment_ring_buffer_uid);

  unsigned int fragment_usse_offset;
  fragment_usse_ring_buffer_addr = gpu_fragment_usse_alloc_map(
      SCE_GXM_DEFAULT_FRAGMENT_USSE_RING_BUFFER_SIZE, &fragment_ring_buffer_uid,
      &fragment_usse_offset);
  SceGxmContextParams gxm_context_params;
  memset(&gxm_context_params, 0, sizeof(gxm_context_params));
  gxm_context_params.hostMem = malloc(SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE);
  gxm_context_params.hostMemSize = SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE;
  gxm_context_params.vdmRingBufferMem = vdm_ring_buffer_addr;
  gxm_context_params.vdmRingBufferMemSize =
      SCE_GXM_DEFAULT_VDM_RING_BUFFER_SIZE;
  gxm_context_params.vertexRingBufferMem = vertex_ring_buffer_addr;
  gxm_context_params.vertexRingBufferMemSize =
      SCE_GXM_DEFAULT_VERTEX_RING_BUFFER_SIZE;
  gxm_context_params.fragmentRingBufferMem = fragment_ring_buffer_addr;
  gxm_context_params.fragmentRingBufferMemSize =
      SCE_GXM_DEFAULT_FRAGMENT_RING_BUFFER_SIZE;
  gxm_context_params.fragmentUsseRingBufferMem = fragment_usse_ring_buffer_addr;
  gxm_context_params.fragmentUsseRingBufferMemSize =
      SCE_GXM_DEFAULT_FRAGMENT_USSE_RING_BUFFER_SIZE;
  gxm_context_params.fragmentUsseRingBufferOffset = fragment_usse_offset;

  sceGxmCreateContext(&gxm_context_params, &gxm_context);
  SceGxmRenderTargetParams render_target_params;
  memset(&render_target_params, 0, sizeof(render_target_params));
  render_target_params.flags = 0;
  render_target_params.width = DISPLAY_WIDTH;
  render_target_params.height = DISPLAY_HEIGHT;
  render_target_params.scenesPerFrame = 1;
  render_target_params.multisampleMode = SCE_GXM_MULTISAMPLE_NONE;
  render_target_params.multisampleLocations = 0;
  render_target_params.driverMemBlock = -1;
  sceGxmCreateRenderTarget(&render_target_params, &gxm_render_target);
  for (i = 0; i < DISPLAY_BUFFER_COUNT; i++) {
    gxm_color_surfaces_addr[i] = gpu_alloc_map(
        SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
        SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE,
        ALIGN(4 * DISPLAY_STRIDE * DISPLAY_HEIGHT, 1 * 1024 * 1024),
        &gxm_color_surfaces_uid[i]);
    memset(gxm_color_surfaces_addr[i], 0, DISPLAY_WIDTH * DISPLAY_HEIGHT);
    sceGxmColorSurfaceInit(
        &gxm_color_surfaces[i], DISPLAY_COLOR_FORMAT,
        SCE_GXM_COLOR_SURFACE_LINEAR, SCE_GXM_COLOR_SURFACE_SCALE_NONE,
        SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT, DISPLAY_WIDTH, DISPLAY_HEIGHT,
        DISPLAY_WIDTH, gxm_color_surfaces_addr[i]);
    sceGxmSyncObjectCreate(&gxm_sync_objects[i]);
  }

  sceGxmDepthStencilSurfaceInitDisabled(&gxm_depth_stencil_surface);
  static const unsigned int shader_patcher_buffer_size = 64 * 1024;
  static const unsigned int shader_patcher_vertex_usse_size = 64 * 1024;
  static const unsigned int shader_patcher_fragment_usse_size = 64 * 1024;

  gxm_shader_patcher_buffer_addr =
      gpu_alloc_map(SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
                    SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_READ,
                    shader_patcher_buffer_size, &gxm_shader_patcher_buffer_uid);

  unsigned int shader_patcher_vertex_usse_offset;
  gxm_shader_patcher_vertex_usse_addr = gpu_vertex_usse_alloc_map(
      shader_patcher_vertex_usse_size, &gxm_shader_patcher_vertex_usse_uid,
      &shader_patcher_vertex_usse_offset);

  unsigned int shader_patcher_fragment_usse_offset;
  gxm_shader_patcher_fragment_usse_addr = gpu_fragment_usse_alloc_map(
      shader_patcher_fragment_usse_size, &gxm_shader_patcher_fragment_usse_uid,
      &shader_patcher_fragment_usse_offset);

  SceGxmShaderPatcherParams shader_patcher_params;
  memset(&shader_patcher_params, 0, sizeof(shader_patcher_params));
  shader_patcher_params.userData = NULL;
  shader_patcher_params.hostAllocCallback = shader_patcher_host_alloc_cb;
  shader_patcher_params.hostFreeCallback = shader_patcher_host_free_cb;
  shader_patcher_params.bufferAllocCallback = NULL;
  shader_patcher_params.bufferFreeCallback = NULL;
  shader_patcher_params.bufferMem = gxm_shader_patcher_buffer_addr;
  shader_patcher_params.bufferMemSize = shader_patcher_buffer_size;
  shader_patcher_params.vertexUsseAllocCallback = NULL;
  shader_patcher_params.vertexUsseFreeCallback = NULL;
  shader_patcher_params.vertexUsseMem = gxm_shader_patcher_vertex_usse_addr;
  shader_patcher_params.vertexUsseMemSize = shader_patcher_vertex_usse_size;
  shader_patcher_params.vertexUsseOffset = shader_patcher_vertex_usse_offset;
  shader_patcher_params.fragmentUsseAllocCallback = NULL;
  shader_patcher_params.fragmentUsseFreeCallback = NULL;
  shader_patcher_params.fragmentUsseMem = gxm_shader_patcher_fragment_usse_addr;
  shader_patcher_params.fragmentUsseMemSize = shader_patcher_fragment_usse_size;
  shader_patcher_params.fragmentUsseOffset =
      shader_patcher_fragment_usse_offset;
  sceGxmShaderPatcherCreate(&shader_patcher_params, &gxm_shader_patcher);

  struct Output output;
  struct Output output1;
  struct Output output2;
  SceUID clear_vertices_uid;
  void *vas = gpu_alloc_map(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
                            SCE_GXM_MEMORY_ATTRIB_READ, 4 * 4 * 9 * 4,
                            &clear_vertices_uid);

  gxm_front_buffer_index = 0;
  gxm_back_buffer_index = 0;

  SceCtrlData pad;
  memset(&pad, 0, sizeof(pad));
  int t;

  const char* input_file_root = "ux0:data/ftvg/input/";
  const char* gxp_file_root = "ux0:data/ftvg/gxp/";
  const char* res_file_root = "ux0:data/ftvg/res/";
	int uid = sceIoDopen(gxp_file_root);
	if(uid <= 0) {
    printf("error opening gxp files\n");
    return 0;
  }
	
	printf("Opened, reading directory.\n");
	
	// iterate over the directory for files, print name and size of array (always 256)
	// this means you use strlen() to get length of file name
	SceIoDirent dir;
	char temp_name[4096]; // 4096 is linux's forced policy
	while(sceIoDread(uid, &dir) > 0) {
		printf("Read `%s` (%d) \n", dir.d_name, sizeof(dir.d_name));

    std::string input_file_path = std::string(input_file_root) + dir.d_name + ".txt";
		std::string gxp_file_path = std::string(gxp_file_root) + dir.d_name;
    std::string res_file_path = std::string(res_file_root) + dir.d_name + ".txt";

    FILE* fd = fopen(gxp_file_path.c_str(), "rb");
    char* buf; 
    int size;
    fseek(fd, 0, SEEK_END);
    size = ftell(fd);
    fseek(fd, 0, SEEK_SET);

    buf = (char*)malloc(size);
    fread(buf, size, 1, fd);
    fclose(fd);

    auto input_fd = fopen(input_file_path.c_str(), "r");
    
    auto res_fd = fopen(res_file_path.c_str(), "w");

    float *input = (float *)vas;
    for (int i = 0; i < 144; ++i) {
      float r;
      fscanf(input_fd, "%f", &r);
      input[i] = r;
    }
    input[0] = -1.0f;
    input[1] = -1.0f;
    input[36] = 1.0f;
    input[37] = -1.0f;
    input[72] = -1.0f;
    input[73] = 1.0f;
    input[108] = 1.0f;
    input[109] = 1.0f;

    test_program((const SceGxmProgram*)buf, (float *)vas, &output,
                 0);
    int j;
    fprintf(res_fd, "INPUT\n");
    fprintf(res_fd, "uniforms:\n");
    for (i = 0; i < 8; i++) {
      for (j = 0; j < 4; j++) {
        fprintf(res_fd, "sa%d: %x ", i*4+j, *(unsigned int *)&input[i*4 + j]);
      }
    }
    fprintf(res_fd, "\n");
    fprintf(res_fd, "-------------------------------------------------------\n");
    for (j = 0; j < 4; j++) {
      fprintf(res_fd, "vertex %d:\n", j);
      for (i = 4; i < 4 * 9; i++) {
        fprintf(res_fd, "pa%d: %x ", i, *(unsigned int *)&input[4 * 9 * j + i]);
      }
      fprintf(res_fd, "\n");
    }
    fprintf(res_fd, "-------------------------------------------------------\n");


    test_program((const SceGxmProgram*)buf, (float *)vas, &output1,
                 1);
    test_program((const SceGxmProgram*)buf, (float *)vas, &output2,
                 2);
    fprintf(res_fd, "OUTPUT\n");
    fprintf(res_fd, "-------------------------------------------------------\n");
    int res[4][3] = {{output.vo2, output1.vo2, output2.vo2},
                     {output.vo3, output1.vo3, output2.vo3},
                     {output.vo0, output1.vo0, output2.vo0},
                     {output.vo1, output1.vo1, output2.vo1}};

    int z;
    for (i = 0; i < 4; i++) {
      fprintf(res_fd, "vertex %d:\n", i);
      for (j = 0; j < 3; j++) {
        const auto unpacked = unpack_4xu8(res[i][j]);
        for (z = 0; z < 4; z++) {
          fprintf(res_fd, "o%d: %x ", 4 + 4 * j + z, unpacked[z]);
        }
      }
      fprintf(res_fd, "\n");
    }

    fclose(res_fd);
    fclose(input_fd);
    free(buf);
	}
	
	printf("Done reading directory.\n");
	
	// clean up
	sceIoDclose(uid);

  sceGxmDestroyContext(gxm_context);

  sceGxmTerminate();

  sceKernelExitProcess(0);

  return 0;
}


void *gpu_alloc_map(SceKernelMemBlockType type, int gpu_attrib, size_t size,
                    SceUID *uid) {
  SceUID memuid;
  void *addr;

  if (type == SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW)
    size = ALIGN(size, 256 * 1024);
  else
    size = ALIGN(size, 4 * 1024);

  memuid = sceKernelAllocMemBlock("gpumem", type, size, NULL);
  if (memuid < 0) {
    return NULL;
  }

  if (sceKernelGetMemBlockBase(memuid, &addr) < 0) {
    return NULL;
  }

  if (sceGxmMapMemory(addr, size, (SceGxmMemoryAttribFlags)gpu_attrib) < 0) {
    sceKernelFreeMemBlock(memuid);
    return NULL;
  }

  if (uid)
    *uid = memuid;

  return addr;
}

void gpu_unmap_free(SceUID uid) {
  void *addr;

  if (sceKernelGetMemBlockBase(uid, &addr) < 0)
    return;

  sceGxmUnmapMemory(addr);

  sceKernelFreeMemBlock(uid);
}

void *gpu_vertex_usse_alloc_map(size_t size, SceUID *uid,
                                unsigned int *usse_offset) {
  SceUID memuid;
  void *addr;

  size = ALIGN(size, 4 * 1024);

  memuid = sceKernelAllocMemBlock(
      "gpu_vertex_usse", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, size, NULL);
  if (memuid < 0)
    return NULL;

  if (sceKernelGetMemBlockBase(memuid, &addr) < 0)
    return NULL;

  if (sceGxmMapVertexUsseMemory(addr, size, usse_offset) < 0)
    return NULL;

  return addr;
}

void gpu_vertex_usse_unmap_free(SceUID uid) {
  void *addr;

  if (sceKernelGetMemBlockBase(uid, &addr) < 0)
    return;

  sceGxmUnmapVertexUsseMemory(addr);

  sceKernelFreeMemBlock(uid);
}

void *gpu_fragment_usse_alloc_map(size_t size, SceUID *uid,
                                  unsigned int *usse_offset) {
  SceUID memuid;
  void *addr;

  size = ALIGN(size, 4 * 1024);

  memuid = sceKernelAllocMemBlock("gpu_fragment_usse",
                                  SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
                                  size, NULL);
  if (memuid < 0)
    return NULL;

  if (sceKernelGetMemBlockBase(memuid, &addr) < 0)
    return NULL;

  if (sceGxmMapFragmentUsseMemory(addr, size, usse_offset) < 0)
    return NULL;

  return addr;
}

void gpu_fragment_usse_unmap_free(SceUID uid) {
  void *addr;

  if (sceKernelGetMemBlockBase(uid, &addr) < 0)
    return;

  sceGxmUnmapFragmentUsseMemory(addr);

  sceKernelFreeMemBlock(uid);
}

void *shader_patcher_host_alloc_cb(void *user_data, unsigned int size) {
  return malloc(size);
}

void shader_patcher_host_free_cb(void *user_data, void *mem) {
  return free(mem);
}

void display_queue_callback(const void *callbackData) {
  SceDisplayFrameBuf display_fb;
  const struct display_queue_callback_data *cb_data =
      (display_queue_callback_data *)callbackData;

  memset(&display_fb, 0, sizeof(display_fb));
  display_fb.size = sizeof(display_fb);
  display_fb.base = cb_data->addr;
  display_fb.pitch = DISPLAY_WIDTH;
  display_fb.pixelformat = DISPLAY_PIXEL_FORMAT;
  display_fb.width = DISPLAY_WIDTH;
  display_fb.height = DISPLAY_HEIGHT;

  sceDisplaySetFrameBuf(&display_fb, SCE_DISPLAY_SETBUF_NEXTFRAME);

  sceDisplayWaitVblankStart();
}
