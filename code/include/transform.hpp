// 原创性：PA1基础框架 + 独立实现(getChild)
#ifndef TRANSFORM_H
#define TRANSFORM_H

#include <vecmath.h>
#include "object3d.hpp"

// transforms a 3D point using a matrix, returning a 3D point
static Vector3f transformPoint(const Matrix4f &mat, const Vector3f &point) {
    return (mat * Vector4f(point, 1)).xyz();
}

// transform a 3D direction using a matrix, returning a direction
static Vector3f transformDirection(const Matrix4f &mat, const Vector3f &dir) {
    return (mat * Vector4f(dir, 0)).xyz();
}

class Transform : public Object3D {
public:
    Transform() {}

    Transform(const Matrix4f &m, Object3D *obj) : o(obj) {
        transform = m.inverse();
    }

    ~Transform() {
    }

    virtual bool intersect(const Ray &r, Hit &h, float tmin) {
        Vector3f trSource = transformPoint(transform, r.getOrigin());
        Vector3f trDirection = transformDirection(transform, r.getDirection());
        Ray tr(trSource, trDirection);
        bool inter = o->intersect(tr, h, tmin);
        if (inter) {
            h.set(h.getT(), h.getMaterial(), transformDirection(transform.transposed(), h.getNormal()).normalized());
        }
        return inter;
    }

    float sampleSurface(float r1, float r2, Vector3f &point, Vector3f &n) const override {
        float pdf = o->sampleSurface(r1, r2, point, n);
        // transform 存储的是逆矩阵, transform.inverse() = 原始正向矩阵
        point = transformPoint(transform.inverse(), point);
        n = transformDirection(transform.transposed(), n).normalized();
        return pdf;
    }

    float getArea() const override { return o->getArea(); }  // 近似（均匀缩放时才准确）

    Object3D *getChild() const { return o; }

    Matrix4f getTransformInv() const { return transform; }

protected:
    Object3D *o; //un-transformed object
    Matrix4f transform;
};

#endif //TRANSFORM_H
