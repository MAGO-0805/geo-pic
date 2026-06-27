// 原创性：课程内容(路径追踪逻辑) + 参考NVIDIA CUDA Programming Guide(CUDA并行框架) + PBRT(BVH遍历)
#include "gpu_render.h"
#include "scene_parser.hpp"
#include "camera.hpp"
#include "group.hpp"
#include "sphere.hpp"
#include "triangle.hpp"
#include "mesh.hpp"
#include "transform.hpp"
#include "material.hpp"
#include "plane.hpp"
#include "bvh.hpp"
#include <cuda_runtime.h>
#include <cmath>
#include <cfloat>
#include <map>
#include <algorithm>
#include <functional>

using namespace std;

// ============ 场景展平 ============

GPUScene flattenScene(SceneParser &parser, Group *rootGroup) {
    GPUScene out;

    map<Material *, int> matMap;
    for (int i = 0; i < parser.getNumMaterials(); i++) {
        Material *m = parser.getMaterial(i);
        if (!matMap.count(m)) {
            int idx = (int)out.materials.size();
            matMap[m] = idx;
            GPUMaterial gm;
            gm.kd_r = m->getDiffuseColor().x();
            gm.kd_g = m->getDiffuseColor().y();
            gm.kd_b = m->getDiffuseColor().z();
            gm.atten_r = m->getAttenuationColor().x();
            gm.atten_g = m->getAttenuationColor().y();
            gm.atten_b = m->getAttenuationColor().z();
            gm.ior = m->getRefractiveIndex();
            gm.roughness = m->getRoughness();
            gm.dispersion = m->getDispersion();
            gm.hasFresnel = m->hasFresnel() ? 1 : 0;
            gm.F0_r = gm.F0_g = gm.F0_b = 0;
            if (m->isEmissive()) {
                gm.type = GPU_EMISSIVE;
                gm.F0_r = m->getEmission().x();
                gm.F0_g = m->getEmission().y();
                gm.F0_b = m->getEmission().z();
            } else {
                switch (m->getType()) {
                    case PHONG:    gm.type = GPU_DIFFUSE; break;
                    case REFLECTIVE: gm.type = GPU_REFLECTIVE; break;
                    case REFRACTIVE: gm.type = GPU_REFRACTIVE; break;
                    case GLOSSY:   gm.type = GPU_GLOSSY; break;
                    default:       gm.type = GPU_DIFFUSE; break;
                }
            }
            out.materials.push_back(gm);
        }
    }

    function<void(Object3D *, const Matrix4f &)> flatten;
    flatten = [&](Object3D *obj, const Matrix4f &accInv) {
        if (!obj) return;
        Material *mat = obj->getMaterial();
        int matId = mat ? matMap[mat] : -1;

        if (auto *g = dynamic_cast<Group *>(obj)) {
            for (int i = 0; i < g->getGroupSize(); i++)
                flatten(g->getChild(i), accInv);
        } else if (auto *t = dynamic_cast<Transform *>(obj)) {
            flatten(t->getChild(), t->getTransformInv() * accInv);
        } else if (auto *s = dynamic_cast<Sphere *>(obj)) {
            if (matId < 0) return;
            GPUSphere gs;
            Vector3f c = s->getCenter();
            float r = s->getRadius();
            Vector3f tc = transformPoint(accInv.inverse(), c);
            Matrix4f M = accInv.inverse();
            float sx = M.getCol(0).xyz().length();
            float sy = M.getCol(1).xyz().length();
            float sz = M.getCol(2).xyz().length();
            float sr = r * max(max(sx, sy), sz);
            gs.cx = tc.x(); gs.cy = tc.y(); gs.cz = tc.z();
            gs.r = sr;
            gs.mat_id = matId;
            out.spheres.push_back(gs);
        } else if (auto *tri = dynamic_cast<Triangle *>(obj)) {
            GPUTriangle gt;
            Vector3f v0 = transformPoint(accInv.inverse(), tri->getVertex(0));
            Vector3f v1 = transformPoint(accInv.inverse(), tri->getVertex(1));
            Vector3f v2 = transformPoint(accInv.inverse(), tri->getVertex(2));
            gt.v0x = v0.x(); gt.v0y = v0.y(); gt.v0z = v0.z();
            gt.v1x = v1.x(); gt.v1y = v1.y(); gt.v1z = v1.z();
            gt.v2x = v2.x(); gt.v2y = v2.y(); gt.v2z = v2.z();
            Vector3f fn = Vector3f::cross(v1-v0, v2-v0).normalized();
            gt.n0x = fn.x(); gt.n0y = fn.y(); gt.n0z = fn.z();
            gt.n1x = fn.x(); gt.n1y = fn.y(); gt.n1z = fn.z();
            gt.n2x = fn.x(); gt.n2y = fn.y(); gt.n2z = fn.z();
            gt.mat_id = matId;
            out.triangles.push_back(gt);
        } else if (auto *mesh = dynamic_cast<Mesh *>(obj)) {
            Matrix4f normalMat = accInv.transposed();
            for (size_t i = 0; i < mesh->t.size(); i++) {
                GPUTriangle gt;
                Vector3f v0 = transformPoint(accInv.inverse(), mesh->v[mesh->t[i][0]]);
                Vector3f v1 = transformPoint(accInv.inverse(), mesh->v[mesh->t[i][1]]);
                Vector3f v2 = transformPoint(accInv.inverse(), mesh->v[mesh->t[i][2]]);
                gt.v0x = v0.x(); gt.v0y = v0.y(); gt.v0z = v0.z();
                gt.v1x = v1.x(); gt.v1y = v1.y(); gt.v1z = v1.z();
                gt.v2x = v2.x(); gt.v2y = v2.y(); gt.v2z = v2.z();
                Vector3f vn0 = (normalMat * Vector4f(mesh->vn[mesh->t[i][0]], 0)).xyz().normalized();
                Vector3f vn1 = (normalMat * Vector4f(mesh->vn[mesh->t[i][1]], 0)).xyz().normalized();
                Vector3f vn2 = (normalMat * Vector4f(mesh->vn[mesh->t[i][2]], 0)).xyz().normalized();
                gt.n0x = vn0.x(); gt.n0y = vn0.y(); gt.n0z = vn0.z();
                gt.n1x = vn1.x(); gt.n1y = vn1.y(); gt.n1z = vn1.z();
                gt.n2x = vn2.x(); gt.n2y = vn2.y(); gt.n2z = vn2.z();
                gt.mat_id = matId;
                out.triangles.push_back(gt);
            }
        } else if (auto *plane = dynamic_cast<Plane *>(obj)) {
            if (matId < 0) return;
            Vector3f n = plane->getNormal();
            float D = plane->getD();
            Vector3f p0;
            if (fabs(n.x()) > 0.9f)      p0 = Vector3f(-D/n.x(), 0, 0);
            else if (fabs(n.y()) > 0.9f) p0 = Vector3f(0, -D/n.y(), 0);
            else                         p0 = Vector3f(0, 0, -D/n.z());
            Vector3f t1, t2;
            if (fabs(n.x()) > 0.9f)      { t1 = Vector3f(0,1,0); t2 = Vector3f(0,0,1); }
            else if (fabs(n.y()) > 0.9f) { t1 = Vector3f(1,0,0); t2 = Vector3f(0,0,1); }
            else                         { t1 = Vector3f(1,0,0); t2 = Vector3f(0,1,0); }
            float S = 20.0f;
            Vector3f v[4] = { p0+(-t1-t2)*S, p0+(t1-t2)*S, p0+(t1+t2)*S, p0+(-t1+t2)*S };
            GPUTriangle gt1, gt2;
            // gt1: v[0],v[1],v[2]; gt2: v[0],v[2],v[3]
            // 确保几何面法向量与 plane normal 同向（否则交换顶点翻转）
            if (Vector3f::dot(Vector3f::cross(v[1]-v[0], v[2]-v[0]), n) >= 0) {
                gt1.v0x=v[0].x();gt1.v0y=v[0].y();gt1.v0z=v[0].z();
                gt1.v1x=v[1].x();gt1.v1y=v[1].y();gt1.v1z=v[1].z();
                gt1.v2x=v[2].x();gt1.v2y=v[2].y();gt1.v2z=v[2].z();
            } else {
                gt1.v0x=v[0].x();gt1.v0y=v[0].y();gt1.v0z=v[0].z();
                gt1.v1x=v[2].x();gt1.v1y=v[2].y();gt1.v1z=v[2].z();
                gt1.v2x=v[1].x();gt1.v2y=v[1].y();gt1.v2z=v[1].z();
            }
            gt1.n0x=n.x();gt1.n0y=n.y();gt1.n0z=n.z();
            gt1.n1x=n.x();gt1.n1y=n.y();gt1.n1z=n.z();
            gt1.n2x=n.x();gt1.n2y=n.y();gt1.n2z=n.z(); gt1.mat_id=matId;
            if (Vector3f::dot(Vector3f::cross(v[2]-v[0], v[3]-v[0]), n) >= 0) {
                gt2.v0x=v[0].x();gt2.v0y=v[0].y();gt2.v0z=v[0].z();
                gt2.v1x=v[2].x();gt2.v1y=v[2].y();gt2.v1z=v[2].z();
                gt2.v2x=v[3].x();gt2.v2y=v[3].y();gt2.v2z=v[3].z();
            } else {
                gt2.v0x=v[0].x();gt2.v0y=v[0].y();gt2.v0z=v[0].z();
                gt2.v1x=v[3].x();gt2.v1y=v[3].y();gt2.v1z=v[3].z();
                gt2.v2x=v[2].x();gt2.v2y=v[2].y();gt2.v2z=v[2].z();
            }
            gt2.n0x=n.x();gt2.n0y=n.y();gt2.n0z=n.z();
            gt2.n1x=n.x();gt2.n1y=n.y();gt2.n1z=n.z();
            gt2.n2x=n.x();gt2.n2y=n.y();gt2.n2z=n.z(); gt2.mat_id=matId;
            out.triangles.push_back(gt1);
            out.triangles.push_back(gt2);
        }
    };

    flatten(rootGroup, Matrix4f::identity());

    Camera *cam = parser.getCamera();
    out.cam.cx = cam->getCenter().x(); out.cam.cy = cam->getCenter().y(); out.cam.cz = cam->getCenter().z();
    out.cam.dx = cam->getDirection().x(); out.cam.dy = cam->getDirection().y(); out.cam.dz = cam->getDirection().z();
    out.cam.ux = cam->getUp().x(); out.cam.uy = cam->getUp().y(); out.cam.uz = cam->getUp().z();
    out.cam.hx = cam->getHorizontal().x(); out.cam.hy = cam->getHorizontal().y(); out.cam.hz = cam->getHorizontal().z();
    auto *pcam = dynamic_cast<PerspectiveCamera *>(cam);
    out.cam.fx = pcam ? pcam->getFx() : 100.0f;
    out.cam.fy = pcam ? pcam->getFy() : 100.0f;
    out.cam.w = cam->getWidth(); out.cam.h = cam->getHeight();
    Vector3f bg = parser.getBackgroundColor();
    out.bg_r = bg.x(); out.bg_g = bg.y(); out.bg_b = bg.z();

    // === 构建 GPU BVH ===
    if (!out.triangles.empty()) {
        int nTri = (int)out.triangles.size();
        std::vector<Vector3f> centers(nTri), mins(nTri), maxs(nTri);
        for (int i = 0; i < nTri; i++) {
            const auto &tr = out.triangles[i];
            mins[i] = Vector3f(std::min(std::min(tr.v0x,tr.v1x),tr.v2x),
                               std::min(std::min(tr.v0y,tr.v1y),tr.v2y),
                               std::min(std::min(tr.v0z,tr.v1z),tr.v2z));
            maxs[i] = Vector3f(std::max(std::max(tr.v0x,tr.v1x),tr.v2x),
                               std::max(std::max(tr.v0y,tr.v1y),tr.v2y),
                               std::max(std::max(tr.v0z,tr.v1z),tr.v2z));
            centers[i] = (mins[i] + maxs[i]) * 0.5f;
        }
        BVH cpuBVH;
        cpuBVH.build(centers, mins, maxs);
        // 按 BVH 顺序重排 triangle 数组
        const auto &perm = cpuBVH.getPrimIndices();
        std::vector<GPUTriangle> reordered(nTri);
        for (int i = 0; i < nTri; i++) reordered[i] = out.triangles[perm[i]];
        out.triangles.swap(reordered);
        // 序列化
        const auto &nodes = cpuBVH.getNodes();
        for (const auto &n : nodes) {
            GPUBVHNode gn;
            gn.bmin_x = n.bbox_min.x(); gn.bmin_y = n.bbox_min.y(); gn.bmin_z = n.bbox_min.z();
            gn.bmax_x = n.bbox_max.x(); gn.bmax_y = n.bbox_max.y(); gn.bmax_z = n.bbox_max.z();
            if (n.right <= 0) {
                gn.left  = -(n.left + 1); gn.right = -n.right;
            } else {
                gn.left  = n.left; gn.right = n.right;
            }
            out.tri_bvh.push_back(gn);
        }
    }
    if (!out.spheres.empty()) {
        int nSph = (int)out.spheres.size();
        std::vector<Vector3f> centers(nSph), mins(nSph), maxs(nSph);
        for (int i = 0; i < nSph; i++) {
            centers[i] = Vector3f(out.spheres[i].cx, out.spheres[i].cy, out.spheres[i].cz);
            float r = out.spheres[i].r;
            mins[i] = centers[i] - Vector3f(r, r, r);
            maxs[i] = centers[i] + Vector3f(r, r, r);
        }
        BVH cpuBVH;
        cpuBVH.build(centers, mins, maxs);
        // 按 BVH 顺序重排 sphere 数组
        const auto &perm = cpuBVH.getPrimIndices();
        std::vector<GPUSphere> reordered(nSph);
        for (int i = 0; i < nSph; i++) reordered[i] = out.spheres[perm[i]];
        out.spheres.swap(reordered);
        // 序列化
        const auto &nodes = cpuBVH.getNodes();
        for (const auto &n : nodes) {
            GPUBVHNode gn;
            gn.bmin_x = n.bbox_min.x(); gn.bmin_y = n.bbox_min.y(); gn.bmin_z = n.bbox_min.z();
            gn.bmax_x = n.bbox_max.x(); gn.bmax_y = n.bbox_max.y(); gn.bmax_z = n.bbox_max.z();
            if (n.right <= 0) {
                gn.left  = -(n.left + 1);
                gn.right = -n.right;
            } else {
                gn.left  = n.left;
                gn.right = n.right;
            }
            out.sphere_bvh.push_back(gn);
        }
    }

    return out;
}

// ============ GPU RNG ============
__device__ unsigned int pcg_hash(unsigned int seed) {
    unsigned int state = seed * 747796405u + 2891336453u;
    unsigned int word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}
__device__ float gpu_randf(unsigned int &seed) {
    seed = pcg_hash(seed);
    return (float)(seed & 0xFFFFFF) / (float)0x1000000;
}

// ============ GPU 几何求交 ============
__device__ bool gpu_intersect_sphere(
    float ox,float oy,float oz,float dx,float dy,float dz,
    float cx,float cy,float cz,float r,
    float tmin,float &t,float &nx,float &ny,float &nz) {
    float ocx=ox-cx,ocy=oy-cy,ocz=oz-cz;
    float a=dx*dx+dy*dy+dz*dz;
    float b=2*(ocx*dx+ocy*dy+ocz*dz);
    float c=ocx*ocx+ocy*ocy+ocz*ocz-r*r;
    float disc=b*b-4*a*c;
    if(disc<0)return false;
    float sqrtd=sqrtf(disc);
    float t1=(-b-sqrtd)/(2*a),t2=(-b+sqrtd)/(2*a);
    float tt=t1;
    if(tt<tmin||tt>t){tt=t2;if(tt<tmin||tt>t)return false;}
    t=tt;
    nx=(ox+dx*t-cx)/r;ny=(oy+dy*t-cy)/r;nz=(oz+dz*t-cz)/r;
    if(dx*nx+dy*ny+dz*nz>0){nx=-nx;ny=-ny;nz=-nz;}
    return true;
}

__device__ bool gpu_intersect_triangle(
    float ox,float oy,float oz,float dx,float dy,float dz,
    float v0x,float v0y,float v0z,float v1x,float v1y,float v1z,float v2x,float v2y,float v2z,
    float n0x,float n0y,float n0z,float n1x,float n1y,float n1z,float n2x,float n2y,float n2z,
    float tmin,float &t,float &nx,float &ny,float &nz, int useSmooth) {
    float e1x=v0x-v1x,e1y=v0y-v1y,e1z=v0z-v1z;
    float e2x=v0x-v2x,e2y=v0y-v2y,e2z=v0z-v2z;
    float sx=v0x-ox,sy=v0y-oy,sz=v0z-oz;
    float det=dx*(e1y*e2z-e1z*e2y)+dy*(e1z*e2x-e1x*e2z)+dz*(e1x*e2y-e1y*e2x);
    if(fabsf(det)<1e-8f)return false;
    float invDet=1/det;
    float tt=(sx*(e1y*e2z-e1z*e2y)+sy*(e1z*e2x-e1x*e2z)+sz*(e1x*e2y-e1y*e2x))*invDet;
    float beta=(dx*(sy*e2z-sz*e2y)+dy*(sz*e2x-sx*e2z)+dz*(sx*e2y-sy*e2x))*invDet;
    float gamma=(dx*(e1y*sz-e1z*sy)+dy*(e1z*sx-e1x*sz)+dz*(e1x*sy-e1y*sx))*invDet;
    if(tt>tmin&&tt<t&&beta>=0&&gamma>=0&&beta+gamma<=1){
        t=tt;
        // 几何面法向量（顶点顺序已在 flattenScene 确保与顶点法向量同向）
        float fnx=e1y*e2z-e1z*e2y,fny=e1z*e2x-e1x*e2z,fnz=e1x*e2y-e1y*e2x;
        float fnlen=sqrtf(fnx*fnx+fny*fny+fnz*fnz);
        fnx/=fnlen;fny/=fnlen;fnz/=fnlen;
        if (useSmooth) {
            float alpha = 1.0f - beta - gamma;
            nx = alpha*n0x + beta*n1x + gamma*n2x;
            ny = alpha*n0y + beta*n1y + gamma*n2y;
            nz = alpha*n0z + beta*n1z + gamma*n2z;
        } else {
            nx=fnx;ny=fny;nz=fnz;
        }
        float len=sqrtf(nx*nx+ny*ny+nz*nz);
        nx/=len;ny/=len;nz/=len;
        // 用几何面法向量判断翻转（与面几何一致，不受插值偏移影响）
        if(dx*fnx+dy*fny+dz*fnz>0){nx=-nx;ny=-ny;nz=-nz;}
        return true;
    }
    return false;
}

// GPU BVH AABB test
__device__ bool gpu_bbox_test(
    float ox,float oy,float oz, float dx,float dy,float dz,
    float bmin_x,float bmin_y,float bmin_z,
    float bmax_x,float bmax_y,float bmax_z,
    float tmin, float tmax)
{
    float t0=tmin, t1=tmax;
    float inv, tNear, tFar;
    inv=1.0f/dx; tNear=(bmin_x-ox)*inv; tFar=(bmax_x-ox)*inv;
    if(tNear>tFar){float tmp=tNear;tNear=tFar;tFar=tmp;}
    t0=tNear>t0?tNear:t0; t1=tFar<t1?tFar:t1; if(t0>t1)return false;
    inv=1.0f/dy; tNear=(bmin_y-oy)*inv; tFar=(bmax_y-oy)*inv;
    if(tNear>tFar){float tmp=tNear;tNear=tFar;tFar=tmp;}
    t0=tNear>t0?tNear:t0; t1=tFar<t1?tFar:t1; if(t0>t1)return false;
    inv=1.0f/dz; tNear=(bmin_z-oz)*inv; tFar=(bmax_z-oz)*inv;
    if(tNear>tFar){float tmp=tNear;tNear=tFar;tFar=tmp;}
    t0=tNear>t0?tNear:t0; t1=tFar<t1?tFar:t1; if(t0>t1)return false;
    return true;
}

__device__ bool gpu_scene_intersect(
    const GPUSphere*spheres,int nS,const GPUTriangle*tris,int nT,
    const GPUBVHNode*sphereBVH,const GPUBVHNode*triBVH,
    float ox,float oy,float oz,float dx,float dy,float dz,
    float tmin,float &t,float &nx,float &ny,float &nz,int &mid, int useSmooth)
{
    bool hit=false;t=1e38f;

    // Sphere BVH
    if(sphereBVH!=nullptr){
        int stack[32]; int sp=0; stack[sp++]=0;
        while(sp>0){
            int ni=stack[--sp];
            const GPUBVHNode &node=sphereBVH[ni];
            if(!gpu_bbox_test(ox,oy,oz,dx,dy,dz,node.bmin_x,node.bmin_y,node.bmin_z,node.bmax_x,node.bmax_y,node.bmax_z,tmin,t))continue;
            if(node.left<0){ // leaf: left = -(first+1)
                int first=-(node.left+1);
                for(int i=0;i<node.right;i++){
                    const GPUSphere &s=spheres[first+i];
                    float hnx,hny,hnz;
                    if(gpu_intersect_sphere(ox,oy,oz,dx,dy,dz,s.cx,s.cy,s.cz,s.r,tmin,t,hnx,hny,hnz))
                        {nx=hnx;ny=hny;nz=hnz;mid=s.mat_id;hit=true;}
                }
            }else{ stack[sp++]=node.right; stack[sp++]=node.left; }
        }
    }else{
        for(int i=0;i<nS;i++){
            float hnx,hny,hnz;
            if(gpu_intersect_sphere(ox,oy,oz,dx,dy,dz,spheres[i].cx,spheres[i].cy,spheres[i].cz,spheres[i].r,tmin,t,hnx,hny,hnz))
                {nx=hnx;ny=hny;nz=hnz;mid=spheres[i].mat_id;hit=true;}
        }
    }

    // Triangle BVH
    if(triBVH!=nullptr){
        int stack[64]; int sp=0; stack[sp++]=0;
        while(sp>0){
            int ni=stack[--sp];
            const GPUBVHNode &node=triBVH[ni];
            if(!gpu_bbox_test(ox,oy,oz,dx,dy,dz,node.bmin_x,node.bmin_y,node.bmin_z,node.bmax_x,node.bmax_y,node.bmax_z,tmin,t))continue;
            if(node.left<0){ // leaf
                int first=-(node.left+1);
                for(int i=0;i<node.right;i++){
                    const GPUTriangle &tr=tris[first+i];
                    float hnx,hny,hnz;
                    if(gpu_intersect_triangle(ox,oy,oz,dx,dy,dz,tr.v0x,tr.v0y,tr.v0z,tr.v1x,tr.v1y,tr.v1z,tr.v2x,tr.v2y,tr.v2z,
                        tr.n0x,tr.n0y,tr.n0z,tr.n1x,tr.n1y,tr.n1z,tr.n2x,tr.n2y,tr.n2z,tmin,t,hnx,hny,hnz,useSmooth))
                        {nx=hnx;ny=hny;nz=hnz;mid=tr.mat_id;hit=true;}
                }
            }else{ stack[sp++]=node.right; stack[sp++]=node.left; }
        }
    }else{
        for(int i=0;i<nT;i++){
            const GPUTriangle&tr=tris[i];float hnx,hny,hnz;
            if(gpu_intersect_triangle(ox,oy,oz,dx,dy,dz,tr.v0x,tr.v0y,tr.v0z,tr.v1x,tr.v1y,tr.v1z,tr.v2x,tr.v2y,tr.v2z,
                tr.n0x,tr.n0y,tr.n0z,tr.n1x,tr.n1y,tr.n1z,tr.n2x,tr.n2y,tr.n2z,tmin,t,hnx,hny,hnz,useSmooth))
                {nx=hnx;ny=hny;nz=hnz;mid=tr.mat_id;hit=true;}
        }
    }
    return hit;
}

// ============ GPU 常量 ============
const int MAX_DEPTH_GPU = 10;
const int RR_DEPTH_GPU = 3;
const float EPS_GPU = 0.001f;

// ============ GPU 工具函数 ============
__device__ inline float3 make_float3_(float x, float y, float z) { float3 v; v.x=x; v.y=y; v.z=z; return v; }
__device__ inline float3 operator+(float3 a, float3 b) { return make_float3_(a.x+b.x, a.y+b.y, a.z+b.z); }
__device__ inline float3 operator-(float3 a, float3 b) { return make_float3_(a.x-b.x, a.y-b.y, a.z-b.z); }
__device__ inline float3 operator*(float3 a, float s) { return make_float3_(a.x*s, a.y*s, a.z*s); }
__device__ inline float3 operator*(float s, float3 a) { return a*s; }
__device__ inline float3 operator*(float3 a, float3 b) { return make_float3_(a.x*b.x, a.y*b.y, a.z*b.z); }
__device__ inline float3 operator/(float3 a, float s) { return make_float3_(a.x/s, a.y/s, a.z/s); }
__device__ inline float dot_f3(float3 a, float3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
__device__ inline float3 cross_f3(float3 a, float3 b) {
    return make_float3_(a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x);
}
__device__ inline float3 normalize_f3(float3 v) {
    float l = sqrtf(v.x*v.x+v.y*v.y+v.z*v.z);
    return make_float3_(v.x/l, v.y/l, v.z/l);
}
__device__ inline float length_f3(float3 v) { return sqrtf(v.x*v.x+v.y*v.y+v.z*v.z); }

__device__ float3 gpu_reflect(float3 I, float3 N) {
    return I - 2.0f * dot_f3(N, I) * N;
}

__device__ bool gpu_refract(float3 I, float3 N, float eta, float3 &T) {
    float cosI = -dot_f3(I, N);
    float sinT2 = eta * eta * (1.0f - cosI*cosI);
    if (sinT2 > 1.0f) return false;
    float cosT = sqrtf(1.0f - sinT2);
    T = eta * I + (eta*cosI - cosT) * N;
    return true;
}

__device__ float gpu_fresnel(float cosI, float ior) {
    float R0 = (ior-1)*(ior-1)/((ior+1)*(ior+1));
    return R0 + (1-R0)*powf(1-cosI, 5.0f);
}

// Glossy: sample GGX half-vector
__device__ float3 gpu_sample_ggx(float r1, float r2, float alpha, float3 N, float3 wo, float3 &wi_ret, float &pdf_ret) {
    float phi = 2.0f*M_PI*r1;
    float cosT = sqrtf((1-r2)/(1+(alpha*alpha-1)*r2));
    float sinT = sqrtf(fmaxf(0,1-cosT*cosT));
    float3 local = make_float3_(sinT*cosf(phi), sinT*sinf(phi), cosT);
    // tangent frame
    float3 T,B;
    if (fabsf(N.x)>0.9f) { T=make_float3_(0,N.z,-N.y); } else { T=make_float3_(N.y,-N.x,0); }
    T = normalize_f3(T); B = cross_f3(N, T);
    float3 h = normalize_f3(T*local.x + B*local.y + N*local.z);
    float dwh = dot_f3(wo, h);
    if (dwh < 0) h = h * (-1.0f);
    dwh = fmaxf(0, dot_f3(wo, h));
    wi_ret = normalize_f3(2*dwh*h - wo);
    float cosH = fmaxf(0, dot_f3(N, h));
    float a2 = alpha*alpha;
    float D = a2 / (M_PI * cosH*cosH*cosH*cosH * (a2 + (1-cosH*cosH)/(cosH*cosH)) * (a2 + (1-cosH*cosH)/(cosH*cosH)));
    pdf_ret = D * cosH / fmaxf(1e-6f, 4*dwh);
    return h;
}

__device__ float3 gpu_ggx_eval(float3 wo, float3 wi, float3 N, float3 kd, float3 F0, float alpha) {
    float cosWo = fmaxf(0, dot_f3(N, wo)), cosWi = fmaxf(0, dot_f3(N, wi));
    if (cosWo<=0||cosWi<=0) return make_float3_(0,0,0);
    float3 h = normalize_f3(wo + wi);
    float cosH = fmaxf(0, dot_f3(N, h));
    float a2 = alpha*alpha, t2 = (1-cosH*cosH)/(cosH*cosH);
    float D = a2/(M_PI*cosH*cosH*cosH*cosH*(a2+t2)*(a2+t2));
    auto G1 = [a2](float c) { float t=(1-c*c)/(c*c); return 2/(1+sqrtf(1+a2*t)); };
    float G = G1(cosWo)*G1(cosWi);
    float3 F = F0 + (make_float3_(1,1,1)-F0)*powf(1-cosH,5);
    float3 spec = D*G*F / fmaxf(1e-6f, 4*cosWo);
    return kd*cosWi/M_PI + spec;
}

__device__ float3 gpu_cosine_sample_dir(float r1, float r2, float3 N) {
    float phi = 2*M_PI*r1;
    float cosT=sqrtf(r2), sinT=sqrtf(1-r2);
    float3 T,B;
    if(fabsf(N.x)>0.9f){T=make_float3_(0,N.z,-N.y);}else{T=make_float3_(N.y,-N.x,0);}
    T=normalize_f3(T); B=cross_f3(N,T);
    return normalize_f3(T*(sinT*cosf(phi)) + B*(sinT*sinf(phi)) + N*cosT);
}

// ============ 路径追踪核函数 ============

__global__ void path_trace_kernel(
    const GPUSphere *spheres, int nSpheres,
    const GPUTriangle *tris, int nTris,
    const GPUBVHNode *sphereBVH, const GPUBVHNode *triBVH,
    const GPUMaterial *materials,
    int nEmissives, const int *emissiveIndices,
    GPUCamera cam, float bg_r, float bg_g, float bg_b,
    float *output, int samples, int mode, int useSmooth, int useFresnel)  // 0=brdf, 1=mis, 2=nee
{
    int px = blockIdx.x * blockDim.x + threadIdx.x;
    int py = blockIdx.y * blockDim.y + threadIdx.y;
    if (px >= cam.w || py >= cam.h) return;

    unsigned int seed = px * 1973 + py * 9277 + 2663;
    float rsum = 0, gsum = 0, bsum = 0;

    for (int s = 0; s < samples; s++) {
        float jx = gpu_randf(seed)-0.5f, jy = gpu_randf(seed)-0.5f;
        float cx = (px+jx-cam.w*0.5f)/cam.fx, cy = (py+jy-cam.h*0.5f)/cam.fy;
        float3 rd = normalize_f3(make_float3_(cx, cy, 1));
        float3 dir = make_float3_(
            cam.hx*rd.x+cam.ux*rd.y+cam.dx*rd.z,
            cam.hy*rd.x+cam.uy*rd.y+cam.dy*rd.z,
            cam.hz*rd.x+cam.uz*rd.y+cam.dz*rd.z);
        float3 o = make_float3_(cam.cx, cam.cy, cam.cz);
        float3 d = dir;
        float3 thr = make_float3_(1,1,1);
        float currentIOR = 1.0f;
        bool fromDelta = false;
        // BRDF 侧 MIS: 上一非 delta 顶点信息
        float3 prevHp, prevN, prevWo, prevThroughput;
        int prevType = -1;
        float prevKd_r=0,prevKd_g=0,prevKd_b=0, prevF0_r=0,prevF0_g=0,prevF0_b=0, prevRoughness=0;

        for (int depth = 0; depth < MAX_DEPTH_GPU; depth++) {
            if (depth > RR_DEPTH_GPU) {
                float p = fmaxf(thr.x, fmaxf(thr.y, thr.z));
                if (gpu_randf(seed) > p) break;
                thr = thr * (1.0f/p);
            }

            float t, nx, ny, nz; int mid;
            if (!gpu_scene_intersect(spheres,nSpheres,tris,nTris,sphereBVH,triBVH,o.x,o.y,o.z,d.x,d.y,d.z,EPS_GPU,t,nx,ny,nz,mid,useSmooth)) {
                rsum+=thr.x*bg_r; gsum+=thr.y*bg_g; bsum+=thr.z*bg_b; break;
            }

            const GPUMaterial &mat = materials[mid];
            float3 hp = o + d*t;
            float3 N = normalize_f3(make_float3_(nx,ny,nz));
            float3 wo = make_float3_(-d.x,-d.y,-d.z); // toward camera

            if (mat.type == GPU_EMISSIVE) {
                if (depth == 0 || mode == 0 || fromDelta) {
                    rsum+=thr.x*mat.F0_r; gsum+=thr.y*mat.F0_g; bsum+=thr.z*mat.F0_b;
                } else if (mode == 1 && prevType >= 0) {
                    // BRDF 侧 MIS: 上一非 delta 顶点弹中光源
                    float3 wi_prev = normalize_f3(hp - prevHp);
                    float pdf_brdf = 0, pdf_light = 0;
                    float3 brdf_val = make_float3_(0,0,0);
                    // 上一顶点的 BRDF pdf/eval
                    if (prevType == GPU_DIFFUSE) {
                        float c = fmaxf(0, dot_f3(prevN, wi_prev));
                        pdf_brdf = c / M_PI;
                        brdf_val = make_float3_(prevKd_r*c/M_PI, prevKd_g*c/M_PI, prevKd_b*c/M_PI);
                    } else if (prevType == GPU_GLOSSY) {
                        float3 kd = make_float3_(prevKd_r,prevKd_g,prevKd_b);
                        float3 F0 = make_float3_(prevF0_r,prevF0_g,prevF0_b);
                        brdf_val = gpu_ggx_eval(prevWo, wi_prev, prevN, kd, F0, prevRoughness);
                        float pD = (kd.x+kd.y+kd.z)/(kd.x+kd.y+kd.z+F0.x+F0.y+F0.z+1e-6f);
                        float pdfD = fmaxf(0,dot_f3(prevN,wi_prev))/M_PI;
                        float3 h_ = normalize_f3(prevWo+wi_prev);
                        float ch=fmaxf(0,dot_f3(prevN,h_)), dw=fmaxf(1e-6f,dot_f3(prevWo,h_));
                        float a2=prevRoughness*prevRoughness;
                        float D_=a2/(M_PI*ch*ch*ch*ch*(a2+(1-ch*ch)/(ch*ch))*(a2+(1-ch*ch)/(ch*ch)));
                        pdf_brdf = pD*pdfD + (1-pD)*D_*ch/(4*dw);
                    }
                    // 光源 pdf: 球体面积
                    float dist2 = (hp.x-prevHp.x)*(hp.x-prevHp.x)+(hp.y-prevHp.y)*(hp.y-prevHp.y)+(hp.z-prevHp.z)*(hp.z-prevHp.z);
                    float cLight = fabsf(dot_f3(N, wi_prev*(-1.0f)));
                    if (cLight > 0 && pdf_brdf > 1e-8f) {
                        for (int si=0;si<nSpheres;si++)if(spheres[si].mat_id==mid){
                            float area=4*M_PI*spheres[si].r*spheres[si].r;
                            pdf_light = (1.0f/area)*dist2/cLight; break;
                        }
                        float mis_w = (pdf_brdf*pdf_brdf)/(pdf_brdf*pdf_brdf+pdf_light*pdf_light+1e-6f);
                        float3 contrib = make_float3_(mat.F0_r,mat.F0_g,mat.F0_b)*brdf_val*(mis_w/pdf_brdf);
                        rsum+=prevThroughput.x*contrib.x; gsum+=prevThroughput.y*contrib.y; bsum+=prevThroughput.z*contrib.z;
                    }
                }
                break;
            }

            // NEE (mode 1=mis, 2=nee)
            if (mode >= 1 && mat.type != GPU_REFLECTIVE && mat.type != GPU_REFRACTIVE) {
                for (int ei = 0; ei < nEmissives; ei++) {
                    int eid = emissiveIndices[ei];
                    const GPUMaterial &em = materials[eid];
                    float3 lp, ln; float pdf_a = 0;
                    // find emissive geometry and sample
                    for (int si = 0; si < nSpheres; si++) {
                        if (spheres[si].mat_id == eid) {
                            float r1=gpu_randf(seed),r2=gpu_randf(seed);
                            float phi2=2*M_PI*r1, ct=1-2*r2, st=sqrtf(fmaxf(0,1-ct*ct));
                            float3 dir_s = make_float3_(st*cosf(phi2),st*sinf(phi2),ct);
                            lp = make_float3_(spheres[si].cx,spheres[si].cy,spheres[si].cz)+dir_s*spheres[si].r;
                            ln = dir_s;
                            pdf_a = 1.0f/(4*M_PI*spheres[si].r*spheres[si].r);
                            break;
                        }
                    }
                    if (pdf_a <= 0) continue;
                    float3 lwi = normalize_f3(lp - hp);
                    float dist2 = (lp.x-hp.x)*(lp.x-hp.x)+(lp.y-hp.y)*(lp.y-hp.y)+(lp.z-hp.z)*(lp.z-hp.z);
                    float cLight = fabsf(dot_f3(ln, lwi*(-1.0f)));
                    if (cLight < 1e-6f) continue;
                    float pdf_w = pdf_a * dist2 / cLight;
                    float cosRay = fmaxf(0, dot_f3(N, lwi));
                    if (cosRay <= 0) continue;
                    // shadow ray
                    float st, snx,sny,snz; int smid;
                    float maxT = sqrtf(dist2);
                    if (gpu_scene_intersect(spheres,nSpheres,tris,nTris,sphereBVH,triBVH,hp.x,hp.y,hp.z,lwi.x,lwi.y,lwi.z,EPS_GPU,st,snx,sny,snz,smid,useSmooth)) {
                        if (materials[smid].type != GPU_EMISSIVE) continue;
                    }
                    // eval BRDF
                    float3 brdf_val; float pdf_brdf_val;
                    if (mat.type == GPU_DIFFUSE) {
                        brdf_val = make_float3_(mat.kd_r*cosRay/M_PI, mat.kd_g*cosRay/M_PI, mat.kd_b*cosRay/M_PI);
                        pdf_brdf_val = cosRay / M_PI;
                    } else if (mat.type == GPU_GLOSSY) {
                        float3 F0 = make_float3_(mat.F0_r, mat.F0_g, mat.F0_b);
                        float3 kd = make_float3_(mat.kd_r, mat.kd_g, mat.kd_b);
                        brdf_val = gpu_ggx_eval(wo, lwi, N, kd, F0, mat.roughness);
                        // approximate pdf for MIS
                        float pD = (kd.x+kd.y+kd.z)/(kd.x+kd.y+kd.z+F0.x+F0.y+F0.z+1e-6f);
                        float pdfD = cosRay/M_PI; float3 h_ = normalize_f3(wo+lwi);
                        float ch_=fmaxf(0,dot_f3(N,h_)), dwh_=fmaxf(1e-6f,dot_f3(wo,h_));
                        float a2=mat.roughness*mat.roughness;
                        float D_=a2/(M_PI*ch_*ch_*ch_*ch_*(a2+(1-ch_*ch_)/(ch_*ch_))*(a2+(1-ch_*ch_)/(ch_*ch_)));
                        float pdfS = D_*ch_/(4*dwh_);
                        pdf_brdf_val = pD*pdfD + (1-pD)*pdfS;
                    } else continue;
                    float mis_w = (pdf_w*pdf_w)/(pdf_w*pdf_w+pdf_brdf_val*pdf_brdf_val+1e-6f);
                    float3 contrib = make_float3_(em.F0_r,em.F0_g,em.F0_b) * brdf_val * (mis_w/pdf_w);
                    rsum+=thr.x*contrib.x; gsum+=thr.y*contrib.y; bsum+=thr.z*contrib.z;
                }
            }

            if (mode == 2 && mat.type != GPU_REFLECTIVE && mat.type != GPU_REFRACTIVE) break; // pure NEE: delta 继续追踪

            // 保存非 delta 顶点信息（BRDF 采样前），用于下一跳 MIS
            if (mat.type == GPU_DIFFUSE || mat.type == GPU_GLOSSY) {
                prevHp = hp; prevN = N; prevWo = wo; prevThroughput = thr;
                prevType = mat.type;
                prevKd_r=mat.kd_r;prevKd_g=mat.kd_g;prevKd_b=mat.kd_b;
                prevF0_r=mat.F0_r;prevF0_g=mat.F0_g;prevF0_b=mat.F0_b;
                prevRoughness=mat.roughness;
            } else {
                prevType = -1; // delta 顶点不参与 MIS
            }

            float3 wi; float pdf_brdf;
            if (mat.type == GPU_DIFFUSE) {
                wi = gpu_cosine_sample_dir(gpu_randf(seed), gpu_randf(seed), N);
                thr = thr * make_float3_(mat.kd_r, mat.kd_g, mat.kd_b);
                fromDelta = false;
            } else if (mat.type == GPU_REFLECTIVE) {
                float3 I = d; // d 已指向表面
                wi = gpu_reflect(I, N);
                thr = thr * make_float3_(mat.atten_r, mat.atten_g, mat.atten_b);
                fromDelta = true;
            } else if (mat.type == GPU_REFRACTIVE) {
                float3 I = d;
                if (mat.dispersion > 0.0f && currentIOR == 1.0f) {
                    // 色散: 随机选 R/G/B, 各 1/3 概率, 波长全程保持
                    float r = gpu_randf(seed);
                    int ch;
                    float wl_ior;
                    if (r < 1.0f/3.0f)      { ch=0; wl_ior = mat.ior - mat.dispersion*0.5f; }
                    else if (r < 2.0f/3.0f) { ch=1; wl_ior = mat.ior; }
                    else                    { ch=2; wl_ior = mat.ior + mat.dispersion*0.5f; }
                    float eta_d = (currentIOR == wl_ior) ? (currentIOR/1.0f) : (currentIOR/wl_ior);
                    float3 Td;
                    if (gpu_refract(I, N, eta_d, Td)) {
                        wi = Td;
                        currentIOR = mat.ior;
                    } else {
                        wi = gpu_reflect(I, N);
                    }
                    float3 atten3 = make_float3_(mat.atten_r*3, mat.atten_g*3, mat.atten_b*3);
                    if      (ch == 0) thr = thr * make_float3_(atten3.x, 0, 0);
                    else if (ch == 1) thr = thr * make_float3_(0, atten3.y, 0);
                    else              thr = thr * make_float3_(0, 0, atten3.z);
                    fromDelta = true;
                } else {
                    float eta = (currentIOR == mat.ior) ? (currentIOR/1.0f) : (currentIOR/mat.ior);
                    float nextIOR = (currentIOR == mat.ior) ? 1.0f : mat.ior;
                    float3 T;
                    if (gpu_refract(I, N, eta, T)) {
                        if (useFresnel) {
                            float cosI = fabsf(dot_f3(d, N));
                            float Fr = gpu_fresnel(cosI, mat.ior);
                            if (gpu_randf(seed) < Fr) {
                                wi = gpu_reflect(I, N);
                            } else {
                                wi = T; currentIOR = nextIOR;
                            }
                        } else {
                            wi = T; currentIOR = nextIOR;
                        }
                    } else {
                        wi = gpu_reflect(I, N);
                    }
                    thr = thr * make_float3_(mat.atten_r, mat.atten_g, mat.atten_b);
                    fromDelta = true;
                }
            } else if (mat.type == GPU_GLOSSY) {
                float3 F0 = make_float3_(mat.F0_r, mat.F0_g, mat.F0_b);
                float3 kd = make_float3_(mat.kd_r, mat.kd_g, mat.kd_b);
                float pD = (kd.x+kd.y+kd.z)/(kd.x+kd.y+kd.z+F0.x+F0.y+F0.z+1e-6f);
                if (gpu_randf(seed) < pD) {
                    wi = gpu_cosine_sample_dir(gpu_randf(seed), gpu_randf(seed), N);
                } else {
                    float3 h_ = gpu_sample_ggx(gpu_randf(seed), gpu_randf(seed), mat.roughness, N, wo, wi, pdf_brdf);
                }
                float3 brdf = gpu_ggx_eval(wo, wi, N, kd, F0, mat.roughness);
                // combined pdf
                float pD_pdf = fmaxf(0,dot_f3(N,wi))/M_PI;
                float3 hx = normalize_f3(wo+wi);
                float ch = fmaxf(0,dot_f3(N,hx)), dw = fmaxf(1e-6f,dot_f3(wo,hx));
                float a2=mat.roughness*mat.roughness;
                float Dx = a2/(M_PI*ch*ch*ch*ch*(a2+(1-ch*ch)/(ch*ch))*(a2+(1-ch*ch)/(ch*ch)));
                float pS_pdf = Dx*ch/(4*dw);
                float pdf_c = pD*pD_pdf + (1-pD)*pS_pdf;
                if (pdf_c < 1e-8f) break;
                thr = thr * brdf / pdf_c;
                fromDelta = false;
            } else break;

            o = hp; d = wi;
        }
    }
    int idx = (py*cam.w+px)*3;
    output[idx]=rsum/samples; output[idx+1]=gsum/samples; output[idx+2]=bsum/samples;
}

// ============ 主机渲染入口 ============
void gpuRender(const GPUScene &scene, float *output, int samples, const char *mode, bool smoothShading, bool useFresnel) {
    int w = scene.cam.w, h = scene.cam.h;
    int modeInt = (strcmp(mode, "brdf") == 0) ? 0 : (strcmp(mode, "nee") == 0) ? 2 : 1;
    int sm = smoothShading ? 1 : 0;
    int uf = useFresnel ? 1 : 0;
    cout << "GPU: " << scene.spheres.size() << " spheres, " << scene.triangles.size()
         << " triangles, mode=" << mode << (smoothShading ? " smooth" : " flat") << endl;

    // 收集发光体编号
    vector<int> emissiveIndices;
    for (int i = 0; i < (int)scene.materials.size(); i++)
        if (scene.materials[i].type == GPU_EMISSIVE)
            emissiveIndices.push_back(i);
    int nEm = (int)emissiveIndices.size();
    int *d_emIdx = nullptr;
    if (nEm > 0) {
        cudaMalloc(&d_emIdx, nEm * sizeof(int));
        cudaMemcpy(d_emIdx, emissiveIndices.data(), nEm * sizeof(int), cudaMemcpyHostToDevice);
    }

    GPUSphere *d_spheres = nullptr;
    GPUTriangle *d_tris = nullptr;
    GPUBVHNode *d_sphereBVH = nullptr;
    GPUBVHNode *d_triBVH = nullptr;
    GPUMaterial *d_mats = nullptr;
    float *d_output = nullptr;

    cudaError_t err;
    err = cudaMalloc(&d_spheres, scene.spheres.size() * sizeof(GPUSphere));
    if (err != cudaSuccess) cout << "cudaMalloc spheres: " << cudaGetErrorString(err) << endl;
    err = cudaMalloc(&d_tris, scene.triangles.size() * sizeof(GPUTriangle));
    if (err != cudaSuccess) cout << "cudaMalloc tris: " << cudaGetErrorString(err) << endl;
    if (!scene.sphere_bvh.empty()) {
        err = cudaMalloc(&d_sphereBVH, scene.sphere_bvh.size() * sizeof(GPUBVHNode));
        if (err != cudaSuccess) cout << "cudaMalloc sphereBVH: " << cudaGetErrorString(err) << endl;
        cudaMemcpy(d_sphereBVH, scene.sphere_bvh.data(), scene.sphere_bvh.size() * sizeof(GPUBVHNode), cudaMemcpyHostToDevice);
    }
    if (!scene.tri_bvh.empty()) {
        err = cudaMalloc(&d_triBVH, scene.tri_bvh.size() * sizeof(GPUBVHNode));
        if (err != cudaSuccess) cout << "cudaMalloc triBVH: " << cudaGetErrorString(err) << endl;
        cudaMemcpy(d_triBVH, scene.tri_bvh.data(), scene.tri_bvh.size() * sizeof(GPUBVHNode), cudaMemcpyHostToDevice);
    }
    err = cudaMalloc(&d_mats, scene.materials.size() * sizeof(GPUMaterial));
    if (err != cudaSuccess) cout << "cudaMalloc mats: " << cudaGetErrorString(err) << endl;
    err = cudaMalloc(&d_output, w * h * 3 * sizeof(float));
    if (err != cudaSuccess) cout << "cudaMalloc output: " << cudaGetErrorString(err) << endl;

    cudaMemcpy(d_spheres, scene.spheres.data(), scene.spheres.size() * sizeof(GPUSphere), cudaMemcpyHostToDevice);
    cudaMemcpy(d_tris, scene.triangles.data(), scene.triangles.size() * sizeof(GPUTriangle), cudaMemcpyHostToDevice);
    cudaMemcpy(d_mats, scene.materials.data(), scene.materials.size() * sizeof(GPUMaterial), cudaMemcpyHostToDevice);

    dim3 block(16, 16);
    dim3 grid((w + 15) / 16, (h + 15) / 16);

    path_trace_kernel<<<grid, block>>>(
        d_spheres, (int)scene.spheres.size(),
        d_tris, (int)scene.triangles.size(),
        d_sphereBVH, d_triBVH,
        d_mats, nEm, d_emIdx,
        scene.cam,
        scene.bg_r, scene.bg_g, scene.bg_b,
        d_output, samples, modeInt, sm, uf);

    err = cudaGetLastError();
    if (err != cudaSuccess) cout << "Kernel launch: " << cudaGetErrorString(err) << endl;
    err = cudaDeviceSynchronize();
    if (err != cudaSuccess) cout << "Kernel sync: " << cudaGetErrorString(err) << endl;

    cudaMemcpy(output, d_output, w * h * 3 * sizeof(float), cudaMemcpyDeviceToHost);

    cudaFree(d_spheres); cudaFree(d_tris); cudaFree(d_mats); cudaFree(d_output);
    if (d_sphereBVH) cudaFree(d_sphereBVH);
    if (d_triBVH) cudaFree(d_triBVH);
    if (d_emIdx) cudaFree(d_emIdx);
}
