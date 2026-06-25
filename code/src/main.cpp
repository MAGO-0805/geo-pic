#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <iostream>

#include "scene_parser.hpp"
#include "image.hpp"
#include "camera.hpp"
#include "group.hpp"
#include "light.hpp"
#include "material.hpp"

#include <string>

using namespace std;

const int MAX_DEPTH = 5;
const float EPSILON = 0.001f;
const float AIR_IOR = 1.0f;

Vector3f traceRay(const Ray &ray, float tmin, Group *scene, SceneParser &parser, int depth, float currentIOR) {
    if (depth > MAX_DEPTH) {
        return Vector3f::ZERO;
    }

    Hit hit;
    bool isIntersect = scene->intersect(ray, hit, tmin);

    if (!isIntersect) {
        return parser.getBackgroundColor();
    }

    Material *material = hit.getMaterial();
    Vector3f hitPoint = ray.pointAtParameter(hit.getT());

    switch (material->getType()) {
        case PHONG: {
            Vector3f finalColor = Vector3f::ZERO;
            for (int li = 0; li < parser.getNumLights(); li++) {
                Light *light = parser.getLight(li);
                Vector3f L, lightColor;
                light->getIllumination(hitPoint, L, lightColor);

                // Shadow Ray: 从交点向光源方向检测遮挡
                Ray shadowRay(hitPoint, L);
                Hit shadowHit;
                float maxDist = light->getMaxShadowDistance(hitPoint);
                if (scene->intersect(shadowRay, shadowHit, EPSILON) &&
                    shadowHit.getT() < maxDist) {
                    continue; // 被遮挡，该光源对此点的贡献为0
                }

                finalColor += material->Shade(ray, hit, L, lightColor);
            }
            return finalColor;
        }

        case REFLECTIVE: {
            Vector3f I = ray.getDirection().normalized();
            Vector3f N = hit.getNormal().normalized();
            Vector3f R = Material::reflectDirection(I, N);
            Ray reflectedRay(hitPoint, R);
            Vector3f recursiveColor = traceRay(reflectedRay, EPSILON, scene, parser, depth + 1, currentIOR);
            return material->getAttenuationColor() * recursiveColor;
        }

        case REFRACTIVE: {
            Vector3f I = ray.getDirection().normalized();
            Vector3f N = hit.getNormal().normalized();
            float materialIOR = material->getRefractiveIndex();

            float eta;
            float nextIOR;

            if (currentIOR == materialIOR) {
                // Exiting: material -> air
                eta = currentIOR / AIR_IOR;
                nextIOR = AIR_IOR;
            } else {
                // Entering: current medium -> material
                eta = currentIOR / materialIOR;
                nextIOR = materialIOR;
            }

            Vector3f T;
            if (Material::refractDirection(I, N, eta, T)) {
                Ray refractedRay(hitPoint, T);
                Vector3f recursiveColor = traceRay(refractedRay, EPSILON, scene, parser, depth + 1, nextIOR);
                return material->getAttenuationColor() * recursiveColor;
            } else {
                // Total internal reflection — stays in same medium
                Vector3f R = Material::reflectDirection(I, N);
                Ray reflectedRay(hitPoint, R);
                Vector3f recursiveColor = traceRay(reflectedRay, EPSILON, scene, parser, depth + 1, currentIOR);
                return material->getAttenuationColor() * recursiveColor;
            }
        }
    }

    return Vector3f::ZERO;
}

int main(int argc, char *argv[]) {
    for (int argNum = 1; argNum < argc; ++argNum) {
        std::cout << "Argument " << argNum << " is: " << argv[argNum] << std::endl;
    }

    if (argc != 3) {
        cout << "Usage: ./bin/PA1 <input scene file> <output bmp file>" << endl;
        return 1;
    }
    string inputFile = argv[1];
    string outputFile = argv[2];

    SceneParser parser(inputFile.c_str());
    Camera *camera = parser.getCamera();
    Group *baseGroup = parser.getGroup();
    Image outputImage(camera->getWidth(), camera->getHeight());

    for (int x = 0; x < camera->getWidth(); x++) {
        for (int y = 0; y < camera->getHeight(); y++) {
            Ray camRay = camera->generateRay(Vector2f(x, y));
            Vector3f pixelColor = traceRay(camRay, 0, baseGroup, parser, 0, AIR_IOR);
            outputImage.SetPixel(x, y, pixelColor);
        }
    }

    outputImage.SaveBMP(outputFile.c_str());
    cout << "Hello! Computer Graphics!" << endl;
    return 0;
}
