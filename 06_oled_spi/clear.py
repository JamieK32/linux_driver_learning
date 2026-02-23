import mmap

WIDTH = 128
HEIGHT = 64
BYTES_PER_PIXEL = 4
STRIDE = 512
FB_SIZE = STRIDE * HEIGHT

with open("/dev/fb0", "r+b") as f:
    fb = mmap.mmap(f.fileno(), FB_SIZE)

    fb[:] = b"\x00" * FB_SIZE   # 全 0

    fb.close()
