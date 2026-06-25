#ifndef MATERIAL_H
#define MATERIAL_H

#include <cassert>
#include <vecmath.h>

#include "ray.hpp"
#include "hit.hpp"
#include <iostream>

enum MaterialType { PHONG, REFLECTIVE, REFRACTIVE };

class Material {
public:

    // PHONG constructor (backward-compatible)
    explicit Material(const Vector3f &d_color, const Vector3f &s_color = Vector3f::ZERO, float s = 0) :
            type(PHONG), diffuseColor(d_color), specularColor(s_color), shininess(s),
            attenuationColor(Vector3f::ZERO), refractiveIndex(1.0f) {}

    // REFLECTIVE constructor
    Material(MaterialType t, const Vector3f &atten) :
            type(REFLECTIVE), diffuseColor(Vector3f::ZERO), specularColor(Vector3f::ZERO), shininess(0),
            attenuationColor(atten), refractiveIndex(1.0f) {
        assert(t == REFLECTIVE);
    }

    // REFRACTIVE constructor
    Material(MaterialType t, float ior, const Vector3f &atten) :
            type(REFRACTIVE), diffuseColor(Vector3f::ZERO), specularColor(Vector3f::ZERO), shininess(0),
            attenuationColor(atten), refractiveIndex(ior) {
        assert(t == REFRACTIVE);
    }

    virtual ~Material() = default;

    MaterialType getType() const { return type; }

    virtual Vector3f getDiffuseColor() const {
        return diffuseColor;
    }

    Vector3f getAttenuationColor() const { return attenuationColor; }
    float getRefractiveIndex() const { return refractiveIndex; }

    Vector3f Shade(const Ray &ray, const Hit &hit,
                   const Vector3f &dirToLight, const Vector3f &lightColor) {
        Vector3f shaded = Vector3f::ZERO;
        Vector3f L = dirToLight.normalized();
        Vector3f N = hit.getNormal().normalized();
        Vector3f V = -ray.getDirection();
        Vector3f R = (2 * Vector3f::dot(N, L) * N - L).normalized();

        Vector3f diffuse = diffuseColor * std::max(0.0f, Vector3f::dot(N, L));
        Vector3f specular = specularColor * std::pow(std::max(0.0f, Vector3f::dot(R, V)), shininess);

        shaded = lightColor * (diffuse + specular);
        return shaded;
    }

    static Vector3f reflectDirection(const Vector3f &I, const Vector3f &N) {
        return (I - 2.0f * Vector3f::dot(N, I) * N).normalized();
    }

    static bool refractDirection(const Vector3f &I, const Vector3f &N, float eta, Vector3f &T) {
        float cosI = -Vector3f::dot(I, N);
        float sinT2 = eta * eta * (1.0f - cosI * cosI);
        if (sinT2 > 1.0f) return false;
        float cosT = sqrt(1.0f - sinT2);
        T = (eta * I + (eta * cosI - cosT) * N).normalized();
        return true;
    }

protected:
    MaterialType type;
    Vector3f diffuseColor;
    Vector3f specularColor;
    float shininess;
    Vector3f attenuationColor;
    float refractiveIndex;
};


#endif // MATERIAL_H
