// SPDX-License-Identifier: MIT
#include "td_edge_table.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// build pipeline: weld -> pair -> crease. everything transient is qsort over
// small POD records — no STL, no exceptions, no recursion beyond libc's.

namespace {

// weld record: a vertex's canonicalized position bits plus its index. sorting
// groups exact-position duplicates into runs; the run's lowest index becomes
// every member's canonical index.
struct TD_WeldRec {
  uint8_t key[12];  // x, y, z float bits, -0.0f collapsed to +0.0f
  uint16_t idx;
};

int tdWeldCmp(const void *a, const void *b) {
  const TD_WeldRec *ra = (const TD_WeldRec *)a, *rb = (const TD_WeldRec *)b;
  int c = memcmp(ra->key, rb->key, sizeof(ra->key));
  if (c != 0) return c;
  return (int)ra->idx - (int)rb->idx;  // deterministic runs: lowest index first
}

// edge record: one directed loop edge keyed on its unordered welded
// endpoints. sorting groups the (up to two, in a manifold) slots sharing an
// edge; ties break on slot so pairing is deterministic ("first-come").
struct TD_EdgeRec {
  uint32_t key;   // (min(a,b) << 16) | max(a,b), welded endpoint indices
  uint16_t face;  // owning mesh-local face
  uint16_t slot;  // faceVerts[] index of the edge's first endpoint
};

int tdEdgeCmp(const void *a, const void *b) {
  const TD_EdgeRec *ra = (const TD_EdgeRec *)a, *rb = (const TD_EdgeRec *)b;
  if (ra->key != rb->key) return ra->key < rb->key ? -1 : 1;
  return (int)ra->slot - (int)rb->slot;
}

inline void tdKeyFloat(uint8_t *out, float v) {
  float c = v + 0.0f;  // -0.0f + 0.0f == +0.0f: value-equality on the one
                       // representable ambiguity, converter parity
  memcpy(out, &c, sizeof(c));
}

// unit face normal by Newell's method over the polygon loop, with the
// converter's fan-cross fallback for the pathological cancelling non-planar
// case. a still-degenerate face gets (0, 0, 0) — its dots read 0 and the
// crease compare degrades instead of dividing by zero.
void tdFaceNormal(const TD_Mesh &m, uint16_t f, float out[3]) {
  const uint16_t ls = m.faces[f][2], lc = m.faces[f][3];
  float nx = 0.0f, ny = 0.0f, nz = 0.0f;
  for (uint16_t k = 0; k < lc; ++k) {
    const float *a = m.verts[m.faceVerts[ls + k]];
    const float *b = m.verts[m.faceVerts[ls + (uint16_t)((k + 1) % lc)]];
    nx += (a[1] - b[1]) * (a[2] + b[2]);
    ny += (a[2] - b[2]) * (a[0] + b[0]);
    nz += (a[0] - b[0]) * (a[1] + b[1]);
  }
  float len = sqrtf(nx * nx + ny * ny + nz * nz);
  if (len < 1e-12f) {
    nx = ny = nz = 0.0f;
    const float *v0 = m.verts[m.faceVerts[ls]];
    for (uint16_t k = 1; k + 1 < lc; ++k) {
      const float *va = m.verts[m.faceVerts[ls + k]];
      const float *vb = m.verts[m.faceVerts[ls + k + 1]];
      float e1[3] = {va[0] - v0[0], va[1] - v0[1], va[2] - v0[2]};
      float e2[3] = {vb[0] - v0[0], vb[1] - v0[1], vb[2] - v0[2]};
      nx += e1[1] * e2[2] - e1[2] * e2[1];
      ny += e1[2] * e2[0] - e1[0] * e2[2];
      nz += e1[0] * e2[1] - e1[1] * e2[0];
    }
    len = sqrtf(nx * nx + ny * ny + nz * nz);
  }
  if (len >= 1e-12f) {
    out[0] = nx / len;
    out[1] = ny / len;
    out[2] = nz / len;
  } else {
    out[0] = out[1] = out[2] = 0.0f;
  }
}

inline bool tdIsDecal(const TD_Mesh &m, uint16_t f) {
  return m.fattr != nullptr && m.fattr[f].host != TD_NO_HOST;
}

}  // namespace

bool TD_EdgeTable::build(const TD_Mesh &mesh, float creaseAngleRad) {
  release();
  const uint32_t nv = mesh.nverts;
  const uint32_t nf = mesh.nfaces;
  const uint32_t nl = mesh.faceVertCount;
  if (nv == 0 || nf == 0 || nl == 0) return true;  // empty mesh: empty table

  // the table block: u16 section then u8 section, 3 bytes per loop slot.
  uint8_t *base = (uint8_t *)malloc((size_t)3u * nl);
  if (!base) return false;
  uint16_t *oppSec = (uint16_t *)base;
  uint8_t *flagSec = base + 2u * nl;
  memset(oppSec, 0xFF, (size_t)2u * nl);  // all slots TD_EDGE_NONE
  memset(flagSec, 0, (size_t)nl);

  // transient scratch, freed before every return below.
  TD_WeldRec *wrec = (TD_WeldRec *)malloc((size_t)nv * sizeof(TD_WeldRec));
  uint16_t *weld = (uint16_t *)malloc((size_t)nv * sizeof(uint16_t));
  TD_EdgeRec *erec = (TD_EdgeRec *)malloc((size_t)nl * sizeof(TD_EdgeRec));
  float(*fnorm)[3] = (float(*)[3])malloc((size_t)nf * sizeof(float[3]));
  if (!wrec || !weld || !erec || !fnorm) {
    free(wrec);
    free(weld);
    free(erec);
    free(fnorm);
    free(base);
    return false;
  }

  // 1. weld by exact position.
  for (uint32_t i = 0; i < nv; ++i) {
    tdKeyFloat(wrec[i].key + 0, mesh.verts[i][0]);
    tdKeyFloat(wrec[i].key + 4, mesh.verts[i][1]);
    tdKeyFloat(wrec[i].key + 8, mesh.verts[i][2]);
    wrec[i].idx = (uint16_t)i;
  }
  qsort(wrec, nv, sizeof(TD_WeldRec), tdWeldCmp);
  uint16_t canon = wrec[0].idx;
  weld[wrec[0].idx] = canon;
  for (uint32_t i = 1; i < nv; ++i) {
    if (memcmp(wrec[i].key, wrec[i - 1].key, sizeof(wrec[i].key)) != 0) canon = wrec[i].idx;
    weld[wrec[i].idx] = canon;
  }

  // 2. collect directed edges (decal faces contribute nothing) and pair.
  uint32_t nEdges = 0;
  for (uint32_t f = 0; f < nf; ++f) {
    if (tdIsDecal(mesh, (uint16_t)f)) continue;
    const uint16_t ls = mesh.faces[f][2], lc = mesh.faces[f][3];
    for (uint16_t k = 0; k < lc; ++k) {
      const uint32_t a = weld[mesh.faceVerts[ls + k]];
      const uint32_t b = weld[mesh.faceVerts[ls + (uint16_t)((k + 1) % lc)]];
      const uint32_t lo = a < b ? a : b, hi = a < b ? b : a;
      erec[nEdges].key = (lo << 16) | hi;
      erec[nEdges].face = (uint16_t)f;
      erec[nEdges].slot = (uint16_t)(ls + k);
      ++nEdges;
    }
  }
  if (nEdges != 0) qsort(erec, nEdges, sizeof(TD_EdgeRec), tdEdgeCmp);

  // 3. per-face unit normals for the crease test.
  for (uint32_t f = 0; f < nf; ++f) tdFaceNormal(mesh, (uint16_t)f, fnorm[f]);
  const float cosCrease = cosf(creaseAngleRad);

  // walk runs of one edge key, pairing consecutive slots; an odd leftover is
  // a boundary edge.
  for (uint32_t i = 0; i < nEdges;) {
    uint32_t j = i;
    while (j < nEdges && erec[j].key == erec[i].key) ++j;
    uint32_t k = i;
    for (; k + 1 < j; k += 2) {
      const TD_EdgeRec &ea = erec[k], &eb = erec[k + 1];
      oppSec[ea.slot] = eb.face;
      oppSec[eb.slot] = ea.face;
      const float *na = fnorm[ea.face], *nb = fnorm[eb.face];
      const float d = na[0] * nb[0] + na[1] * nb[1] + na[2] * nb[2];
      if (d < cosCrease) {
        flagSec[ea.slot] |= TD_EDGE_CREASE;
        flagSec[eb.slot] |= TD_EDGE_CREASE;
      }
    }
    if (k < j) flagSec[erec[k].slot] |= TD_EDGE_BOUNDARY;
    i = j;
  }

  free(wrec);
  free(weld);
  free(erec);
  free(fnorm);

  opp = oppSec;
  flags = flagSec;
  nloop = (uint16_t)nl;
  return true;
}

void TD_EdgeTable::release() {
  // opp is the block base by construction; one free tears the table down.
  free(opp);
  opp = nullptr;
  flags = nullptr;
  nloop = 0;
}
