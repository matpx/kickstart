/*
 * Model 3D voxel-only reader / writer
 * https://bztsrc.gitlab.io/model3d
 *
 * Copyright (C) 2023 bzt (bztsrc@gitlab), MIT license
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL ANY
 * DEVELOPER OR DISTRIBUTOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
 * IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

/*
 * This is the same code as the src/formats/m3d.c Goxel plugin, except I have removed
 * all the Goxel API, so this version can be, and is MIT licensed (Goxel is GPL). It
 * might be a little more readable as well, good start to implement your own voxel-only
 * M3D importer / exporter. Note that this only handles static models; rigged and animated
 * voxel models are only supported by the M3D SDK. The exporter is also limited to 2^16-2
 * different voxel types, however the importer supports up to 2^32-2, just like the SDK.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

/* special values in voxlayers[].data */
#define VOX_AIR     -1U     /* this means that voxel is empty (air), so keep original world's voxel on load */
#define VOX_CLEAR   -2U     /* this means that original world's voxel must be set to empty (air) */

/* -------- abstract structure to store voxel types and layers. Replace this with your own implementation -------- */

typedef struct {
    uint32_t diffuse;       /* diffuse color (packed RGBA) */
    uint32_t ambient;       /* ambient color (packed RGBA) */
    uint32_t specular;      /* specular color (packed RGBA) */
    uint32_t emission;      /* emission color (packed RGBA) */
    uint32_t transmis;      /* transmission color (packed RGBA) */
    float emisexp;          /* emission exponent */
    float metallic;         /* metallic factor */
    float roughness;        /* roughness factor */
    char *name;             /* material name (optional) */
} m3d_voxel_material_t;
/* array to store materials */
m3d_voxel_material_t *voxmaterials = NULL;
int numvoxmaterials;

typedef struct {
    uint32_t color;         /* voxel color if no material is assigned */
    uint32_t materialid;    /* material index, -1U if there's none */
    char *name;             /* name of the voxel type (optional) */
} m3d_voxel_type_t;
/* array to store voxel types */
m3d_voxel_type_t *voxtypes = NULL;
int numvoxtypes;

typedef struct {
    int x, y, z;            /* position of the layer */
    int w, h, d;            /* dimensions */
    uint32_t *data;         /* layer data, voxtypes indeces */
} m3d_voxel_layer_t;
/* array to store voxel data */
m3d_voxel_layer_t *voxlayers = NULL;
int numvoxlayers;

/* -------- end of abstract structures -------- */

// get stbi_zlib_compress(), you can use the official zlib library as well
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// get stbi_zlib_decompress(), you can use the official zlib library as well
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_GIF
#include "stb_image.h"

// material types
enum {
    m3dp_Kd = 0,                /* scalar display properties */
    m3dp_Ka,
    m3dp_Ks,
    m3dp_Ns,
    m3dp_Ke,
    m3dp_Tf,
    m3dp_Km,
    m3dp_d,
    m3dp_il,

    m3dp_Pr = 64,               /* scalar physical properties */
    m3dp_Pm,
    m3dp_Ps,
    m3dp_Ni,
    m3dp_Nt,

    m3dp_map_Kd = 128,          /* textured display map properties */
    m3dp_map_Ka,
    m3dp_map_Ks,
    m3dp_map_Ns,
    m3dp_map_Ke,
    m3dp_map_Tf,
    m3dp_map_Km, /* bump map */
    m3dp_map_D,
    m3dp_map_N,  /* normal map */

    m3dp_map_Pr = 192,          /* textured physical map properties */
    m3dp_map_Pm,
    m3dp_map_Ps,
    m3dp_map_Ni,
    m3dp_map_Nt
};

/*** m3d reader ***/

/**
 * Import a Model 3D binary into the abstract structs
 */
static int import_as_m3d(const char *path)
{
    FILE *file;
    long int size;
    unsigned char *data, *buf, *s, *e, *chunk;
    char *strtbl, *n;
    int i, j, k, l, ci_s, si_s, sk_s, vd_s, vp_s;
    int sx, sy, sz, px, py, pz;
    uint32_t *cmap = NULL, C;

    // read in file data
    file = fopen(path, "rb");
    if(!file) {
ferr:   fprintf(stderr, "Cannot load from %s: %s", path, strerror(errno));
        return -1;
    }
    fseek(file, 0, SEEK_END);
    size = (long int)ftell(file);
    if(size < 8) { fclose(file); goto ferr; }
    fseek(file, 0, SEEK_SET);
    data = (unsigned char*)malloc(size);
    if(!data) {
        fclose(file);
merr:   fprintf(stderr, "Memory allocation error %s: %ld bytes", path, size);
        return -1;
    }
    if(fread(data, 1, size, file) != size) {
        fclose(file);
        free(data);
        goto ferr;
    }
    fclose(file);

    // check magic and uncompress
    if(memcmp(data, "3DMO", 4)) {
        free(data);
        fprintf(stderr, "Bad file format %s", path);
        return -1;
    }

    // skip over file header
    s = data + 8;
    e = data + size;
    size -= 8;
    // skip over optional preview chunk if it exists
    if(!memcmp(s, "PRVW", 4)) {
        size -= *((uint32_t*)(s + 4));
        s += *((uint32_t*)(s + 4));
    }
    // check if it's a header chunk, if not, then file is stream compressed
    if(memcmp(s, "HEAD", 4)) {
        buf = (unsigned char *)stbi_zlib_decode_malloc_guesssize_headerflag(
            (const char*)s, size, 4096, &l, 1);
        free(data);
        if(!buf || !l || memcmp(buf, "HEAD", 4)) {
            if(buf) free(buf);
            fprintf(stderr, "Uncompression error %s", path);
            return -1;
        }
        data = s = buf;
        e = data + l;
    }
    // decode item sizes from model header
    strtbl = (char*)s + 16;             // string table
    si_s = 1 << ((s[12] >> 4) & 3);     // string index (offset) size
    ci_s = 1 << ((s[12] >> 6) & 3);     // color index size
    sk_s = 1 << ((s[13] >> 6) & 3);     // skin index size
    vd_s = 1 << ((s[14] >> 6) & 3);     // voxel data size
    vp_s = 1 << ((s[15] >> 0) & 3);     // voxel position size
    // some are optional. To simplify calculations, use 0 for these
    if(ci_s == 8) ci_s = 0;
    if(sk_s == 8) sk_s = 0;
    if(si_s == 8) si_s = 0;
    // voxel type and position size must be specified with this importer
    if(vd_s == 8 || vp_s == 8) {
        free(data);
        fprintf(stderr, "Bad model header %s", path);
        return -1;
    }

    // just to be on the safe side, free buffers in case they are already allocated (they shouldn't be)
    if(voxtypes) { free(voxtypes); voxtypes = NULL; }
    if(voxlayers) { free(voxlayers); voxlayers = NULL; }
    if(voxmaterials) { free(voxmaterials); voxmaterials = NULL; }
    numvoxtypes = numvoxlayers = numvoxmaterials = 0;

    // iterate on chunks, simply skip those we don't care about
    for(chunk = s; chunk < e && memcmp(chunk, "OMD3", 4);) {
        // decode chunk header and adjust to the next chunk
        s = chunk;
        l = *((uint32_t*)(chunk + 4));
        chunk += l;
        if(l < 8 || chunk >= e) break;
        l -= 8;
        // if it's a color map (not saved, but m3d files might have it)
        if(!memcmp(s, "CMAP", 4)) { cmap = (uint32_t*)(s + 8); } else
        // material chunk
        if(!memcmp(s, "MTRL", 4)) {
            // allocate memory for a new material
            voxmaterials = (m3d_voxel_material_t*)realloc(voxmaterials, (numvoxmaterials + 1) * sizeof(m3d_voxel_material_t));
            if(!voxmaterials) goto merr;
            memset(&voxmaterials[numvoxmaterials], 0, sizeof(m3d_voxel_material_t));
            // get material's name
            s += 8; n = NULL;
            switch(si_s) {
                case 1: n = strtbl + s[0]; break;
                case 2: n = strtbl + *((uint16_t*)s); break;
                case 4: n = strtbl + *((uint32_t*)s); break;
            }
            s += si_s;
            if(n && *n)
                voxmaterials[numvoxmaterials].name = strdup(n);
            // parse material properties
            while(s < chunk) {
                switch(s[0]) {
                    /* emission exponent, float */
                    case m3dp_Ns:
                        memcpy(&voxmaterials[numvoxmaterials].emisexp, s + 1, 4);
                        s += 5;
                    break;
                    /* metallic, float */
                    case m3dp_Pm:
                        memcpy(&voxmaterials[numvoxmaterials].metallic, s + 1, 4);
                        s += 5;
                    break;
                    /* roughness, float */
                    case m3dp_Pr:
                        memcpy(&voxmaterials[numvoxmaterials].roughness, s + 1, 4);
                        s += 5;
                    break;
                    /* get various colors */
                    case m3dp_Kd: case m3dp_Ka: case m3dp_Ks: case m3dp_Ke: case m3dp_Tf:
                        /* decode color into a packed RGBA pixel */
                        j = *s++; C = 0;
                        switch(ci_s) {
                            case 1:  C = cmap ? cmap[s[0]] : 0; s++; break;
                            case 2:  C = cmap ? cmap[*((uint16_t*)s)] : 0; s += 2; break;
                            case 4:  C = *((uint32_t*)s); s += 4; break;
                        }
                        /* store the decoded color in the appropriate material field */
                        switch(j) {
                            case m3dp_Kd: voxmaterials[numvoxmaterials].diffuse = C; break;
                            case m3dp_Ka: voxmaterials[numvoxmaterials].ambient = C; break;
                            case m3dp_Ks: voxmaterials[numvoxmaterials].specular = C; break;
                            case m3dp_Ke: voxmaterials[numvoxmaterials].emission = C; break;
                            case m3dp_Tf: voxmaterials[numvoxmaterials].transmis = C; break;
                        }
                    break;
                    /* skip over properties we don't care about, most notably texture identifiers */
                    case m3dp_il: s += 2; break;
                    default: s += 1 + (s[0] >= 128 ? si_s : 4); break;
                }
            }
            numvoxmaterials++;
        } else
        // voxel types
        if(!memcmp(s, "VOXT", 4)) {
            s += 8;
            // this will get an upper bound of number of types
            numvoxtypes = l / (ci_s + si_s + 3 + sk_s);
            voxtypes = (m3d_voxel_type_t*)realloc(voxtypes, numvoxtypes * sizeof(m3d_voxel_type_t));
            if(!voxtypes) goto merr;
            memset(voxtypes, 0, numvoxtypes * sizeof(m3d_voxel_material_t));
            // get voxel types
            for(i = 0; i < numvoxtypes && s < chunk; i++) {
                // diffuse color (when there's no material, each voxel might use a different voxel color)
                C = 0;
                switch(ci_s) {
                    case 1:  C = cmap ? cmap[s[0]] : 0; s++; break;
                    case 2:  C = cmap ? cmap[*((uint16_t*)s)] : 0; s += 2; break;
                    case 4:  C = *((uint32_t*)s); s += 4; break;
                }
                voxtypes[i].color = C;
                // voxel type's name
                n = NULL;
                switch(si_s) {
                    case 1: n = strtbl + s[0]; break;
                    case 2: n = strtbl + *((uint16_t*)s); break;
                    case 4: n = strtbl + *((uint32_t*)s); break;
                }
                s += si_s;
                // material index
                voxtypes[i].materialid = -1U;
                if(n && *n) {
                    voxtypes[i].name = strdup(n);
                    // if there's a material with the same name as the voxel type then set the materialid
                    for(j = 0; j < numvoxmaterials && strcmp(voxmaterials[j].name, n); j++);
                    if(j < numvoxmaterials)
                        voxtypes[i].materialid = j;
                }
                // skip over other additional attributes (rigging and such)
                s += 2;
                j = *s;
                s += 1 + sk_s + j * (2 + si_s);
            }
            // if we actually have less types than the upper bound, free the unused memory
            if(i != numvoxtypes) {
                numvoxtypes = i;
                voxtypes = (m3d_voxel_type_t *)realloc(voxtypes, numvoxtypes * sizeof(m3d_voxel_type_t));
                if(!voxtypes) goto merr;
            }
        } else
        // voxel data
        if(!memcmp(s, "VOXD", 4)) {
            voxlayers = (m3d_voxel_layer_t*)realloc(voxlayers, (numvoxlayers + 1) * sizeof(m3d_voxel_layer_t));
            if(!voxlayers) goto merr;
            memset(&voxlayers[numvoxlayers], 0, sizeof(m3d_voxel_layer_t));
            // layer name
            s += 8; n = NULL;
            switch(si_s) {
                case 1: n = strtbl + s[0]; break;
                case 2: n = strtbl + *((uint16_t*)s); break;
                case 4: n = strtbl + *((uint32_t*)s); break;
            }
            s += si_s;
            if(n && *n)
                voxlayers[numvoxlayers].name = strdup(n);
            // get layer dimensions
            px = py = pz = sx = sy = sz = 0;
            switch(vd_s) {
                case 1:
                    px = (int8_t)s[0]; py = (int8_t)s[1]; pz = (int8_t)s[2];
                    sx = (int8_t)s[3]; sy = (int8_t)s[4]; sz = (int8_t)s[5];
                    s += 6;
                break;
                case 2:
                    px = *((int16_t*)(s+0)); py = *((int16_t*)(s+2)); pz = *((int16_t*)(s+4));
                    sx = *((int16_t*)(s+6)); sy = *((int16_t*)(s+8)); sz = *((int16_t*)(s+10));
                    s += 12;
                break;
                case 4:
                    px = *((int32_t*)(s+0)); py = *((int32_t*)(s+4)); pz = *((int32_t*)(s+8));
                    sx = *((int32_t*)(s+12)); sy = *((int32_t*)(s+16)); sz = *((int32_t*)(s+20));
                    s += 24;
                break;
            }
            voxlayers[numvoxlayers].x = px;
            voxlayers[numvoxlayers].y = py;
            voxlayers[numvoxlayers].z = pz;
            voxlayers[numvoxlayers].w = sx;
            voxlayers[numvoxlayers].h = py;
            voxlayers[numvoxlayers].d = sz;
            voxlayers[numvoxlayers].data = (uint32_t *)malloc(sx * sy * sz * sizeof(uint32_t));
            if(!voxlayers[numvoxlayers].data) goto merr;
            // decompress RLE layer data
            for(s += 2, i = 0; s < chunk && i < sx * sy * sz;) {
                // get RLE packet length
                l = ((*s++) & 0x7F) + 1;
                // check packet's type
                if(s[-1] & 0x80) {
                    // get voxel type index
                    switch(vp_s) {
                        case 1: k = *s++; break;
                        case 2: k = *((uint16_t*)s); s += 2; break;
                        case 4: k = *((uint32_t*)s); s += 4; break;
                        default: k = -1; break;
                    }
                    // repeat it l times
                    for(j = 0; j < l; j++, i++)
                        voxlayers[numvoxlayers].data[i] = k;
                } else {
                    // each index in the l sized packet is a different voxel type
                    for(j = 0; j < l; j++, i++) {
                        // get voxel type index
                        switch(vp_s) {
                            case 1: k = *s++; break;
                            case 2: k = *((uint16_t*)s); s += 2; break;
                            case 4: k = *((uint32_t*)s); s += 4; break;
                            default: k = -1; break;
                        }
                        voxlayers[numvoxlayers].data[i] = k;
                    }
                }
            }
            numvoxlayers++;
        }
    }
    free(data);

    return 0;
}

/*** m3d writer ***/

/* string table stuff. */
typedef struct {
    char *data;     // concatenated, escape-safe string buffer with zero terminated UTF-8 strings
    int len, num;   // string table length and number of strings in table
    int *str;       // string table offsets
} m3d_strtable_t;

/* remove unsafe characters from identifiers (tab, newline, directory separators etc.) */
/* for the morelines argument, see M3D SDK documentation. */
char *m3d_safestr(const char *in, int morelines)
{
    char *out, *o, *i = (char*)in;
    int l;
    if(!in || !*in) {
        out = (char*)malloc(1);
        if(!out) return NULL;
        out[0] = 0;
    } else {
        for(o = (char*)in, l = 0; *o && ((morelines & 1) || (*o != '\r' && *o != '\n')) && l < 256; o++, l++);
        out = o = (char*)malloc(l+1);
        if(!out) return NULL;
        while(*i == ' ' || *i == '\t' || *i == '\r' || (morelines && *i == '\n')) i++;
        for(; *i && (morelines || (*i != '\r' && *i != '\n')); i++) {
            if(*i == '\r') continue;
            if(*i == '\n') {
                if(morelines >= 3 && o > out && *(o-1) == '\n') break;
                if(i > in && *(i-1) == '\n') continue;
                if(morelines & 1) {
                    if(morelines == 1) *o++ = '\r';
                    *o++ = '\n';
                } else
                    break;
            } else
            if(*i == ' ' || *i == '\t') {
                *o++ = morelines? ' ' : '_';
            } else
                *o++ = !morelines && (*i == '/' || *i == '\\') ? '_' : *i;
        }
        for(; o > out && (*(o-1) == ' ' || *(o-1) == '\t' || *(o-1) == '\r' || *(o-1) == '\n'); o--);
        *o = 0;
        out = (char*)realloc(out, (uintptr_t)o - (uintptr_t)out + 1);
    }
    return out;
}

/* add a string to string table (with de-duplication) */
void m3d_addstr(m3d_strtable_t *tbl, const char *str)
{
    int i, l;
    char *safe = m3d_safestr(str, 0);

    if(!safe) return;
    // first 4 strings are mandatory: title, license, author, comment (in this order. If not specified, must be saved as 4 zeros)
    if(tbl->num > 3) {
        for(i = 4; i < tbl->num && tbl->data && strcmp(tbl->data + tbl->str[i], safe); i++);
        if(tbl->data && i < tbl->num) { free(safe); return; }
    }
    l = strlen(safe) + 1;
    tbl->data = (char*)realloc(tbl->data, tbl->len + l);
    if(!tbl->data) { free(safe); return; }
    i = tbl->num++;
    tbl->str = (int*)realloc(tbl->str, tbl->num * sizeof(int));
    if(!tbl->str) { free(safe); return; }
    tbl->str[i] = tbl->len;
    memcpy(tbl->data + tbl->len, safe, l);
    tbl->len += l;
    free(safe);
}

/* return string table offset for a string */
int m3d_getstr(m3d_strtable_t *tbl, const char *str)
{
    int i;
    char *safe;

    if(!tbl || !tbl->data || !tbl->str || !str || !*str) return -1;
    safe = m3d_safestr(str, 0);
    if(!safe) return -1;
    for(i = 0; i < tbl->num && strcmp(tbl->data + tbl->str[i], safe); i++);
    free(safe);
    return i < tbl->num ? tbl->str[i] : -1;
}

/* free string table */
void m3d_freestr(m3d_strtable_t *tbl)
{
    if(tbl->data) free(tbl->data);
    if(tbl->str) free(tbl->str);
}

/**
 * Export the abstract structs into a Model 3D binary
 */
static int export_as_m3d(const char *path)
{
    FILE *out;
    uint8_t *data = NULL, *comp = NULL, *chunk, *ptr;
    uint16_t *blk;
    uint32_t *intp, *sizp;
    m3d_strtable_t str;
    int size = 0, i, j, k, l, m, n, o;

    // the M3D format supports 2^32-2 voxel types, and the importer works with that many, however this exporter
    // unconditionally saves 16 bit voxel data, therefore limited to 65534 different voxel types (reasonable).
    // 2 is substracted because of VOX_AIR (0xffff) and VOX_CLEAR (0xfffe)
    if(numvoxtypes >= 65534) {
        fprintf(stderr, "Cannot save, too many palette entries (max 65534) %s: %d", path, numvoxtypes);
        return -1;
    }

    // write out file
    out = fopen(path, "wb");
    if (!out) {
        fprintf(stderr, "Cannot save to %s: %s", path, strerror(errno));
        return -1;
    }
    fwrite("3DMO", 4, 1, out);

    // construct the string table
    memset(&str, 0, sizeof(m3d_strtable_t));
    // these are not stored by goxel, but we must save them
    m3d_addstr(&str, "");   // title, model name
    m3d_addstr(&str, "");   // license
    m3d_addstr(&str, "");   // author
    m3d_addstr(&str, "");   // comment

    // iterate through materials and add their names to string table
    for(i = 0; i < numvoxmaterials; i++)
        m3d_addstr(&str, voxmaterials[i].name);

    // iterate through voxel types and add their names to string table (table is de-duplicated in case they match material name)
    for(i = 0; i < numvoxtypes; i++)
        m3d_addstr(&str, voxtypes[i].name);

    // iterate through layers and add their names to string table
    for(i = 0; i < numvoxlayers; i++)
        m3d_addstr(&str, voxlayers[i].name);

    size = 16 + str.len +           /* header size */
        8 + numvoxtypes * 11 +      /* voxel type chunk size */
        numvoxmaterials * 64;       /* total size of material chunks */
        /* voxel data chunk reallocated and size calculated dynamically */
    data = calloc(size, 1);
    if(!data) {
merr:   fprintf(stderr, "Memory allocation error %s: %ld bytes", path, size);
        return -1;
    }

    // construct model header
    intp = (uint32_t*)(data + 0);
    memcpy(data + 0, "HEAD", 4);    // chunk magic
    intp[1] = 16 + str.len;         // chunk size
    intp[2] = 0x3F800000;           // scale (1 SI meters)
    intp[3] = 0x018FCFA0;           // flags (all index sizes 32 bits, except voxel data, that's 16 bits)
    chunk = data + 16 + str.len;
    if(str.data)
        memcpy(data + 16, str.data, str.len);

    // materials
    for(i = 0; i < numvoxmaterials; i++) {
        intp = (uint32_t*)(chunk);
        memcpy(chunk, "MTRL", 4); chunk += 8;
        /* material name index (actually, byte offset within the string table) */
        j = m3d_getstr(&str, voxmaterials[i].name);
        memcpy(chunk, &j, 4); chunk += 4;
        /* diffuse color (transparency for glass effect is stored in its alpha channel) */
        if(voxmaterials[i].diffuse) {
            *chunk++ = m3dp_Kd;
            memcpy(chunk, &voxmaterials[i].diffuse, 4); chunk += 4;
        }
        /* ambient color */
        if(voxmaterials[i].ambient) {
            *chunk++ = m3dp_Ka;
            memcpy(chunk, &voxmaterials[i].ambient, 4); chunk += 4;
        }
        /* specular color */
        if(voxmaterials[i].specular) {
            *chunk++ = m3dp_Ks;
            memcpy(chunk, &voxmaterials[i].specular, 4); chunk += 4;
        }
        /* emission color */
        if(voxmaterials[i].emission) {
            *chunk++ = m3dp_Ke;
            memcpy(chunk, &voxmaterials[i].emission, 4); chunk += 4;
            *chunk++ = m3dp_Ns;
            memcpy(chunk, &voxmaterials[i].emisexp, 4); chunk += 4;
        }
        /* transmission color */
        if(voxmaterials[i].transmis) {
            *chunk++ = m3dp_Tf;
            memcpy(chunk, &voxmaterials[i].transmis, 4); chunk += 4;
        }
        /* metallic, float */
        if(voxmaterials[i].metallic != 0.0f) {
            *chunk++ = m3dp_Pm;
            memcpy(chunk, &voxmaterials[i].metallic, 4); chunk += 4;
        }
        /* roughness, float */
        if(voxmaterials[i].roughness != 0.0f) {
            *chunk++ = m3dp_Pr;
            memcpy(chunk, &voxmaterials[i].roughness, 4); chunk += 4;
        }
        /* set the size of the chunk in the chunk header */
        intp[1] = chunk - (uint8_t*)intp;
    }

    // voxel types chunk
    memcpy(chunk, "VOXT", 4);
    i = 8 + 11 * numvoxtypes; memcpy(chunk + 4, &i, 4);
    chunk += 8;
    for(i = 0; i < numvoxtypes; i++, chunk += 11) {
        /* diffuse voxel color (should be used when there's no material associated) */
        memcpy(chunk, &voxtypes[i].color, 4);
        /* if we have a materialid, then use the material's name, otherwise fallback to voxel type name */
        j = m3d_getstr(&str, voxtypes[i].materialid != -1U ? voxmaterials[voxtypes[i].materialid].name : voxtypes[i].name);
        memcpy(chunk + 4, &j, 4);
    }

    // because we've allocated memory for the worst case, now recalculate how much memory we've actually used so far
    size = (uintptr_t)chunk - (uintptr_t)data;

    // iterate through layers to get voxel data chunks
    for(i = 0; i < numvoxlayers; i++) {
        n = voxtypes[i].w * voxtypes[i].h * voxtypes[i].d * sizeof(uint16_t);
        // allocate memory for the worst case, when every voxel is a packet (38: min voxd chunk length)
        chunk -= (uintptr_t)data;
        data = realloc(data, size + 38 + n * 3);
        if(!data) goto merr;
        chunk += (uintptr_t)data;
        /* set pointer to the start of the new chunk, and write chunk magic */
        intp = (uint32_t*)(chunk);
        memset(chunk, 0, 38);
        memcpy(chunk, "VOXD", 4);
        /* voxel layer name index */
        j = m3d_getstr(&str, voxlayers[i].name);
        memcpy(chunk + 8, &j, 4); chunk += 4;
        /* save position and dimensions */
        memcpy(chunk + 12, &voxlayers[i].x, 4);
        memcpy(chunk + 16, &voxlayers[i].y, 4);
        memcpy(chunk + 20, &voxlayers[i].z, 4);
        memcpy(chunk + 24, &voxlayers[i].w, 4);
        memcpy(chunk + 28, &voxlayers[i].h, 4);
        memcpy(chunk + 32, &voxlayers[i].d, 4);
        // RLE compression
        ptr = chunk + 38;
        k = o = 0; ptr[o++] = 0;
        for(m = 0; m < n; i++) {
            for(l = 1; l < 128 && m + l < n && voxlayers[i].data[m] == voxlayers[i].data[m + l]; l++);
            if(l > 1) {
                l--;
                if(ptr[k]) { ptr[k]--; ptr[o++] = 0x80 | l; }
                else ptr[k] = 0x80 | l;
                memcpy(ptr + o, &voxlayers[i].data[m], 2);
                o += 2;
                k = o; ptr[o++] = 0;
                m += l;
                continue;
            }
            ptr[k]++;
            memcpy(ptr + o, &voxlayers[i].data[m], 2);
            o += 2;
            if(ptr[k] > 127) { ptr[k]--; k = o; ptr[o++] = 0; }
        }
        if(!(ptr[k] & 0x80)) { if(ptr[k]) ptr[k]--; else o--; }
        intp[1] = ptr + o - chunk;  // chunk size
        size += intp[1];
        chunk = data + size;
    }
    m3d_freestr(&str);

    // end chunk
    data = realloc(data, size + 4);
    memcpy(data + size, "OMD3", 4);
    size += 4;

    // compress payload
    comp = stbi_zlib_compress(data, size, &i, 9);
    if(comp && i) {
        free(data);
        data = comp;
        size = i;
    }

    // write out compressed size (plus file header's size, including file magic and this size field itself)
    i = size + 8;
    fwrite(&i, 1, sizeof(uint32_t), out);

    // write out compressed chunks
    if(data) {
        fwrite(data, 1, size, out);
        free(data);
    }
    fclose(out);
    return 0;
}
