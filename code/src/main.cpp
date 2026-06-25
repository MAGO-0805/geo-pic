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

#include <string>

using namespace std;

int main(int argc, char *argv[]) {
    for (int argNum = 1; argNum < argc; ++argNum) {
        std::cout << "Argument " << argNum << " is: " << argv[argNum] << std::endl;
    }

    if (argc != 3) {
        cout << "Usage: ./bin/PA1 <input scene file> <output bmp file>" << endl;
        return 1;
    }
    string inputFile = argv[1];
    string outputFile = argv[2];  // only bmp is allowed.

    // TODO: Main RayCasting Logic
    // First, parse the scene using SceneParser. 
    SceneParser parser(inputFile.c_str());
    Camera *camera = parser.getCamera();
    Group *baseGroup = parser.getGroup();
    Image outputImage(camera->getWidth(), camera->getHeight());
    // Then loop over each pixel in the image, shooting a ray
    // through that pixel and finding its intersection with
    // the scene.  Write the color at the intersection to that
    // pixel in your output image.
    for(int x=0; x<camera->getWidth();x++){
        for(int y=0; y<camera->getHeight();y++){
            // 计算当前像素处相机射出光线 camRay
            Ray camRay = camera->generateRay(Vector2f(x,y));
            Hit hit;
            // 判断是否有交点，把最近的交点保存在hit中
            bool isIntersect = baseGroup->intersect(camRay, hit, 0);
            if(isIntersect){
                Vector3f finalColor = Vector3f::ZERO;
                // 找到交点后，累加来自所有光源的光强影响
                for(int li = 0; li < parser.getNumLights(); li++){
                    Light* light = parser.getLight(li);
                    Vector3f L, lightColor;
                    // 光亮度
                    light->getIllumination(camRay.pointAtParameter(hit.getT()), L, lightColor);
                    // 计算局部光强
                    finalColor += hit.getMaterial()->Shade(camRay, hit, L, lightColor);
                    // 注意这里没有考虑环境光，因为这个作业的场景里没有环境光源，大作业如果需要就在这里再加一个环境光即可
                }
                outputImage.SetPixel(x, y, finalColor);
            }else{
                // 没有交点，设置背景色
                outputImage.SetPixel(x, y, parser.getBackgroundColor());
            }
        }
    }
    outputImage.SaveBMP(outputFile.c_str());
    cout << "Hello! Computer Graphics!" << endl;
    return 0;
}

