#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <iostream>
#include <random>

#include "scene_parser.hpp"
#include "image.hpp"
#include "camera.hpp"
#include "group.hpp"
#include "light.hpp"
#include "material.hpp"

#include <string>

using namespace std;

// === 路径追踪参数 ===
const int SAMPLES = 128;
const int MAX_DEPTH = 10;
const int RR_DEPTH = 3;
const float EPSILON = 0.001f;
const float AIR_IOR = 1.0f;

// === 随机数 ===
mt19937 rng(42);
uniform_real_distribution<float> dist(0.0f, 1.0f);
inline float randf() { return dist(rng); }

// === Whitted-Style 递归追踪 ===
Vector3f traceWhitted(const Ray &ray, float tmin, Group *scene, SceneParser &parser,
                      int depth, float currentIOR) {
    if (depth > 5) return Vector3f::ZERO;

    Hit hit;
    if (!scene->intersect(ray, hit, tmin)) return parser.getBackgroundColor();

    Material *mat = hit.getMaterial();
    Vector3f hitPoint = ray.pointAtParameter(hit.getT());

    if (mat->getType() == PHONG) {
        Vector3f color(0, 0, 0);
        for (int li = 0; li < parser.getNumLights(); li++) {
            Light *light = parser.getLight(li);
            Vector3f L, lc;
            light->getIllumination(hitPoint, L, lc);
            Ray sr(hitPoint, L);
            Hit sh;
            float md = light->getMaxShadowDistance(hitPoint);
            if (scene->intersect(sr, sh, EPSILON) && sh.getT() < md) continue;
            color += mat->Shade(ray, hit, L, lc);
        }
        return color;
    }

    if (mat->getType() == REFLECTIVE) {
        Vector3f I = ray.getDirection().normalized();
        Vector3f N = hit.getNormal().normalized();
        Vector3f R = Material::reflectDirection(I, N);
        return mat->getAttenuationColor() *
               traceWhitted(Ray(hitPoint, R), EPSILON, scene, parser, depth + 1, currentIOR);
    }

    if (mat->getType() == REFRACTIVE) {
        Vector3f I = ray.getDirection().normalized();
        Vector3f N = hit.getNormal().normalized();
        float ior = mat->getRefractiveIndex();
        float eta = (currentIOR == ior) ? (currentIOR / AIR_IOR) : (currentIOR / ior);
        float nextIOR = (currentIOR == ior) ? AIR_IOR : ior;

        Vector3f T;
        if (Material::refractDirection(I, N, eta, T))
            return mat->getAttenuationColor() *
                   traceWhitted(Ray(hitPoint, T), EPSILON, scene, parser, depth + 1, nextIOR);
        else {
            Vector3f R = Material::reflectDirection(I, N);
            return mat->getAttenuationColor() *
                   traceWhitted(Ray(hitPoint, R), EPSILON, scene, parser, depth + 1, currentIOR);
        }
    }

    return Vector3f::ZERO;
}

// === 路径追踪 ===
Vector3f tracePath(const Ray &firstRay, Group *scene, const Vector3f &bgColor) {
    Vector3f radiance(0, 0, 0);
    Vector3f throughput(1, 1, 1);
    Ray ray = firstRay;
    float currentIOR = AIR_IOR;

    for (int depth = 0; depth < MAX_DEPTH; depth++) {
        if (depth > RR_DEPTH) {
            float p = min(1.0f, max(throughput.x(), max(throughput.y(), throughput.z())));
            if (randf() > p) break;
            throughput = throughput / p;
        }

        Hit hit;
        if (!scene->intersect(ray, hit, EPSILON)) {
            radiance += throughput * bgColor;
            break;
        }

        Material *mat = hit.getMaterial();
        Vector3f hitPoint = ray.pointAtParameter(hit.getT());
        Vector3f N = hit.getNormal().normalized();
        Vector3f wo = -ray.getDirection().normalized();

        if (mat->isEmissive()) {
            radiance += throughput * mat->getEmission();
            break;
        }

        BRDF *brdf = mat->getBRDF();
        Vector3f wi;
        float pdf, nextIOR;

        if (brdf->isDelta()) {
            wi = brdf->sampleDelta(wo, N, currentIOR, nextIOR);
            throughput = throughput * brdf->deltaThroughput();
        } else {
            brdf->sample(wo, N, randf(), randf(), wi, pdf);
            if (pdf < 1e-6f) break;
            throughput = throughput * brdf->eval(wo, wi, N) / pdf;
            nextIOR = AIR_IOR;
        }
        ray = Ray(hitPoint, wi);
        currentIOR = nextIOR;
    }
    return radiance;
}

int main(int argc, char *argv[]) {
    bool usePathTracing = false;
    string inputFile, outputFile;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--path") == 0) usePathTracing = true;
        else if (inputFile.empty()) inputFile = argv[i];
        else outputFile = argv[i];
    }

    if (inputFile.empty() || outputFile.empty()) {
        cout << "Usage: ./PA1 <scene.txt> <output.bmp> [--path]" << endl;
        return 1;
    }

    SceneParser parser(inputFile.c_str());
    Camera *camera = parser.getCamera();
    Group *scene = parser.getGroup();
    int w = camera->getWidth(), h = camera->getHeight();
    Image outputImage(w, h);

    if (usePathTracing) {
        Vector3f bg = parser.getBackgroundColor();
        cout << "Path Tracing " << w << "x" << h << " with " << SAMPLES << " spp...";
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                Vector3f color(0, 0, 0);
                for (int s = 0; s < SAMPLES; s++) {
                    float jx = randf() - 0.5f, jy = randf() - 0.5f;
                    color += tracePath(camera->generateRay(Vector2f(x + jx, y + jy)), scene, bg);
                }
                outputImage.SetPixel(x, y, color / SAMPLES);
            }
            if (y % (h / 10) == 0) cout << "." << flush;
        }
        cout << " Done!" << endl;
    } else {
        cout << "Whitted-Style " << w << "x" << h << "...";
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                Ray r = camera->generateRay(Vector2f(x, y));
                outputImage.SetPixel(x, y, traceWhitted(r, 0, scene, parser, 0, AIR_IOR));
            }
            if (y % (h / 10) == 0) cout << "." << flush;
        }
        cout << " Done!" << endl;
    }

    outputImage.SaveBMP(outputFile.c_str());
    return 0;
}
