import mmap
from PIL import Image
import numpy as np

WIDTH = 128
HEIGHT = 64
BYTES_PER_PIXEL = 4
STRIDE = 512
FB_SIZE = STRIDE * HEIGHT

# 打开图片
img = Image.open("chatgpt.png").convert("RGBA")
img = img.resize((WIDTH, HEIGHT))

# RGBA -> BGRA
arr = np.array(img)
arr = arr[:, :, [2, 1, 0, 3]]

# 转成连续字节
frame = arr.tobytes()

with open("/dev/fb0", "r+b") as f:
    fb = mmap.mmap(f.fileno(), FB_SIZE)
    fb[:len(frame)] = frame
    fb.close()
