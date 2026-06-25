#ifndef LIGHT_H
#define LIGHT_H

#include <Vector3f.h>
#include "object3d.hpp"

class Light {
public:
    Light() = default;

    virtual ~Light() = default;

    virtual void getIllumination(const Vector3f &p, Vector3f &dir, Vector3f &col) const = 0;

    // Shadow Ray 最大有效距离：方向光无穷远，点光源为交点到光源距离
    virtual float getMaxShadowDistance(const Vector3f &p) const = 0;
};


class DirectionalLight : public Light {
public:
    DirectionalLight() = delete;

    DirectionalLight(const Vector3f &d, const Vector3f &c) {
        direction = d.normalized();
        color = c;
    }

    ~DirectionalLight() override = default;

    void getIllumination(const Vector3f &p, Vector3f &dir, Vector3f &col) const override {
        dir = -direction;
        col = color;
    }

    float getMaxShadowDistance(const Vector3f &p) const override {
        return 1e38; // 方向光无穷远，任意遮挡即产生阴影
    }

private:

    Vector3f direction;
    Vector3f color;

};

class PointLight : public Light {
public:
    PointLight() = delete;

    PointLight(const Vector3f &p, const Vector3f &c) {
        position = p;
        color = c;
    }

    ~PointLight() override = default;

    void getIllumination(const Vector3f &p, Vector3f &dir, Vector3f &col) const override {
        dir = (position - p);
        dir = dir / dir.length();
        col = color;
    }

    float getMaxShadowDistance(const Vector3f &p) const override {
        return (position - p).length(); // 点光源：遮挡物必须在交点与光源之间
    }

private:

    Vector3f position;
    Vector3f color;

};

#endif // LIGHT_H
