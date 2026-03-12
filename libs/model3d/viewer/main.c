/*
 * viewer/main.c
 *
 * Copyright (C) 2022 bzt (bztsrc@gitlab)
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * @brief WebAssembly based Model3D viewer
 * https://gitlab.com/bztsrc/model3d
 *
 */

#include <math.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/key_codes.h>
#include <GLES3/gl3.h>
#include <EGL/egl.h>
#define M3D_IMPLEMENTATION
#define M3D_ASCII
#include <m3d.h>

int msec = 0, screenw, screenh;
GLuint frag_id, vert_id, prog_id, proj_id, view_id, vao_id, vbo_id;
GLuint tex_id, ht_id, amb_id, spe_id, shi_id, emi_id;
float proj[16], view[16];
const char *frag = "#version 300 es\n"
"precision mediump float;\n"
"in vec3 pos;\n"
"in vec3 norm;\n"
"in vec2 uv;\n"
"in vec4 col;\n"
"in mat4 mv;\n"
"out vec4 glColor;\n"
"uniform sampler2D tex;\n"
"uniform int hastex;\n"
"uniform vec4 ambient;\n"
"uniform vec4 specular;\n"
"uniform float shininess;\n"
"uniform vec4 emission;\n"
"vec3 light_position = vec3(-1.0, 2.0, 2.0);\n"
"vec3 light_color = vec3(1.0, 1.0, 1.0);\n"
"void main(){\n"
"vec3 light_dir = normalize(light_position - pos);\n"
"vec4 diff = vec4(max(dot(norm, light_dir), 0.0) * light_color, 1.0);\n"
"vec3 eyedirn = normalize(vec3(0.0) - pos);\n"
"vec3 refl_dir = reflect(-light_dir, norm);\n"
"vec4 spec = vec4(pow(max(dot(eyedirn, refl_dir), 0.0), shininess) * light_color, 1.0) * specular;\n"
"vec4 color;\n"
"if(hastex != 0) color = texture(tex, uv); else color = col;\n"
"glColor = (ambient + diff + spec) * color;\n"
"}";
const char *vert = "#version 300 es\n"
"layout(location = 0) in vec3 vert_pos;\n"
"layout(location = 1) in vec3 vert_norm;\n"
"layout(location = 2) in vec2 vert_uv;\n"
"layout(location = 3) in vec4 vert_col;\n"
"uniform mat4 proj;\n"
"uniform mat4 view;\n"
"out vec3 pos;\n"
"out vec3 norm;\n"
"out vec2 uv;\n"
"out vec4 col;\n"
"out mat4 mv;\n"
"void main(){\n"
"gl_Position = proj * view * vec4(vert_pos, 1.0f);\n"
"pos = vert_pos;\n"
"norm = normalize(mat3(transpose(inverse(view))) * vert_norm);\n"
"uv = vert_uv;\n"
"col = vert_col;\n"
"mv = view;\n"
"}";
/*
void debug_shader(GLuint shader)
{
    GLint success = 0, log_size = 0;
    char *log;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if(!success) {
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_size);
        log = malloc(log_size);
        glGetShaderInfoLog(shader, log_size, NULL, log);
        printf("%s\n", log);
        free(log);
        exit(1);
    }
}
*/
unsigned int actionid = 0, frame = -1U, fpsdivisor = 1, lastframe = 0;
char *wintitle = NULL, infostr[4096];
m3d_t *model = NULL;
int mousebtn = 0, mousemove = 0, mousex = 0, mousey = 0, mousez = 0, doframe = 0, domesh = 1, doskel = 0;
float mindist = 1, maxdist = 25, distance = 5.5, pitch = /*0*/-35.264, yaw = /*0*/45;
unsigned int numtexture, texture[32];
unsigned char checker_data[4*128*128];
uint32_t default_color = 0xFF235580;

/* vertex attributes. Packing shouldn't be an issue, but be safe than sorry */
typedef struct {
    float vertex_x;
    float vertex_y;
    float vertex_z;
    float normal_x;
    float normal_y;
    float normal_z;
    float tex_u;
    float tex_v;
    uint32_t color;
    uint32_t vertexid;      /* needed for animation */
    uint32_t normalid;
} __attribute__((packed))vbo_t;
vbo_t *vbo = NULL;
/* integer triplets, material index, vbo index and vbo size */
unsigned int numvbo, numvboidx = 0, *vboidx = NULL;

/**
 * Clean up on exit
 */
void cleanup()
{
    if(model) m3d_free(model);
    if(vbo) free(vbo);
}

/**
 * Print an error message and quit
 */
void error(char *msg)
{
    fprintf(stderr, "m3dview: %s\n", msg);
    EM_ASM({var msg = new TextDecoder("utf-8").decode(new Uint8Array(Module.HEAPU8.buffer,$0,$1));alert(msg);}, msg, strlen(msg));
    cleanup();
    exit(1);
}

/**
 * Initialize loaded model
 */
void initmodel()
{
    unsigned int i, j, k, l, last = -2U;
    uint32_t diffuse_color = default_color;
    sprintf(infostr, "%s (%s, %s)\n%d triangles, %d vertices (%d bit), scale %g, %d actions",
        model->name, model->license[0] ? model->license : "no license", model->author[0] ? model->author : "no author",
        model->numface, model->numvertex, model->vc_s << 3, model->scale, model->numaction);
    EM_ASM({var txt = new TextDecoder("utf-8").decode(new Uint8Array(Module.HEAPU8.buffer,$0,$1));document.getElementById('info').innerHTML=txt;},
        infostr, strlen(infostr));

    /* count how many times we switch material context */
    numvbo = model->numface * 3;
    for(i = 0; i < model->numface; i++) {
        if(last != model->face[i].materialid) {
            last = model->face[i].materialid;
            numvboidx++;
        }
    }
    vboidx = (unsigned int*)malloc(numvboidx * 3 * sizeof(numvboidx));
    if(!vboidx) error("unable to allocate memory");
    /* set up vbo array */
    vbo = (vbo_t*)malloc(numvbo * sizeof(vbo_t));
    if(!vbo) error("unable to allocate memory");
    memset(vbo, 0, numvbo * sizeof(vbo_t));
    for(i = k = l = 0, last = -2U; i < model->numface; i++) {
        /* if we change material, record it in vboidx and set diffuse color in vbo.color */
        if(last != model->face[i].materialid) {
            last = model->face[i].materialid;
            diffuse_color = default_color;
            if(last < model->nummaterial)
                for(j = 0; j < model->material[last].numprop; j++)
                    if(model->material[last].prop[j].type == m3dp_Kd) {
                        diffuse_color = model->material[last].prop[j].value.color;
                        break;
                    }
            if(l)
                vboidx[l-1] = k - vboidx[l-2];
            vboidx[l] = last;
            vboidx[l + 1] = k;
            l += 3;
        }
        for(j = 0; j < 3; j++, k++) {
            /* fill up VBO records */
            memcpy(&vbo[k].vertex_x, &model->vertex[model->face[i].vertex[j]].x, 3 * sizeof(float));
            memcpy(&vbo[k].normal_x, &model->vertex[model->face[i].normal[j]].x, 3 * sizeof(float));
            if(model->tmap && model->face[i].texcoord[j] < model->numtmap) {
                vbo[k].tex_u = model->tmap[model->face[i].texcoord[j]].u;
                vbo[k].tex_v = 1.0f - model->tmap[model->face[i].texcoord[j]].v;
            } else
                vbo[k].tex_u = vbo[k].tex_v = 0.0f;
            /* if there's no material, use vertex color for vbo.color, may change for every vertex */
            vbo[k].color = (model->face[i].materialid != -1U ? diffuse_color : (
                model->vertex[model->face[i].vertex[j]].color ? model->vertex[model->face[i].vertex[j]].color : default_color));
            if(!vbo[k].color) vbo[k].color = default_color;
            vbo[k].vertexid = model->face[i].vertex[j];
            vbo[k].normalid = model->face[i].normal[j];
        }
    }
    if(l)
        vboidx[l-1] = k - vboidx[l-2];

    /* set up GL textures */
#ifdef GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, (GLint*)&numtexture);
    if(numtexture > 31) numtexture = 31;
    if(numtexture < 1) return;
#else
    numtexture = 32;
#endif
    memset(texture, 0, sizeof(texture));
    glGenTextures(numtexture, (GLuint*)&texture);
    for (j = k = 0; j < 128; j++)
        for (i = 0; i < 128; i++, k += 4) {
            memcpy(&checker_data[k], &default_color, 4);
            checker_data[k] += ((((i>>5) & 1) ^ ((j>>5) & 1)) << 5);
        }
    glBindTexture(GL_TEXTURE_2D, texture[0]);
#ifdef GL_GENERATE_MIPMAP
    glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
#endif
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 128, 128, 0, GL_RGBA, GL_UNSIGNED_BYTE, checker_data);
    /* add textures from model */
    if(model->numtexture) {
        for(i = 0; i < numtexture && i < model->numtexture; i++) {
            if(!model->texture[i].w || !model->texture[i].h || !model->texture[i].d) {
                fprintf(stderr, "m3dview: unable to load texture '%s'\n", model->texture[i].name);
                texture[1 + i] = texture[0];
                continue;
            }
            glBindTexture(GL_TEXTURE_2D, texture[1 + i]);
#ifdef GL_GENERATE_MIPMAP
            glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
#endif
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            switch(model->texture[i].f) {
                case 1: k = GL_LUMINANCE; break;
                case 2: k = GL_LUMINANCE_ALPHA; break;
                case 3: k = GL_RGB; break;
                default: k = GL_RGBA; break;
            }
            glTexImage2D(GL_TEXTURE_2D, 0, k, model->texture[i].w, model->texture[i].h, 0, k, GL_UNSIGNED_BYTE,
                model->texture[i].d);
        }
    }
}

/**
 * Multiply a vertex with a transformation matrix
 */
void vec3_mul_mat4(m3dv_t *out, m3dv_t *v, float *mat)
{
    out->x = mat[ 0] * v->x + mat[ 1] * v->y + mat[ 2] * v->z + mat[ 3];
    out->y = mat[ 4] * v->x + mat[ 5] * v->y + mat[ 6] * v->z + mat[ 7];
    out->z = mat[ 8] * v->x + mat[ 9] * v->y + mat[10] * v->z + mat[11];
}

/**
 * Multiply a vertex with a rotation matrix
 */
void vec3_mul_mat3(m3dv_t *out, m3dv_t *v, float *mat)
{
    out->x = mat[ 0] * v->x + mat[ 1] * v->y + mat[ 2] * v->z;
    out->y = mat[ 4] * v->x + mat[ 5] * v->y + mat[ 6] * v->z;
    out->z = mat[ 8] * v->x + mat[ 9] * v->y + mat[10] * v->z;
}

/**
 * Multiply 4x4 matrix to the left
 */
void mat4_mul(float *c, float *b) {
    int i, j, k;
    float s, a[16];

    memcpy(&a, c, sizeof(a));
    for(i = 0; i < 4; i++)
        for(j = 0; j < 4; j++) {
            for(s = 0.0, k = 0; k < 4; k++)
                s += a[i+k*4] * b[k+j*4];
            c[i+j*4] = s;
        }
}

/**
 * Rotate by angle t on axis u
 */
void rotate(float *out, float t, int u)
{
    float s, c, a[16];
    int v, w;
    if((v = u + 1) > 2) v = 0;
    if((w = v + 1) > 2) w = 0;
    t *= 0.0174533; s = sinf(t); c = cosf(t);
    memset(&a, 0, sizeof(a)); a[0] = a[5] = a[10] = a[15] = 1.0;
    a[v+v*4] = c;
    a[v+w*4] = -s;
    a[w+v*4] = s;
    a[w+w*4] = c;
    mat4_mul(out, a);
}

/**
 * Calculate frustum projection
 */
void frustum(float *out, float l, float r, float b, float t, float n, float f)
{
    out[0] = (2.0 * n) / (r - l); out[1] = 0;                   out[2] = 0;                           out[3] = 0;
    out[4] = 0;                   out[5] = (2.0 * n) / (t - b); out[6] = 0;                           out[7] = 0;
    out[8] = (r + l) / (r - l);   out[9] = (t + b) / (t - b);   out[10] = -(f + n) / (f - n);         out[11] = -1;
    out[12] = 0;                  out[13] = 0;                  out[14] = -(2.0 * f * n) / (f - n);   out[15] = 0;
}

/**
 * Convert a standard uint32_t color into OpenGL color
 */
void set_color(uint32_t c, float *f) {
    if(!c) {
        f[0] = f[1] = f[2] = 0.0; f[3] = 1.0;
    } else {
        f[3] = ((float)((c >> 24)&0xff)) / 255;
        f[2] = ((float)((c >> 16)&0xff)) / 255;
        f[1] = ((float)((c >>  8)&0xff)) / 255;
        f[0] = ((float)((c >>  0)&0xff)) / 255;
    }
}

/**
 * Set material to use.
 */
void set_material(unsigned int mi)
{
    unsigned int i, t;
    m3dm_t *m;
    float ambient[4] = { 0 }, specular[4] = { 0 }, emission[4] = { 0 }, shininess = 0.0;

    glActiveTexture(GL_TEXTURE0);
    glUniform1i(ht_id, 0);
    /* update with what we have in this new material */
    if(mi < model->nummaterial) {
        m = &model->material[mi];
        for(i = 0; i < m->numprop; i++) {
            switch(m->prop[i].type) {
                case m3dp_Kd: /* this is already copied into the VBO */ break;
                case m3dp_Ka: set_color(m->prop[i].value.color, (float*)&ambient); break;
                case m3dp_Ks: set_color(m->prop[i].value.color, (float*)&specular); break;
                case m3dp_Ke: set_color(m->prop[i].value.color, (float*)&emission); break;
                case m3dp_Ns: shininess = m->prop[i].value.fnum; break;
                case m3dp_map_Kd:
                    if(numtexture) {
                        t = m->prop[i].value.textureid < numtexture ? m->prop[i].value.textureid + 1 : 0;
                        glUniform1i(ht_id, 1);
                        glUniform1i(tex_id, 0);
                        glBindTexture(GL_TEXTURE_2D, texture[t]);
                    }
                break;
            }
        }
    }
    glUniform4fv(amb_id, 1, ambient);
    glUniform4fv(spe_id, 1, specular);
    glUniform1f(shi_id, shininess);
    glUniform4fv(emi_id, 1, emission);
}

/**
 * Set FPS divisor for animation debugging
 */
void fpsdiv(int idx)
{
    switch(idx) {
        case 1: fpsdivisor = 2; break;
        case 2: fpsdivisor = 3; break;
        case 3: fpsdivisor = 5; break;
        case 4: fpsdivisor = 10; break;
        case 5: fpsdivisor = 15; break;
        case 6: fpsdivisor = 20; break;
        case 7: fpsdivisor = 25; break;
        case 8: fpsdivisor = 30; break;
        case 9: fpsdivisor = 60; break;
        default: fpsdivisor = 1; break;
    }
}

/**
 * Toggle continous playback
 */
void continous(void)
{
    doframe ^= 1;
    if(actionid < model->numaction) {
        if(frame == -1U) frame = model->action[actionid].numframe - 1;
        if(frame > model->action[actionid].numframe - 1) frame = 0;
    } else
        frame = 0;
}

/**
 * Switch to next frame
 */
void nextframe(void)
{
    doframe = 0; frame++;
    continous();
}

/**
 * Switch to previous frame
 */
void prevframe(void)
{
    doframe = 0; frame--;
    continous();
}

/**
 * Zoom in
 */
void zoomin(void)
{
    distance -= 0.01 * distance;
    if(distance < 0.000001) distance = 0.000001;
}

/**
 * Zoom out
 */
void zoomout(void)
{
    distance += 0.01 * distance;
    if(distance > 100000) distance = 100000;
}

/**
 * Animate the mesh
 */
m3db_t *animate_model(unsigned int msec)
{
    unsigned int i, j, s;
    m3db_t *animpose;
    m3dv_t tmp1, tmp2;

    if(actionid < model->numaction && doframe) {
        /* if we are frame debugging, use the exact timestamp of the frame as msec */
        if(frame == -1U) frame = 0;
        msec = model->action[actionid].frame[frame].msec;
    } else
        /* otherwise modify by the debugging fps divisor (is 1 by default) */
        msec /= fpsdivisor;

    /* get the animation-pose skeleton*/
    animpose = m3d_pose(model, actionid, msec);

    /* don't regenerate if we have the same timestamp as last time */
    if(msec == lastframe) return animpose;
    lastframe = msec;

    /* convert mesh vertices from bind-pose into animation-pose */
    for(i = 0; i < numvbo; i++) {
        s = model->vertex[vbo[i].vertexid].skinid;
        if(s != -1U) {
            /* we assume that vbo_t is packed and normals follow vertex coordinates */
            memset(&vbo[i].vertex_x, 0, 6 * sizeof(float));
            for(j = 0; j < M3D_NUMBONE && model->skin[s].weight[j] > 0.0; j++) {
                /* transfer from bind-pose model-space into bone-local space */
                vec3_mul_mat4(&tmp1, &model->vertex[vbo[i].vertexid], (float*)&model->bone[ model->skin[s].boneid[j] ].mat4);
                /* transfer from bone-local space into animation-pose model-space */
                vec3_mul_mat4(&tmp2, &tmp1, (float*)&animpose[ model->skin[s].boneid[j] ].mat4);
                /* multiply with weight and accumulate */
                vbo[i].vertex_x += tmp2.x * model->skin[s].weight[j];
                vbo[i].vertex_y += tmp2.y * model->skin[s].weight[j];
                vbo[i].vertex_z += tmp2.z * model->skin[s].weight[j];
                /* now again for the normal vector */
                vec3_mul_mat3(&tmp1, &model->vertex[vbo[i].normalid], (float*)&model->bone[ model->skin[s].boneid[j] ].mat4);
                vec3_mul_mat3(&tmp2, &tmp1, (float*)&animpose[ model->skin[s].boneid[j] ].mat4);
                vbo[i].normal_x += tmp2.x * model->skin[s].weight[j];
                vbo[i].normal_y += tmp2.y * model->skin[s].weight[j];
                vbo[i].normal_z += tmp2.z * model->skin[s].weight[j];
            }
        }
    }
    glBufferSubData(GL_ARRAY_BUFFER, 0, numvbo * sizeof(vbo_t), vbo);
    return animpose;
}

/**
 * Display the model
 */
void display(unsigned int msec)
{
    m3db_t *animpose = NULL, *bones;
    unsigned int i, j;
    float ct, cp, st, sp, tp[16], fov, fov2, *skel = NULL, s;
    char tmp[1024];

    /* handle model rotation */
    if(mousemove) {
        yaw -= mousex * 0.3;
        pitch -= mousey * 0.2;
        if (pitch < -90) pitch = -90;
        if (pitch > 90) pitch = 90;
        if (yaw < 0) yaw += 360;
        if (yaw > 360) yaw -= 360;
        mousemove = 0;
    }
    /* switch action to animate */
    if(actionid == -1U) actionid = model->numaction;
    else if((unsigned int)actionid > model->numaction) actionid = 0;
    /* animate the mesh */
    if(model->numaction)
        animpose = animate_model(msec);

    screenw = EM_ASM_INT({ return Module.canvas.width; });
    screenh = EM_ASM_INT({ return Module.canvas.height; });
    glViewport(0, 0, screenw, screenh);
    glUseProgram(prog_id);
    glBindVertexArray(vao_id);

    /* display model */
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClearColor(0.1, 0.1, 0.1, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    fov = 0.221694 * (mindist/5);
    fov2 = fov * (float)screenw / (float)screenh;
    frustum(proj, -fov2, fov2, -fov, fov, mindist/5, maxdist*5);
    glUniformMatrix4fv(proj_id, 1, GL_FALSE, (GLfloat*)proj);

    view[ 0] = 1;       view[ 1] = 0;       view[ 2] = 0;       view[ 3] = 0;
    view[ 4] = 0;       view[ 5] = 1;       view[ 6] = 0;       view[ 7] = 0;
    view[ 8] = 0;       view[ 9] = 0;       view[10] = 1;       view[11] = 0;
    view[12] = 0;       view[13] = (float)mousez/10.0f; view[14] = -distance*1.1; view[15] = 1;
    rotate(view, -pitch, 0);
    rotate(view, -yaw, 1);
    glUniformMatrix4fv(view_id, 1, GL_FALSE, (GLfloat*)view);

    /* draw the skeleton */
    if(doskel && model->numbone) {
        bones = animpose ? animpose : model->bone;
    }
    if(animpose) {
        free(animpose);
        animpose = NULL;
    }

    /* draw the mesh */
    if(domesh) {
        for(i = j = 0; i < numvboidx; i++, j += 3) {
            set_material(vboidx[j + 0]);
            glDrawArrays(GL_TRIANGLES, vboidx[j + 1], vboidx[j + 2]);
        }
    }
    glDisable(GL_DEPTH_TEST);

    tmp[0] = 0;
    if(model->numaction) {
        sprintf(tmp, "current: %s ", actionid < model->numaction ? model->action[actionid].name : "(bind-pose)");
    }
    if(doframe) {
        sprintf(&tmp[strlen(tmp)], "frame %4d / %4d",
            actionid < model->numaction ? frame + 1 : 1,
            actionid < model->numaction ? model->action[actionid].numframe : 1);
    }
    sprintf(&tmp[strlen(tmp)], "\n");
    EM_ASM({var txt = new TextDecoder("utf-8").decode(new Uint8Array(Module.HEAPU8.buffer,$0,$1));document.getElementById('stat').innerHTML=txt;},
        tmp, strlen(tmp));
}

/**
 * Main display loop
 */
static void main_loop(void)
{
    msec += 1000/60;
    display(msec);
}

/**
 * Process a keyboard event callback
 */
EM_BOOL key(int type, const EmscriptenKeyboardEvent *key, void *data)
{
    int ourkey = 1;

    (void)type;
    (void)data;
    if(!key->ctrlKey && !key->shiftKey && !key->altKey && !key->metaKey) {
        switch(key->keyCode) {
            case DOM_VK_Q: case DOM_VK_ESCAPE: cleanup(); exit(1); break;
            case DOM_VK_UP: mousez++; break;
            case DOM_VK_DOWN: mousez--; break;
            case DOM_VK_LEFT: mousex = -10; mousey = 0; mousemove = 1; break;
            case DOM_VK_RIGHT: mousex = 10; mousey = 0; mousemove = 1; break;
            case DOM_VK_PAGE_UP: actionid--; break;
            case DOM_VK_TAB:
            case DOM_VK_PAGE_DOWN: actionid++; break;
            case DOM_VK_EQUALS: case DOM_VK_PLUS: case DOM_VK_ADD: zoomin(); break;
            case DOM_VK_HYPHEN_MINUS: case DOM_VK_SUBTRACT: zoomout(); break;
            case DOM_VK_COMMA: prevframe(); break;
            case DOM_VK_PERIOD: nextframe(); break;
            case DOM_VK_SPACE: continous(); break;
            case DOM_VK_M: domesh ^= 1; break;
            case DOM_VK_S: doskel ^= 1; break;
            case DOM_VK_0:
            case DOM_VK_1:
            case DOM_VK_2:
            case DOM_VK_3:
            case DOM_VK_4:
            case DOM_VK_5:
            case DOM_VK_6:
            case DOM_VK_7:
            case DOM_VK_8:
            case DOM_VK_9: fpsdiv(key->keyCode-DOM_VK_0); break;
            default: ourkey = 0; break;
        }
        if(ourkey) EM_ASM({event.preventDefault();event.stopPropagation();});
    }
    return 0;
}

/**
 * Process a mouse event callback
 */
EM_BOOL mouse(int type, const EmscriptenMouseEvent *evt, void *data)
{
    (void)type;
    (void)data;
    if(evt->buttons == 1) {
        mousex = evt->movementX;
        mousey = evt->movementY;
        mousemove = 1;
    }
    return 0;
}

/**
 * Process a wheel event callback
 */
EM_BOOL wheel(int type, const EmscriptenWheelEvent *evt, void *data)
{
    (void)type;
    (void)data;
    if(evt->deltaY < 0) zoomin(); else
    if(evt->deltaY > 0) zoomout(); else
        return 0;
    EM_ASM({event.preventDefault();event.stopPropagation();});
    return 0;
}

/**
 * Main procedure. Set up and main loop
 */
int m3d_viewer(unsigned char *data, int size)
{
    EmscriptenWebGLContextAttributes    attrs;
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE     context;
    GLint status;
    double mx, my, px, py;

    emscripten_webgl_init_context_attributes(&attrs);
    attrs.majorVersion = 2;
    context = emscripten_webgl_create_context("canvas", &attrs);
    emscripten_webgl_make_context_current(context);
    frag_id = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(frag_id, 1, (const GLchar**)&frag, NULL);
    glCompileShader(frag_id);
    /*debug_shader(frag_id);*/
    status = 0; glGetShaderiv(frag_id, GL_COMPILE_STATUS, &status);
    if(status == GL_FALSE) error("unable to compile frag shader");
    vert_id = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vert_id, 1, (const GLchar**)&vert, NULL);
    glCompileShader(vert_id);
    status = 0; glGetShaderiv(vert_id, GL_COMPILE_STATUS, &status);
    if(status == GL_FALSE) error("unable to compile vert shader");
    prog_id = glCreateProgram();
    glAttachShader(prog_id, vert_id);
    glAttachShader(prog_id, frag_id);
    glLinkProgram(prog_id);
    glGetProgramiv(prog_id, GL_LINK_STATUS, &status);
    glDetachShader(prog_id, vert_id);
    glDetachShader(prog_id, frag_id);
    glDeleteShader(vert_id);
    glDeleteShader(frag_id);
    if(status == GL_FALSE) error("unable to set up shaders");
    glUseProgram(prog_id);
    proj_id = glGetUniformLocation(prog_id, "proj");
    view_id = glGetUniformLocation(prog_id, "view");
    tex_id = glGetUniformLocation(prog_id, "tex");
    ht_id = glGetUniformLocation(prog_id, "hastex");
    amb_id = glGetUniformLocation(prog_id, "ambient");
    spe_id = glGetUniformLocation(prog_id, "specular");
    shi_id = glGetUniformLocation(prog_id, "shininess");
    emi_id = glGetUniformLocation(prog_id, "emission");

    model = m3d_load(data, NULL, NULL, NULL);
    if(!model) error("unable to parse model");
    initmodel();

    glGenVertexArrays(1, &vao_id);
    glBindVertexArray(vao_id);
    glGenBuffers(1, &vbo_id);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_id);
    glBufferData(GL_ARRAY_BUFFER, numvbo * sizeof(vbo_t), vbo, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vbo_t), 0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(vbo_t), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(vbo_t), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(vbo_t), (void*)(8 * sizeof(float)));
    glEnableVertexAttribArray(3);

    emscripten_set_main_loop_arg((em_arg_callback_func)main_loop, NULL, -1, 0);
    emscripten_set_keydown_callback("body", NULL, 1, key);
    emscripten_set_mousemove_callback("canvas", NULL, 1, mouse);
    emscripten_set_wheel_callback("canvas", NULL, 1, wheel);
    return 0;
}
