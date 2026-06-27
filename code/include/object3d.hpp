// 原创性：PA1基础框架 + 独立实现(sampleSurface/getArea)
#ifndef OBJECT3D_H
#define OBJECT3D_H

#include "ray.hpp"
#include "hit.hpp"
#include "material.hpp"

// Base class for all 3d entities.
class Object3D {
public:
    Object3D() : material(nullptr) {}

    virtual ~Object3D() = default;

    explicit Object3D(Material *material) {
        this->material = material;
    }

    // Intersect Ray with this object. If hit, store information in hit structure.
    virtual bool intersect(const Ray &r, Hit &h, float tmin) = 0;

    // NEE 表面采样: 返回面积 PDF，设 point 和 normal
    virtual float sampleSurface(float r1, float r2, Vector3f &point, Vector3f &normal) const { return 0; }

    virtual float getArea() const { return 0; }

    Material *getMaterial() const { return material; }

protected:

    Material *material;
};

#endif

