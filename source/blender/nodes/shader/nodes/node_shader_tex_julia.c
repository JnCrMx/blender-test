#include "node_shader_util.h"

static bNodeSocketTemplate sh_node_tex_julia_in[] = {
    {SOCK_VECTOR, N_("Vector"), 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, PROP_NONE, SOCK_HIDE_VALUE},
    {SOCK_VECTOR, N_("Rule"), 0.0f, 0.0f},
    {-1, ""},
};
static bNodeSocketTemplate sh_node_tex_julia_out[] = {
    {SOCK_RGBA, N_("Color"), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f},
    {-1, ""},
};

static void node_shader_init_tex_julia(bNodeTree *UNUSED(ntree), bNode *node)
{
  NodeTexJulia *tex = MEM_callocN(sizeof(NodeTexJulia), "NodeTexJulia");
  BKE_texture_mapping_default(&tex->base.tex_mapping, TEXMAP_TYPE_POINT);
  BKE_texture_colormapping_default(&tex->base.color_mapping);
  tex->iterations = 25;
  tex->breakout = 2.0;
  tex->use_smooth = true;

  node->storage = tex;
}

static int gpu_shader_tex_julia(GPUMaterial *mat,
                              bNode *node,
                              bNodeExecData *UNUSED(execdata),
                              GPUNodeStack *in,
                              GPUNodeStack *out)
{
  NodeTexJulia *tex = (NodeTexJulia *)node->storage;
  float iterations = tex->iterations;
  float breakout = tex->breakout;
  float smooth = tex->use_smooth ? 1.0f : 0.0f;

  node_shader_gpu_default_tex_coord(mat, node, &in[0].link);
  node_shader_gpu_tex_mapping(mat, node, in, out);
  return GPU_stack_link(mat, node, "node_tex_julia", in, out, GPU_constant(&iterations), GPU_constant(&breakout), GPU_constant(&smooth));
}

void register_node_type_sh_tex_julia(void)
{
  static bNodeType ntype;

  sh_node_type_base(&ntype, SH_NODE_TEX_JULIA, "Julia Texture", NODE_CLASS_TEXTURE, 0);
  node_type_socket_templates(&ntype, sh_node_tex_julia_in, sh_node_tex_julia_out);
  node_type_init(&ntype, node_shader_init_tex_julia);
  node_type_storage(&ntype, "NodeTexJulia", node_free_standard_storage, node_copy_standard_storage);
  node_type_gpu(&ntype, gpu_shader_tex_julia);

  nodeRegisterType(&ntype);
}
