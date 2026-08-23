from PIL import Image
import sys

WHITE_IDX = 3




global_pal = [
    (0,0,0), # black
    (0,255,0), # green
    (255,223,0), # gold
    (255,255,255), # white
    (255,0,0), # red
    (0,0,255) # blue
]

def un_rle(packets, color_lut):
    output = []
    idx = 0
    while idx < len(packets):
        packet = packets[idx]
        idx += 1
        if packet&0b1:
            # non-white run
            length = (packet>>2)+1
            color_bit = (packet>>1)&1
            actual_pal_idx = color_lut[color_bit]
        else:
            # white run
            next_byte = packets[idx]
            idx += 1
            length = (packet>>1) + (next_byte<<7) + 1

            actual_pal_idx = WHITE_IDX

        output += ([actual_pal_idx] * length)

    return output


tenbou = [
216, 250, 1, 0, 0, 57, 0, 0, 1, 188, 1, 189, 150, 1, 213, 158, 1, 169, 230, 254,]
color_lut = [0, 3]

#with open(sys.argv[1], "w"):
uncompressed_bytes = un_rle(tenbou, color_lut)
img = Image.new("RGB", (256, 256))
for y in range(256):
    for x in range(256):
        img.putpixel((x,y),global_pal[uncompressed_bytes[y*256+x]])


img.show()
print(img)