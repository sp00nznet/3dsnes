/*
 * renderer.c — OpenGL 3.3 instanced voxel renderer.
 *
 * Renders each visible SNES pixel as a colored cube using GPU instancing.
 * Also supports a 2D framebuffer overlay for reference.
 */

#include "3dsnes/renderer.h"
#include <glad/glad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Shaders ─────────────────────────────────────────────────────── */

static const char *voxel_vert_src =
    "#version 330 core\n"
    "layout(location = 0) in vec3 a_pos;\n"         /* unit cube vertex */
    "layout(location = 1) in vec3 a_normal;\n"       /* cube face normal */
    "layout(location = 2) in vec3 a_inst_pos;\n"     /* instance world pos */
    "layout(location = 3) in vec4 a_inst_color;\n"   /* instance RGBA (0-1) */
    "\n"
    "uniform mat4 u_view;\n"
    "uniform mat4 u_proj;\n"
    "\n"
    "out vec3 v_normal;\n"
    "out vec4 v_color;\n"
    "\n"
    "void main() {\n"
    "    vec3 world_pos = a_pos + a_inst_pos;\n"
    "    gl_Position = u_proj * u_view * vec4(world_pos, 1.0);\n"
    "    v_normal = a_normal;\n"
    "    v_color = a_inst_color;\n"
    "}\n";

static const char *voxel_frag_src =
    "#version 330 core\n"
    "in vec3 v_normal;\n"
    "in vec4 v_color;\n"
    "out vec4 frag_color;\n"
    "\n"
    "void main() {\n"
    "    // Simple directional lighting\n"
    "    vec3 light_dir = normalize(vec3(0.3, 0.8, 0.5));\n"
    "    float diff = max(dot(normalize(v_normal), light_dir), 0.0);\n"
    "    float ambient = 0.35;\n"
    "    float lighting = ambient + diff * 0.65;\n"
    "    frag_color = vec4(v_color.rgb * lighting, v_color.a);\n"
    "}\n";

static const char *quad_vert_src =
    "#version 330 core\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "layout(location = 1) in vec2 a_uv;\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "    v_uv = a_uv;\n"
    "}\n";

static const char *quad_frag_src =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "out vec4 frag_color;\n"
    "uniform sampler2D u_texture;\n"
    "void main() {\n"
    "    frag_color = texture(u_texture, v_uv);\n"
    "}\n";

/* ── Helpers ─────────────────────────────────────────────────────── */

static GLuint compile_shader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "Shader compile error: %s\n", log);
    }
    return s;
}

static GLuint link_program(const char *vert_src, const char *frag_src) {
    GLuint v = compile_shader(GL_VERTEX_SHADER, vert_src);
    GLuint f = compile_shader(GL_FRAGMENT_SHADER, frag_src);
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(p, sizeof(log), NULL, log);
        fprintf(stderr, "Program link error: %s\n", log);
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

/* Unit cube: 36 vertices (12 triangles), each with position + normal */
static const float cube_verts[] = {
    /* pos (x,y,z), normal (nx,ny,nz) */
    /* Front face (+Z) */
    0,0,1, 0,0,1,  1,0,1, 0,0,1,  1,1,1, 0,0,1,
    0,0,1, 0,0,1,  1,1,1, 0,0,1,  0,1,1, 0,0,1,
    /* Back face (-Z) */
    1,0,0, 0,0,-1,  0,0,0, 0,0,-1,  0,1,0, 0,0,-1,
    1,0,0, 0,0,-1,  0,1,0, 0,0,-1,  1,1,0, 0,0,-1,
    /* Top face (+Y) */
    0,1,0, 0,1,0,  0,1,1, 0,1,0,  1,1,1, 0,1,0,
    0,1,0, 0,1,0,  1,1,1, 0,1,0,  1,1,0, 0,1,0,
    /* Bottom face (-Y) */
    0,0,0, 0,-1,0,  1,0,0, 0,-1,0,  1,0,1, 0,-1,0,
    0,0,0, 0,-1,0,  1,0,1, 0,-1,0,  0,0,1, 0,-1,0,
    /* Right face (+X) */
    1,0,0, 1,0,0,  1,0,1, 1,0,0,  1,1,1, 1,0,0,
    1,0,0, 1,0,0,  1,1,1, 1,0,0,  1,1,0, 1,0,0,
    /* Left face (-X) */
    0,0,1, -1,0,0,  0,0,0, -1,0,0,  0,1,0, -1,0,0,
    0,0,1, -1,0,0,  0,1,0, -1,0,0,  0,1,1, -1,0,0,
};

/* ── Public API ──────────────────────────────────────────────────── */

bool renderer_init(Renderer *r, int width, int height) {
    memset(r, 0, sizeof(*r));
    r->width = width;
    r->height = height;
    r->show_3d = false;  /* start in 2D — F1 toggles to 3D */
    r->show_overlay = false;
    r->wireframe = false;

    /* Compile shaders */
    r->voxel_shader = link_program(voxel_vert_src, voxel_frag_src);
    r->quad_shader = link_program(quad_vert_src, quad_frag_src);

    /* ── Unit cube VAO ────────────────────────────────────────── */
    glGenVertexArrays(1, &r->cube_vao);
    glGenBuffers(1, &r->cube_vbo);
    glGenBuffers(1, &r->instance_vbo);

    glBindVertexArray(r->cube_vao);

    /* Cube geometry (shared across all instances) */
    glBindBuffer(GL_ARRAY_BUFFER, r->cube_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cube_verts), cube_verts, GL_STATIC_DRAW);

    /* attrib 0: position */
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    /* attrib 1: normal */
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    /* Instance data buffer (will be filled each frame) */
    r->instance_capacity = 200000;
    glBindBuffer(GL_ARRAY_BUFFER, r->instance_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 r->instance_capacity * (3 * sizeof(float) + 4 * sizeof(float)),
                 NULL, GL_DYNAMIC_DRAW);

    /* attrib 2: instance position (vec3) */
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(2);
    glVertexAttribDivisor(2, 1);

    /* attrib 3: instance color (vec4) */
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float),
                          (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribDivisor(3, 1);

    glBindVertexArray(0);

    /* ── 2D overlay quad VAO ─────────────────────────────────── */
    float quad[] = {
        /* pos x,y   uv u,v */
        -1, -1,  0, 1,
         1, -1,  1, 1,
         1,  1,  1, 0,
        -1, -1,  0, 1,
         1,  1,  1, 0,
        -1,  1,  0, 0,
    };
    glGenVertexArrays(1, &r->fb_vao);
    glGenBuffers(1, &r->fb_vbo);
    glBindVertexArray(r->fb_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->fb_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    /* Framebuffer texture */
    glGenTextures(1, &r->fb_texture);
    glBindTexture(GL_TEXTURE_2D, r->fb_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    /* Pre-allocate CPU staging buffer */
    r->upload_buf_capacity = 200000;
    r->upload_buf = (float *)malloc(r->upload_buf_capacity * 7 * sizeof(float));

    /* Create offscreen FBO for rendering */
    r->fbo_width = width;
    r->fbo_height = height;

    glGenFramebuffers(1, &r->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, r->fbo);

    glGenTextures(1, &r->fbo_color);
    glBindTexture(GL_TEXTURE_2D, r->fbo_color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r->fbo_color, 0);

    glGenRenderbuffers(1, &r->fbo_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, r->fbo_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, r->fbo_depth);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    /* Over-allocate readback buffer — GPU drivers may write extra padding */
    r->readback_buf = (uint8_t *)malloc(width * height * 4 + 65536);

    /* OpenGL state */
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    return true;
}

void renderer_shutdown(Renderer *r) {
    glDeleteProgram(r->voxel_shader);
    glDeleteProgram(r->quad_shader);
    glDeleteVertexArrays(1, &r->cube_vao);
    glDeleteBuffers(1, &r->cube_vbo);
    glDeleteBuffers(1, &r->instance_vbo);
    glDeleteVertexArrays(1, &r->fb_vao);
    glDeleteBuffers(1, &r->fb_vbo);
    glDeleteTextures(1, &r->fb_texture);
    free(r->upload_buf);
    r->upload_buf = NULL;
    free(r->readback_buf);
    r->readback_buf = NULL;
    glDeleteFramebuffers(1, &r->fbo);
    glDeleteTextures(1, &r->fbo_color);
    glDeleteRenderbuffers(1, &r->fbo_depth);
}

void renderer_resize(Renderer *r, int width, int height) {
    r->width = width;
    r->height = height;
    glViewport(0, 0, width, height);
}

void renderer_upload_voxels(Renderer *r, const VoxelMesh *mesh) {
    int count = mesh->count;
    if (count == 0) return;

    /* Grow GPU buffer if needed */
    if (count > r->instance_capacity) {
        r->instance_capacity = count + 10000;
        glBindBuffer(GL_ARRAY_BUFFER, r->instance_vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     r->instance_capacity * 7 * sizeof(float),
                     NULL, GL_DYNAMIC_DRAW);
    }

    /* Grow CPU staging buffer if needed (pre-allocated, no per-frame malloc) */
    if (count > r->upload_buf_capacity) {
        r->upload_buf_capacity = count + 10000;
        free(r->upload_buf);
        r->upload_buf = (float *)malloc(r->upload_buf_capacity * 7 * sizeof(float));
    }

    /* Pack instance data: [x, y, z, r, g, b, a] as floats */
    float *data = r->upload_buf;
    for (int i = 0; i < count; i++) {
        const VoxelInstance *v = &mesh->instances[i];
        data[i * 7 + 0] = v->x;
        data[i * 7 + 1] = v->y;
        data[i * 7 + 2] = v->z;
        data[i * 7 + 3] = v->r / 255.0f;
        data[i * 7 + 4] = v->g / 255.0f;
        data[i * 7 + 5] = v->b / 255.0f;
        data[i * 7 + 6] = v->a / 255.0f;
    }

    glBindBuffer(GL_ARRAY_BUFFER, r->instance_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, count * 7 * sizeof(float), data);
    /* No free — buffer is reused across frames */
}

void renderer_upload_framebuffer(Renderer *r, const uint8_t *pixels,
                                  int width, int height)
{
    glBindTexture(GL_TEXTURE_2D, r->fb_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
}

void renderer_draw(Renderer *r, const Camera *cam, int voxel_count) {
    /* Render to offscreen FBO */
    glBindFramebuffer(GL_FRAMEBUFFER, r->fbo);
    glViewport(0, 0, r->fbo_width, r->fbo_height);

    glClearColor(0.08f, 0.08f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (r->show_3d && voxel_count > 0) {
        if (r->wireframe) {
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        }

        glUseProgram(r->voxel_shader);

        /* Upload matrices */
        GLint view_loc = glGetUniformLocation(r->voxel_shader, "u_view");
        GLint proj_loc = glGetUniformLocation(r->voxel_shader, "u_proj");
        glUniformMatrix4fv(view_loc, 1, GL_FALSE, cam->view);
        glUniformMatrix4fv(proj_loc, 1, GL_FALSE, cam->proj);

        /* Draw instanced cubes */
        glBindVertexArray(r->cube_vao);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 36, voxel_count);
        glBindVertexArray(0);

        if (r->wireframe) {
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }
    }

    /* 2D overlay */
    if (r->show_overlay || !r->show_3d) {
        glDisable(GL_DEPTH_TEST);
        glUseProgram(r->quad_shader);
        glBindVertexArray(r->fb_vao);
        glBindTexture(GL_TEXTURE_2D, r->fb_texture);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
        glEnable(GL_DEPTH_TEST);
    }

    /* Unbind FBO */
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

const uint8_t *renderer_readback(Renderer *r) {
    int w = r->fbo_width, h = r->fbo_height;
    int stride = w * 4;

    /* Read from FBO using PBO for async (non-stalling) readback */
    static GLuint pbo = 0;
    static int pbo_size = 0;
    int needed = w * h * 4;

    if (pbo == 0 || pbo_size < needed) {
        if (pbo) glDeleteBuffers(1, &pbo);
        glGenBuffers(1, &pbo);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
        glBufferData(GL_PIXEL_PACK_BUFFER, needed, NULL, GL_STREAM_READ);
        pbo_size = needed;
    } else {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
    }

    /* Initiate async read from FBO into PBO */
    glBindFramebuffer(GL_FRAMEBUFFER, r->fbo);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, 0); /* offset 0 = into PBO */
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    /* Map PBO to CPU (this may stall, but PBO avoids corrupting other memory) */
    void *mapped = glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
    if (mapped) {
        /* Copy with vertical flip (OpenGL bottom-up -> SDL top-down) */
        for (int y = 0; y < h; y++) {
            memcpy(r->readback_buf + y * stride,
                   (uint8_t*)mapped + (h - 1 - y) * stride, stride);
        }
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    return r->readback_buf;
}
