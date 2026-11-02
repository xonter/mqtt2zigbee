from PIL import Image
import struct
for n in ("status_up", "status_down", "setup", "status_ru"):
    d = open(n + ".raw", "rb").read()
    px = struct.unpack("<%dH" % (240 * 135), d)
    im = Image.new("RGB", (240, 135))
    im.putdata([(((p >> 11) & 31) * 255 // 31, ((p >> 5) & 63) * 255 // 63, (p & 31) * 255 // 31) for p in px])
    im.resize((720, 405), Image.NEAREST).save(n + ".png")
