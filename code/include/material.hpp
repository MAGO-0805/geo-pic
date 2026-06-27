// 原创性：PA1基础框架 + 课程内容(BRDF接口) + 独立实现(Glossy/Emissive材质)
#ifndef MATERIAL_H
#define MATERIAL_H

#include <cassert>
#include <vecmath.h>
#include <cmath>

#include "ray.hpp"
#include "hit.hpp"
#include "brdf.hpp"
#include <iostream>

enum MaterialType { PHONG, REFLECTIVE, REFRACTIVE, EMISSIVE, GLOSSY };

class Material {
public:
    // PHONG (Whitted 漫反射 + Phong 高光)
    explicit Material(const Vector3f &d_color, const Vector3f &s_color = Vector3f::ZERO, float s = 0) :
            type(PHONG), brdf(new LambertianBRDF(d_color)),
            diffuseColor(d_color), specularColor(s_color), shininess(s),
            emissionColor(Vector3f::ZERO), emissiveFlag(false), dispersionSpread(0) {}

    // REFLECTIVE
    Material(MaterialType t, const Vector3f &atten) :
            type(REFLECTIVE), brdf(new SpecularReflectionBRDF(atten)),
            diffuseColor(Vector3f::ZERO), specularColor(Vector3f::ZERO), shininess(0),
            attenuationColor(atten), refractiveIndex(1.0f),
            emissionColor(Vector3f::ZERO), emissiveFlag(false), dispersionSpread(0) {
        assert(t == REFLECTIVE);
    }

    // REFRACTIVE
    Material(MaterialType t, float ior, const Vector3f &atten, float dispersion = 0.0f) :
            type(REFRACTIVE), brdf(new SpecularTransmissionBRDF(ior, atten)),
            diffuseColor(Vector3f::ZERO), specularColor(Vector3f::ZERO), shininess(0),
            attenuationColor(atten), refractiveIndex(ior),
            emissionColor(Vector3f::ZERO), emissiveFlag(false), dispersionSpread(dispersion) {
        assert(t == REFRACTIVE);
    }

    // EMISSIVE
    Material(MaterialType t, const Vector3f &emission, const Vector3f &) :
            type(PHONG), brdf(new LambertianBRDF(Vector3f::ZERO)),
            diffuseColor(Vector3f::ZERO), specularColor(Vector3f::ZERO), shininess(0),
            emissionColor(emission), emissiveFlag(true), dispersionSpread(0) {
        assert(t == EMISSIVE);
    }

    // GLOSSY
    Material(MaterialType t, const Vector3f &kd, const Vector3f &F0, float roughness) :
            type(GLOSSY), brdf(new GlossyBRDF(kd, F0, roughness)),
            diffuseColor(kd), specularColor(F0), shininess(0),
            attenuationColor(Vector3f::ZERO), refractiveIndex(1.0f),
            emissionColor(Vector3f::ZERO), emissiveFlag(false), glossyRoughness(roughness), dispersionSpread(0) {
        assert(t == GLOSSY);
    }

    ~Material() { delete brdf; }

    MaterialType getType() const { return type; }
    BRDF *getBRDF() const { return brdf; }
    bool isEmissive() const { return emissiveFlag; }
    Vector3f getEmission() const { return emissionColor; }
    Vector3f getDiffuseColor() const { return diffuseColor; }
    Vector3f getAttenuationColor() const { return attenuationColor; }
    float getRefractiveIndex() const { return refractiveIndex; }
    float getRoughness() const { return glossyRoughness; }
    float getDispersion() const { return dispersionSpread; }

    void setFresnel(bool v) {
        if (auto *st = dynamic_cast<SpecularTransmissionBRDF *>(brdf))
            st->setFresnel(v);
    }
    bool hasFresnel() const {
        if (auto *st = dynamic_cast<SpecularTransmissionBRDF *>(brdf))
            return st->hasFresnel();
        return false;
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

    // === Whitted-Style Phong 着色 (保留不变) ===
    Vector3f Shade(const Ray &ray, const Hit &hit,
                   const Vector3f &dirToLight, const Vector3f &lightColor) {
        Vector3f L = dirToLight.normalized();
        Vector3f N = hit.getNormal().normalized();
        Vector3f V = -ray.getDirection();
        Vector3f R = (2 * Vector3f::dot(N, L) * N - L).normalized();
        Vector3f diffuse = diffuseColor * std::max(0.0f, Vector3f::dot(N, L));
        Vector3f specular = specularColor * std::pow(std::max(0.0f, Vector3f::dot(R, V)), shininess);
        return lightColor * (diffuse + specular);
    }

private:
    MaterialType type;
    BRDF *brdf;
    Vector3f diffuseColor, specularColor;
    float shininess;
    Vector3f attenuationColor;
    float refractiveIndex;
    float glossyRoughness;
    Vector3f emissionColor;
    bool emissiveFlag;
    float dispersionSpread;
};

#endif // MATERIAL_H
