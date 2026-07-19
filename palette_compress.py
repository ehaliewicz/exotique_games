
from PIL import Image
import sys
WHITE_IDX = 3

def un_rle(packets,unique_color_rev_map):

    output = []
    idx = 0
    while idx < len(packets):
        packet = packets[idx]
        idx += 1
        if packet&0b1:
            # non-white run
            length = (packet>>2)+1
            color_bit = (packet>>1)&1
            actual_pal_idx = unique_color_rev_map[color_bit]
        else:
            # white run
            next_byte = packets[idx]
            idx += 1
            length = (packet>>1) + (next_byte<<7) + 1

            actual_pal_idx = WHITE_IDX

        output += ([actual_pal_idx] * length)
    
        #color_bits = packet & 0b11
        #actual_pal_idx = unique_color_rev_map[color_bits]
        #adj_run_len = packet >> 2
        #run_len = adj_run_len + 1
        #output += ([actual_pal_idx] * run_len)

    return output


def rle(colors, unique_color_map, max_run_len, max_white_run_len):
    cur_run_len = None
    cur_run_color = None
    packet_output = []
    is_white_run = False

    for col in colors:
        if cur_run_len != -1 and col == cur_run_color and ((is_white_run and cur_run_len < max_white_run_len) or ((not is_white_run) and cur_run_len < max_run_len)):
            # if we've reached the limit of 256, then we have to stop
            cur_run_len += 1
        else:
            # output prev len if it exists
            if cur_run_len is not None:
                adj_run_len = cur_run_len-1 # represent a run length of 1 with 0, 2 with 1, 64 with 32, etc
                if is_white_run:
                    low_7_bits = (adj_run_len&0b1111111)<<1
                    packet_output.append(low_7_bits|0)
                    packet_output.append(adj_run_len>>7)
                    #packet_output.append((adj_run_len<<1)|0)
                else:
                    color_bit = unique_color_map[cur_run_color]
                    assert color_bit in [0,1]
                    packet_output.append((adj_run_len<<2)|(color_bit<<1)|1)
            cur_run_len = 1
            cur_run_color = col
            is_white_run = (col == 3)
        
    if cur_run_len is not None:
        adj_run_len = cur_run_len-1
        if is_white_run:
            low_7_bits = (adj_run_len&0b1111111)<<1
            packet_output.append(low_7_bits|0)
            packet_output.append(adj_run_len>>7)

        else:
            color_bit = unique_color_map[cur_run_color]
            assert color_bit in [0,1]
            packet_output.append((adj_run_len<<2)|(color_bit<<1)|1)
    
    return packet_output


            


def process_quantized_image(img, tex_name):

    (x,y) = img.size
    unique_colors_map = {}
    unique_colors_rev_map = []
    raw_pixels = []
    for py in range(y):
        for px in range(x):
            pix = img.getpixel((px,py))
            if px == x-1 or pix == 2:
                pix = 3

            if pix != 3 and pix not in unique_colors_map:
                bits = len(unique_colors_map)
                unique_colors_map[pix] = bits
                unique_colors_rev_map.append(pix)
            
            raw_pixels.append(pix)

    # raw_pixels = global raw palette indexes

    assert len(unique_colors_map) <= 4
    #ez_compressed = rle(raw_pixels, unique_colors_map, 64)
    compressed_packets = rle(raw_pixels, unique_colors_map, 64, 32768)

    raw_pixels_decompressed = un_rle(compressed_packets, unique_colors_rev_map)

    #raw_pixels_decompressed = [unique_colors_rev_map[bits] for bits in decompressed]
    print("#ifndef TEXTURE_{}_H".format(tex_name))
    print("#define TEXTURE_{}_H".format(tex_name))
    #print("{} compressed bytes from {} uncompressed bytes".format(len(compressed_packets), len(raw_pixels)))
    assert raw_pixels_decompressed == raw_pixels
    

    print("u8 comp_tex_{}_packets[{}]".format(tex_name, len(compressed_packets)) + " = {")
    for idx,packet in enumerate(compressed_packets):
        print("{},".format(packet), end=" ")
        if ((idx+1) %32)==0:
            print("")
    print("};")

    print("compressed_texture comp_tex_{}".format(tex_name) + " = {")
    print("    {", end="")
    for local_idx, global_idx in enumerate(unique_colors_rev_map):
        print("{}, ".format(global_idx), end="")
    if len(unique_colors_rev_map):
        print("3", end="") # empty palette just output a white index to satify the compiler
    print("},")
    print("    comp_tex_{}_packets".format(tex_name))
    print("};")
    print("#endif")

    #print("#ifndef TEXTURE_{}_H".format(tex_name))
    #print("#define TEXTURE_{}_H".format(tex_name))
    #print("u8 texture_{}[{}*{}] = ".format(tex_name, x,y) + "{")
    #for py in range(y):
    #    for px in range(x):
    #        pix = img.getpixel((px,py))
    #        #print(pix, end=", ")
    #        #if px % 32 == 0:
    #        #    print("")
    #        unique_colors.add(pix)
    #    #print("")
    ##print("};")
    ##print("#endif")
    #print("unique colors {}".format(unique_colors))

    #assert len(unique_colors) <= 4



if __name__ == '__main__':

    file_name = "Man4_quant.png" #sys.argv[1]
    tex_name = "four_man" #sys.argv[2]
    img = Image.open(file_name)
    process_quantized_image(img, tex_name)