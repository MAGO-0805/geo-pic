#ifndef TRIANGLE_H
#define TRIANGLE_H

#include "object3d.hpp"
#include <vecmath.h>
#include <cmath>
#include <iostream>
using namespace std;

// TODO: implement this class and add more fields as necessary,
class Triangle: public Object3D {

public:
	Triangle() = delete;

    // a b c are three vertex positions of the triangle
	Triangle( const Vector3f& a, const Vector3f& b, const Vector3f& c, Material* m) : Object3D(m) {
        vertices[0] = a;
        vertices[1] = b;
        vertices[2] = c;
        Vector3f edge1 = vertices[1] - vertices[0];
        Vector3f edge2 = vertices[2] - vertices[0];
        normal = Vector3f::cross(edge1, edge2).normalized();
	}

	bool intersect( const Ray& ray,  Hit& hit , float tmin) override {
        Vector3f E_1 = vertices[0] - vertices[1];
        Vector3f E_2 = vertices[0] - vertices[2];
        Vector3f S = vertices[0] - ray.getOrigin();
        Matrix3f Rd_E1_E2 = Matrix3f(ray.getDirection(), E_1, E_2, true);
        Matrix3f S_E1_E2 = Matrix3f(S, E_1, E_2, true);
        Matrix3f Rd_S_E2 = Matrix3f(ray.getDirection(), S, E_2, true);
        Matrix3f Rd_E1_S = Matrix3f(ray.getDirection(), E_1, S, true);
        float alpha = Rd_E1_E2.determinant();
        Vector3f t_beta_gamma = Vector3f(S_E1_E2.determinant(), Rd_S_E2.determinant(), Rd_E1_S.determinant()) / alpha;
        float t = t_beta_gamma.x();
        float beta = t_beta_gamma.y();
        float gamma = t_beta_gamma.z();
        if (t > tmin && t < hit.getT() && beta >= 0 && gamma >= 0 && beta + gamma <= 1) {
            // 根据光线和当前三角形认定的normal设置交点的normal方向
            Vector3f hit_normal = normal;
            if (Vector3f::dot(normal, ray.getDirection()) > 0) {
                hit_normal = -normal;
            }
            hit.set(t, material, hit_normal);
            return true;
        }
        return false;
	}

    float sampleSurface(float r1, float r2, Vector3f &point, Vector3f &n) const override {
        float sr1 = sqrt(r1);
        float u = 1.0f - sr1;
        float v = r2 * sr1;
        point = vertices[0] + u * (vertices[1] - vertices[0]) + v * (vertices[2] - vertices[0]);
        n = normal;
        return 1.0f / getArea();
    }

    float getArea() const override {
        Vector3f e1 = vertices[1] - vertices[0];
        Vector3f e2 = vertices[2] - vertices[0];
        return Vector3f::cross(e1, e2).length() * 0.5f;
    }
	Vector3f normal;
	Vector3f vertices[3];
    Vector3f getVertex(int i) const { return vertices[i]; }
// protected:

};

#endif //TRIANGLE_H
