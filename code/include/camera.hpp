// 原创性：PA1基础框架
#ifndef CAMERA_H
#define CAMERA_H

#include "ray.hpp"
#include <vecmath.h>
#include <float.h>
#include <cmath>


class Camera {
public:
    Camera(const Vector3f &center, const Vector3f &direction, const Vector3f &up, int imgW, int imgH) {
        // 这里的center是相机的坐标，不是二位画布的中心（光心）
        this->center = center;
        this->direction = direction.normalized();
        this->horizontal = Vector3f::cross(this->direction, up).normalized();
        this->up = Vector3f::cross(this->horizontal, this->direction);
        this->width = imgW;
        this->height = imgH;
    }

    // Generate rays for each screen-space coordinate
    virtual Ray generateRay(const Vector2f &point) = 0;
    virtual ~Camera() = default;

    int getWidth() const { return width; }
    int getHeight() const { return height; }

    Vector3f getCenter() const { return center; }
    Vector3f getDirection() const { return direction; }
    Vector3f getUp() const { return up; }
    Vector3f getHorizontal() const { return horizontal; }

protected:
    // Extrinsic parameters
    Vector3f center;
    Vector3f direction;
    Vector3f up;
    Vector3f horizontal;
    // Intrinsic parameters
    int width;
    int height;
};

// TODO: Implement Perspective camera
// You can add new functions or variables whenever needed.
class PerspectiveCamera : public Camera {

public:
    PerspectiveCamera(const Vector3f &center, const Vector3f &direction,
            const Vector3f &up, int imgW, int imgH, float angle) : Camera(center, direction, up, imgW, imgH) {
        // angle is in radian.
        this->angle = angle;
        float half_angle = angle / 2.0f;
        this->f_y = height / (2.0f * tan(half_angle));
        this->f_x = f_y * (float)width / (float)height;
    }

    Ray generateRay(const Vector2f &point) override {
        // 先计算相机空间下的射线Rc
        Vector3f RcStart = Vector3f(0, 0, 0);
        Vector3f RcDir = Vector3f((point.x() - width / 2.0f) / f_x, (point.y() - height / 2.0f) / f_y, 1).normalized();
        Vector3f RwStart = center;
        Vector3f RwDir = Matrix3f(horizontal, up, direction) * RcDir;
        return Ray(RwStart, RwDir);
    }

    float getFx() const { return f_x; }
    float getFy() const { return f_y; }

private:
    float angle;
    float f_x;
    float f_y;
};

#endif //CAMERA_H
