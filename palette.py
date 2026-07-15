

from PIL import Image
import sys
if __name__ == '__main__':

    tex_name = sys.argv[1]
    img = Image.open("./models/mahjong_texture_{}_256.png".format(tex_name))
    (x,y) = img.size
    unique_colors_map = {}
    unique_colors_rev_map = []
    raw_pixels = []
    for py in range(y):
        for px in range(x):
            pix = img.getpixel((px,py))
            
            raw_pixels.append(pix)


    print("#ifndef TEXTURE_{}_H".format(tex_name))
    print("#define TEXTURE_{}_H".format(tex_name))    

    print("u8 texture_{}[{}]".format(tex_name, len(raw_pixels)) + " = {")
    for idx,pix in enumerate(raw_pixels):
        print("{},".format(pix), end=" ")
        if ((idx+1) %32)==0:
            print("")
    print("};")
    print("#endif")