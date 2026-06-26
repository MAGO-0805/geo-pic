from PIL import Image
import numpy as np

img = Image.open("output/111test.bmp")
arr = np.array(img)
h, w = arr.shape[:2]
print(f"图片尺寸: {w} × {h}")
# 打印第10行第20列像素RGB值
x, y = 20, 10
r,g,b = arr[y, x]
print(f"像素({x},{y}) R:{r} G:{g} B:{b}")