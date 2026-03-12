/*
 * m3dconv/pmx.h
 *
 * Copyright (C) 2023 bzt (bztsrc@gitlab)
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
 * @brief simple 3D model to M3D converter Polygon Model eXtended importer
 * https://gitlab.com/bztsrc/model3d
 *
 */

static int te = 0, vc = 0, vs = 1, ts = 1, ms = 1, bs = 1, as = 1, rs = 1;

/**
 * Read an index from PMX
 */
int pmx_idx(uint8_t *data, int type)
{
    switch(type) {
        case 1: return data[0] == 0xff ? -1 : data[0];
        case 2: return data[0] == 0xff && data[1] == 0xff ? -1 : data[0] | (data[1] << 8);
        case 4: return data[0] == 0xff && data[1] == 0xff && data[2] == 0xff && data[3] == 0xff ? -1 :
            data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
    }
    return -1;
}

/**
 * Convert a PMX string into a zero terminated UTF-8 C string
 */
void pmx_str(uint8_t *data, int n, char *tmp)
{
    uint16_t u;
    int i, l = 0;

    tmp[0] = 0;
    if(te) {
        for(i = l = 0; i < n && l < 1023; i++) {
            if(data[i] == '\r' || (data[i] == '\n' && l && tmp[l-1] == '\n')) continue;
            tmp[l++] = data[i];
        }
    } else {
        for(i = l = 0; i < n && l < 1020; i++) {
            u = data[i*2] | (data[i*2+1] << 8);
            if(u == '\r' || (u == '\n' && l && tmp[l-1] == '\n')) continue;
            if(u < 0x80) { tmp[l++] = u; } else
            if(u < 0x800) { tmp[l++] = ((u>>6)&0x1F)|0xC0; tmp[l++] = (u&0x3F)|0x80; }
            else { tmp[l++] = ((u>>12)&0x0F)|0xE0; tmp[l++] = ((u>>6)&0x3F)|0x80; tmp[l++] = (u&0x3F)|0x80; }
        }
    }
    tmp[l] = 0;
}

/**
 * Load a model and convert it's structures into a Model 3D in-memory format
 */
m3d_t *pmx_load(unsigned char *data, unsigned int size)
{
    uint8_t *end = data + size, *sav;
    m3d_t *m3d;
    m3dm_t *m;
    m3dv_t *v;
    uint32_t siz;
    int i, j, k, l, n, o, idx, nv = 0, nf = 0, nt = 0, nm = 0, nb = 0;
    float ver, r, g, b, a;
    char tmp[1024], *s, *e;

    m3d = (m3d_t*)malloc(sizeof(m3d_t));
    if(!m3d) { fprintf(stderr, "m3dconv: unable to allocate memory\n"); exit(1); }
    memset(m3d, 0, sizeof(m3d_t));
    m3d->flags = M3D_FLG_FREESTR;

    /* add default position and orientation, may be needed by bones in group statements */
    m3d->numvertex = 2;
    m3d->vertex = (m3dv_t*)malloc(m3d->numvertex * sizeof(m3dv_t));
    if(!m3d->vertex) { fprintf(stderr, "m3dconv: unable to allocate memory\n"); exit(1); }
    memset(m3d->vertex, 0, 2 * sizeof(m3dv_t));
    m3d->vertex[0].skinid = -1U;
    m3d->vertex[0].type = VT_WORLD;
    m3d->vertex[1].skinid = -2U;
    m3d->vertex[1].type = VT_QUATERN;

    /* header */
    memcpy(&ver, data + 4, 4); data += 8;
    n = (int)(uint32_t)*data++;
    if(n > 0) te = data[0]; /* text encoding, 0-UTF16LE, 1-UTF8 */
    if(n > 1) vc = data[1]; /* additional vertex vec4 count */
    if(n > 2) vs = data[2]; /* vertex index size */
    if(n > 3) ts = data[3]; /* texture index size */
    if(n > 4) ms = data[4]; /* material index size */
    if(n > 5) bs = data[5]; /* bone index size */
    if(n > 6) as = data[6]; /* animation morph index size */
    if(n > 7) rs = data[7]; /* rigidbody index size */
    data += n;

    /* name */
    n = data[0] | (data[1] << 8) | (data[2] << 16); data += 4;
    if(!data[n] && !data[n + 1] && !data[n + 2]) {
        pmx_str(data, n, tmp); data += n;
        data += (data[0] | (data[1] << 8) | (data[2] << 16)) + 4;
    } else {
        data += n; n = data[0] | (data[1] << 8) | (data[2] << 16); data += 4;
        pmx_str(data, n, tmp); data += n;
    }
    m3d->name = _m3d_safestr(tmp, 2);

    /* comment */
    n = data[0] | (data[1] << 8) | (data[2] << 16); data += 4;
    if(!data[n] && !data[n + 1] && !data[n + 2]) {
        pmx_str(data, n, tmp); data += n;
        data += (data[0] | (data[1] << 8) | (data[2] << 16)) + 4;
    } else {
        data += n; n = data[0] | (data[1] << 8) | (data[2] << 16); data += 4;
        pmx_str(data, n, tmp); data += n;
    }
    m3d->desc = _m3d_safestr(tmp, 3);

    /* vertex list */
    nv = data[0] | (data[1] << 8) | (data[2] << 16) | ((data[3] & 0x7F) << 24); data += 4;
    if(verbose > 1) printf("  NumVertex %u\n", nv);
    m3d->vertex = (m3dv_t*)realloc(m3d->vertex, (m3d->numvertex + 2 * nv) * sizeof(m3dv_t));
    if(!m3d->vertex) { fprintf(stderr, "m3dconv: unable to allocate memory\n"); exit(1); }
    memset(&m3d->vertex[m3d->numvertex], 0, 2*nv * sizeof(m3dv_t));
    m3d->tmap = (m3dti_t*)realloc(m3d->tmap, (m3d->numtmap + nv) * sizeof(m3dti_t));
    if(!m3d->tmap) { fprintf(stderr, "m3dconv: unable to allocate memory\n"); exit(1); }
    memset(&m3d->tmap[m3d->numtmap], 0, nv * sizeof(m3dti_t));
    m3d->skin = (m3ds_t*)realloc(m3d->skin, (m3d->numskin + nv) * sizeof(m3ds_t));
    if(!m3d->skin) { fprintf(stderr, "m3dconv: unable to allocate memory\n"); exit(1); }
    memset(&m3d->skin[m3d->numskin], 0, nv * sizeof(m3ds_t));
    for(i = 0; i < nv && data < end; i++) {
        /* position */
        memcpy(&m3d->vertex[m3d->numvertex + i].x, data, 3 * sizeof(float));
        m3d->vertex[m3d->numvertex + i].z *= -1.0f;
        m3d->vertex[m3d->numvertex + i].w = 1.0f;
        m3d->vertex[m3d->numvertex + i].skinid = m3d->numskin + i;
        m3d->vertex[m3d->numvertex + i].type = VT_WORLD;
        data += 3 * sizeof(float);
        /* normal */
        memcpy(&m3d->vertex[m3d->numvertex + nv + i], data, 3 * sizeof(float));
        m3d->vertex[m3d->numvertex + nv + i].z *= -1.0f;
        m3d->vertex[m3d->numvertex + nv + i].w = 1.0f;
        m3d->vertex[m3d->numvertex + nv + i].skinid = -1U;
        m3d->vertex[m3d->numvertex + nv + i].type = VT_NORMAL;
        data += 3 * sizeof(float);
        /* uv */
        memcpy(&m3d->tmap[m3d->numtmap + i], data, 2 * sizeof(float));
        m3d->tmap[m3d->numtmap + i].v *= -1.0f;
        data += 2 * sizeof(float);
        /* additional vec4 */
        if(vc) {
            memcpy(&r, data + 0*sizeof(float), sizeof(float));
            memcpy(&b, data + 1*sizeof(float), sizeof(float));
            memcpy(&g, data + 2*sizeof(float), sizeof(float));
            memcpy(&a, data + 3*sizeof(float), sizeof(float));
            if(a > 0.1f)
                m3d->vertex[m3d->numvertex + i].color =
                    ((uint32_t)(a*255.0) << 24L) |
                    ((uint32_t)(b*255.0) << 16L) |
                    ((uint32_t)(g*255.0) <<  8L) |
                    ((uint32_t)(r*255.0) <<  0L);
        }
        data += 4 * vc * sizeof(float);
        /* skin */
        for(j = 0; j < M3D_NUMBONE; j++) m3d->skin[m3d->numskin + i].boneid[j] = -1U;
        switch(*data++) {
            case 0: /* BDEF1 */
                m3d->skin[m3d->numskin + i].boneid[0] = pmx_idx(data, bs); data += bs;
                m3d->skin[m3d->numskin + i].weight[0] = 1.0;
            break;
            case 1: /* BDEF2 */
                m3d->skin[m3d->numskin + i].boneid[0] = pmx_idx(data, bs); data += bs;
                m3d->skin[m3d->numskin + i].boneid[1] = pmx_idx(data, bs); data += bs;
                memcpy(&m3d->skin[m3d->numskin + i].weight[0], data, sizeof(float)); data += sizeof(float);
                m3d->skin[m3d->numskin + i].weight[1] = 1.0 - m3d->skin[m3d->numskin + i].weight[0];
            break;
            case 2: /* BDEF4 */
                for(j = 0; j < 4; j++, data += bs)
                    m3d->skin[m3d->numskin + i].boneid[j] = pmx_idx(data, bs);
                for(j = 0; j < 4; j++, data += sizeof(float))
                    memcpy(&m3d->skin[m3d->numskin + i].weight[j], data, sizeof(float));
            break;
            case 3: /* SDEF */
                data += bs + bs + 10 * sizeof(float);
            break;
            case 4: /* QDEF */
                data += 4 * (bs + sizeof(float));
            break;
        }
        data += sizeof(float); /* outline */
    }

    /* surface */
    n = data[0] | (data[1] << 8) | (data[2] << 16) | ((data[3] & 0x7F) << 24); data += 4;
    nf = n / 3; sav = data;
    if(verbose > 1) printf("  NumSurface %u (tri %u)\n", n, nf);
    m3d->face = (m3df_t*)realloc(m3d->face, (m3d->numface + nf) * sizeof(m3df_t));
    if(!m3d->face) { fprintf(stderr, "m3dconv: unable to allocate memory\n"); exit(1); }
    memset(&m3d->face[m3d->numface], 255, nf * sizeof(m3df_t));
    for(i = 0; i < nf && data < end; i++) {
        m3d->face[m3d->numface + i].materialid = -1U;
        /* CW -> CCW */
        idx = pmx_idx(data, vs); data += vs;
        m3d->face[m3d->numface + i].vertex[0] = m3d->numvertex + idx;
        m3d->face[m3d->numface + i].normal[0] = m3d->numvertex + nv + idx;
        m3d->face[m3d->numface + i].texcoord[0] = m3d->numtmap + idx;
        idx = pmx_idx(data, vs); data += vs;
        m3d->face[m3d->numface + i].vertex[2] = m3d->numvertex + idx;
        m3d->face[m3d->numface + i].normal[2] = m3d->numvertex + nv + idx;
        m3d->face[m3d->numface + i].texcoord[2] = m3d->numtmap + idx;
        idx = pmx_idx(data, vs); data += vs;
        m3d->face[m3d->numface + i].vertex[1] = m3d->numvertex + idx;
        m3d->face[m3d->numface + i].normal[1] = m3d->numvertex + nv + idx;
        m3d->face[m3d->numface + i].texcoord[1] = m3d->numtmap + idx;
    }
    data = sav + n * vs;

    /* texture data */
    nt = data[0] | (data[1] << 8) | (data[2] << 16) | ((data[3] & 0x7F) << 24); data += 4;
    if(verbose > 1) printf("  NumTexture %u\n", nt);
    m3d->texture = (m3dtx_t*)realloc(m3d->texture, (m3d->numtexture + nt) * sizeof(m3dtx_t));
    if(!m3d->texture) { fprintf(stderr, "m3dconv: unable to allocate memory\n"); exit(1); }
    memset(&m3d->texture[m3d->numtexture], 0, nt * sizeof(m3dtx_t));
    for(i = 0; i < nt && data < end; i++) {
        n = data[0] | (data[1] << 8) | (data[2] << 16); data += 4;
        pmx_str(data, n, tmp); data += n;
        s = strrchr(tmp, '/');
        if(!s) s = strrchr(tmp, '\\');
        if(!s) s = tmp; else s++;
        e = strrchr(s, '.'); if(e) *e = 0;
        m3d->texture[m3d->numtexture + i].name = _m3d_safestr(s, 0);
        if(storeinline) {
            if(!e) e = s + strlen(s);
            strcpy(e, ".png");
            for(siz = strlen(tmp)-1; siz > 0; siz--) if(tmp[siz] == '\\') tmp[siz] = '/';
            if(!(readfile(tmp, &siz))) {
                strcpy(e, ".PNG");
                if(!(readfile(tmp, &siz))) {
                    for(siz = strlen(tmp)-1; siz > 0; siz--)
                        if(tmp[siz] >= 'A' && tmp[siz] <= 'Z') tmp[siz] += 'a'-'A';
                    readfile(tmp, &siz);
                }
            }
            if(siz) {
                if(verbose > 1) printf("  Inlining '%s'\n", m3d->texture[m3d->numtexture + i].name);
            } else {
                if(verbose > 2) printf("  Texture '%s' not found\n", tmp);
            }
        }
    }

    /* materials */
    nm = data[0] | (data[1] << 8) | (data[2] << 16) | ((data[3] & 0x7F) << 24); data += 4;
    if(verbose > 1) printf("  NumMaterial %u\n", nm);
    m3d->material = (m3dm_t*)realloc(m3d->material, (m3d->nummaterial + nm) * sizeof(m3dm_t));
    if(!m3d->material) { fprintf(stderr, "m3dconv: unable to allocate memory\n"); exit(1); }
    memset(&m3d->material[m3d->nummaterial], 0, nm * sizeof(m3dm_t));
    for(i = o = 0; i < nm && data < end; i++) {
        m = &m3d->material[m3d->nummaterial + i];
        m->prop = (m3dp_t*)malloc(8 * sizeof(m3dp_t));
        if(!m->prop) { fprintf(stderr, "m3dconv: unable to allocate memory\n"); exit(1); }
        memset(m->prop, 0, 8 * sizeof(m3dp_t));

        /* name */
        n = data[0] | (data[1] << 8) | (data[2] << 16); data += 4;
        if(!data[n] && !data[n + 1] && !data[n + 2]) {
            pmx_str(data, n, tmp); data += n;
            data += (data[0] | (data[1] << 8) | (data[2] << 16)) + 4;
        } else {
            data += n; n = data[0] | (data[1] << 8) | (data[2] << 16); data += 4;
            pmx_str(data, n, tmp); data += n;
        }
        m->name = _m3d_safestr(tmp, 0);
        if(verbose > 1) printf("  Material '%s' (%d, '%s')\n", tmp, i, m->name);

        /* diffuse color */
        memcpy(&r, data, sizeof(float)); data += sizeof(float);
        memcpy(&g, data, sizeof(float)); data += sizeof(float);
        memcpy(&b, data, sizeof(float)); data += sizeof(float);
        memcpy(&a, data, sizeof(float)); data += sizeof(float);
        if(r > M3D_EPSILON && g > M3D_EPSILON && b > M3D_EPSILON && a > M3D_EPSILON) {
            m->prop[m->numprop].type = m3dp_Kd;
            m->prop[m->numprop].value.color =
                ((uint32_t)(a*255.0) << 24L) |
                ((uint32_t)(b*255.0) << 16L) |
                ((uint32_t)(g*255.0) <<  8L) |
                ((uint32_t)(r*255.0) <<  0L);
            if(verbose > 2) printf("    Diffuse %08x\n", m->prop[m->numprop].value.color);
            m->numprop++;
        }

        /* specular color and strength */
        memcpy(&r, data, sizeof(float)); data += sizeof(float);
        memcpy(&g, data, sizeof(float)); data += sizeof(float);
        memcpy(&b, data, sizeof(float)); data += sizeof(float);
        memcpy(&a, data, sizeof(float)); data += sizeof(float);
        if(r > M3D_EPSILON && g > M3D_EPSILON && b > M3D_EPSILON && a > M3D_EPSILON) {
            m->prop[m->numprop].type = m3dp_Ks;
            m->prop[m->numprop].value.color =
                ((uint32_t)0xFF << 24L) |
                ((uint32_t)(b*255.0) << 16L) |
                ((uint32_t)(g*255.0) <<  8L) |
                ((uint32_t)(r*255.0) <<  0L);
            if(verbose > 2) printf("    Specular %08x (strength %f)\n", m->prop[m->numprop].value.color, a);
            m->numprop++;
            m->prop[m->numprop].type = m3dp_Ns;
            m->prop[m->numprop].value.fnum = a;
            m->numprop++;
        }

        /* ambient color */
        memcpy(&r, data, sizeof(float)); data += sizeof(float);
        memcpy(&g, data, sizeof(float)); data += sizeof(float);
        memcpy(&b, data, sizeof(float)); data += sizeof(float);
        if(r > M3D_EPSILON && g > M3D_EPSILON && b > M3D_EPSILON) {
            m->prop[m->numprop].type = m3dp_Ka;
            m->prop[m->numprop].value.color =
                ((uint32_t)0xFF << 24L) |
                ((uint32_t)(b*255.0) << 16L) |
                ((uint32_t)(g*255.0) <<  8L) |
                ((uint32_t)(r*255.0) <<  0L);
            if(verbose > 2) printf("    Ambient %08x\n", m->prop[m->numprop].value.color);
            m->numprop++;
        }

        /* flags */
        k = *data++;
        if(verbose > 2) printf("    Flags %02x\n", k);

        /* edge */
        data += 5 * sizeof(float);

        /* diffuse map */
        idx = pmx_idx(data, ts); data += ts;
        if(idx != -1) {
            l = m->numprop;
            m->prop[m->numprop].type = m3dp_map_Kd;
            m->prop[m->numprop++].value.textureid = m3d->numtexture + idx;
            if(verbose > 2) printf("    DiffuseMap %d\n", idx);
        } else l = -1;

        /* environment map and blend mode */
        idx = pmx_idx(data, ts); data += ts;
        if(idx != -1) {
            m->prop[m->numprop].type = m3dp_map_Km;
            m->prop[m->numprop++].value.textureid = m3d->numtexture + idx;
            if(verbose > 2) printf("    EnvMap %d\n", idx);
        }
        data++;

        /* toon texture */
        if(*data++) {
            idx = *data++;
            if(idx != -1) {
                sprintf(tmp, "toon%02u", idx);
                for(j = 0; j < nt && strcmp(m3d->texture[m3d->numtexture + j].name, tmp); j++);
                if(j >= nt) {
                    sprintf(tmp, "toon-%02u", idx);
                    for(j = 0; j < nt && strcmp(m3d->texture[m3d->numtexture + j].name, tmp); j++);
                    if(j >= nt) {
                        sprintf(tmp, "toon%u", idx);
                        for(j = 0; j < nt && strcmp(m3d->texture[m3d->numtexture + j].name, tmp); j++);
                        if(j >= nt) {
                            sprintf(tmp, "toon-%u", idx);
                            for(j = 0; j < nt && strcmp(m3d->texture[m3d->numtexture + j].name, tmp); j++);
                            if(j >= nt) idx = -1;
                        }
                    }
                }
            }
        } else { idx = pmx_idx(data, ts); data += ts; }
        if(idx != -1) {
            if(verbose > 2) printf("    ToonTexture %d\n", idx);
            /* if we already have a DiffuseMap, replace it */
/*
            if(l != -1) m->prop[l].value.textureid = m3d->numtexture + idx;
            else {
                m->prop[m->numprop].type = m3dp_map_Kd;
                m->prop[m->numprop++].value.textureid = m3d->numtexture + idx;
            }
*/
        }

        /* meta */
        data += (data[0] | (data[1] << 8) | (data[2] << 16)) + 4;

        /* surface count */
        l = (data[0] | (data[1] << 8) | (data[2] << 16) | ((data[3] & 0x7F) << 24)) / 3; data += 4;
        if(verbose > 2) printf("    SurfaceCount %u VertexColor %d\n", l, (k & (1 << 5)) ? 1 : 0);
        for(j = 0; j < l; j++, o++)
            /* vertex color flag not set */
            if(!(k & (1 << 5))) {
                m3d->face[m3d->numface + o].materialid = m3d->nummaterial + i;
                m3d->vertex[m3d->face[m3d->numface + o].vertex[0]].color =
                m3d->vertex[m3d->face[m3d->numface + o].vertex[1]].color =
                m3d->vertex[m3d->face[m3d->numface + o].vertex[2]].color = 0;
            }
    }
    if(verbose > 1 && o != nf) printf("    SurfaceCount %u != NumSurface %u\n", o, nf);

    /* bones */
    nb = data[0] | (data[1] << 8) | (data[2] << 16) | ((data[3] & 0x7F) << 24); data += 4;
    m3d->bone = (m3db_t*)realloc(m3d->bone, (m3d->numbone + nb) * sizeof(m3db_t));
    if(!m3d->bone) { fprintf(stderr, "m3dconv: unable to allocate memory\n"); exit(1); }
    memset(&m3d->bone[m3d->numbone], 0, nb * sizeof(m3db_t));
    m3d->vertex = (m3dv_t*)realloc(m3d->vertex, (m3d->numvertex + 2 * (nv + nb)) * sizeof(m3dv_t));
    if(!m3d->vertex) { fprintf(stderr, "m3dconv: unable to allocate memory\n"); exit(1); }
    memset(&m3d->vertex[m3d->numvertex + 2 * nv], 0, 2 * nb * sizeof(m3dv_t));
    for(i = 0; i < nb && data < end; i++) {
        /* name */
        n = data[0] | (data[1] << 8) | (data[2] << 16); data += 4;
        if(!data[n] && !data[n + 1] && !data[n + 2]) {
            pmx_str(data, n, tmp); data += n;
            data += (data[0] | (data[1] << 8) | (data[2] << 16)) + 4;
        } else {
            data += n; n = data[0] | (data[1] << 8) | (data[2] << 16); data += 4;
            pmx_str(data, n, tmp); data += n;
        }
        m3d->bone[m3d->numbone + i].name = _m3d_safestr(tmp, 0);
        if(verbose > 1) printf("  Bone '%s' (%d, '%s')\n", tmp, i, m3d->bone[m3d->numbone + i].name);

        /* position */
        j = m3d->numvertex + 2 * (nv + i);
        memcpy(&m3d->vertex[j].x, data, 3 * sizeof(float)); data += 3 * sizeof(float);
        m3d->vertex[j].z *= -1.0f;
        m3d->vertex[j].w = 1.0f;
        m3d->vertex[j].skinid = -1U;
        m3d->vertex[j].type = VT_WORLD;
        m3d->bone[m3d->numbone + i].pos = j;
        if(verbose > 2) printf("    Position %u (%f, %f, %f)\n", j, m3d->vertex[j].x, m3d->vertex[j].y, m3d->vertex[j].z);

        /* parent */
        m3d->bone[m3d->numbone + i].parent = pmx_idx(data, bs); data += bs;
        if(verbose > 2) printf("    Parent %d\n", m3d->bone[m3d->numbone + i].parent);

        /* layer */
        data += 4;

        /* flags */
        k = data[0] | (data[1] << 8); data += 2;
        if(verbose > 2) printf("    Flags %04x\n", k);

        /* tail */
        if(!(k & 1)) {
            j = m3d->numvertex + 2 * (nv + i) + 1;
            /* temporarily store the tail position in the oriantaion's place */
            memcpy(&m3d->vertex[j].x, data, 3 * sizeof(float)); data += 3 * sizeof(float);
            m3d->vertex[j].z *= -1.0f;
            m3d->bone[m3d->numbone + i].ori = j;
            m3d->bone[m3d->numbone + i].numweight = 0;  /* use numweight as a flag */
            if(verbose > 2) printf("    Tail Pos %u (%f, %f, %f)\n", j, m3d->vertex[j].x, m3d->vertex[j].y, m3d->vertex[j].z);
        } else {
            /* temporarily store the tail bone index in the oriantaion's index */
            m3d->bone[m3d->numbone + i].ori = pmx_idx(data, bs); data += bs;
            m3d->bone[m3d->numbone + i].numweight = 1;  /* ori is a bone index */
            if(verbose > 2) printf("    Tail Bone %d\n", m3d->bone[m3d->numbone + i].ori);
        }

        /* inherit bone */
        if(k & (3 << 8)) {
            data += bs + sizeof(float);
            if(verbose > 2) printf("    Inherit bone\n");
        }

        /* fixed axis */
        if(k & (1 << 10)) {
            data += 3 * sizeof(float);
            if(verbose > 2) printf("    Fixed axis\n");
        }

        /* local coordinate */
        if(k & (1 << 11)) {
            data += 6 * sizeof(float);
            if(verbose > 2) printf("    Local\n");
        }

        /* external parent */
        if(k & (1 << 12)) {
            data += bs;
            if(verbose > 2) printf("    External\n");
        }

        /* inverse kinematics */
        if(k & (1 << 5)) {
            data += bs + 4 + sizeof(float);
            n = data[0] | (data[1] << 8) | (data[2] << 16); data += 4;
            if(verbose > 2) printf("    IK %u\n", n);
            for(j = 0; j < n; j++) {
                data += bs;
                if(*data++) data += 6 * sizeof(float);
            }
        }
    }
    /* resolve orientations, we can't do this sooner because tail bone index might be a forward reference */
    /* also convert absolute positions to bone relative positions */
    if(verbose > 2) printf("  Bone Orientations\n");
    for(i = nb - 1; i >= 0; i--) {
        j = m3d->numvertex + 2 * (nv + i);
        if(m3d->bone[m3d->numbone + i].ori != -1U) {
            if(m3d->bone[m3d->numbone + i].numweight && m3d->bone[m3d->numbone + i].ori) {
                m3d->bone[m3d->numbone + i].numweight = 0;
                v = &m3d->vertex[m3d->bone[m3d->bone[m3d->numbone + i].ori].pos];
            } else
                v = &m3d->vertex[m3d->bone[m3d->numbone + i].ori];
            /* head in m3d->vertex[j], tail in v, convert to rotation quaternion */
            _m3d_euler_to_quat(v->x - m3d->vertex[j].x, v->y - m3d->vertex[j].y, v->z - m3d->vertex[j].z, &m3d->vertex[j + 1]);
        } else { m3d->vertex[j + 1].x = m3d->vertex[j + 1].y = m3d->vertex[j + 1].z = 0.0f; m3d->vertex[j + 1].w = 1.0f; }
        m3d->vertex[j + 1].skinid = -2U;
        m3d->vertex[j + 1].type = VT_QUATERN;
        m3d->bone[m3d->numbone + i].ori = j + 1;
/*
        if(m3d->bone[m3d->numbone + i].parent != -1U) {
            v = &m3d->vertex[m3d->bone[m3d->bone[m3d->numbone + i].parent].pos];
            m3d->vertex[j].x -= v->x;
            m3d->vertex[j].y -= v->y;
            m3d->vertex[j].z -= v->z;
            m3d->vertex[j].type = VT_RELATIVE;
        }
*/
    }

    m3d->numvertex += 2 * (nv + nb);
    m3d->numtexture += nt;
    m3d->numtmap += nv;
    m3d->numskin += nv;
    m3d->numface += nf;
    m3d->numbone += nb;
    m3d->nummaterial += nm;
    return m3d;
}
