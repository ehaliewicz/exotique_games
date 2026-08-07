#!/usr/bin/env python3
"""
process_mahjong_tile.py
 
Takes a mahjong tile character image, quantizes it to a fixed palette
(snapping anti-aliased border pixels to the nearest palette color, and
capping the number of distinct non-white colors used so the result is
compatible with a 1-bit white/non-white RLE encoding), scales it to
255x256, and places it in a 256x256 canvas with a single white column
reserved on the right (for the solid-color triangle marker).
 
Usage:
    python process_mahjong_tile.py input.png output.png [--max-colors N]
"""
 
import sys
import argparse
import numpy as np
from PIL import Image
 
# Palette from palette_mahjong.py (0xRRGGBB integers)
PALETTE_HEX = [
    0x000000,
    0x1c6e40,
    0xb09b0a,
    0xffffff,
    0xb30018,
    0x332bcf,
    
]
 
FINAL_SIZE = 256
 
# Character fills the whole canvas except a single reserved column on the
# right (used for the solid-color triangle marker, and forced to white by
# the compressor's `px == x-1` check regardless of what's stored here).
BORDER_LEFT = 10
BORDER_RIGHT = 11
BORDER_TOP = 10
BORDER_BOTTOM = 10

TARGET_W, TARGET_H = FINAL_SIZE-(BORDER_LEFT+BORDER_RIGHT), FINAL_SIZE-(BORDER_TOP+BORDER_BOTTOM)
 
 
def hex_to_rgb(h):
    return ((h >> 16) & 0xFF, (h >> 8) & 0xFF, h & 0xFF)
 
 
def build_palette_array():
    return np.array([hex_to_rgb(h) for h in PALETTE_HEX], dtype=np.float64)
 
 
def to_rgb_array(img: Image.Image) -> np.ndarray:
    """Convert img to an (H, W, 3) float64 RGB array, compositing any
    transparency onto white first so antialiased edges fade toward white
    rather than black."""
    if img.mode in ("RGBA", "LA") or (img.mode == "P" and "transparency" in img.info):
        img = img.convert("RGBA")
        background = Image.new("RGBA", img.size, (255, 255, 255, 255))
        img = Image.alpha_composite(background, img)
 
    img = img.convert("RGB")
    return np.asarray(img, dtype=np.float64)
 
 
def quantize_to_indices(img: Image.Image, palette_rgb: np.ndarray,
                         candidate_indices=None) -> np.ndarray:
    """Snap every pixel in img to the nearest color in palette_rgb and return
    an (H, W) array of palette indices (uint8).
 
    If candidate_indices is given, pixels are only ever snapped to one of
    those palette indices (e.g. [white_idx, red_idx, black_idx]) rather than
    the full palette -- this is what prevents antialiased pixels between two
    solid colors from landing on some unrelated third palette color."""
 
    arr = to_rgb_array(img)
    h, w, _ = arr.shape
    pixels = arr.reshape(-1, 3)  # (H*W, 3)
 
    if candidate_indices is None:
        candidates = palette_rgb
        candidate_indices = np.arange(len(palette_rgb))
    else:
        candidate_indices = np.asarray(candidate_indices)
        candidates = palette_rgb[candidate_indices]
 
    # Compute squared distance from every pixel to every candidate color
    # pixels: (N, 3), candidates: (K, 3) -> dists: (N, K)
    #diffs = pixels[:, None, :] - candidates[None, :, :]
    #dists = np.einsum("nkc,nkc->nk", diffs, diffs)
 
    weights = np.array([0.30, 0.59, 0.11])  # or [0.2126, 0.7152, 0.0722]

    diffs = pixels[:, None, :] - candidates[None, :, :]
    dists = np.sum(weights * diffs**2, axis=2)

    nearest = np.argmin(dists, axis=1)  # (N,) index into `candidates`
    nearest_idx = candidate_indices[nearest]  # map back to real palette indices
    return nearest_idx.reshape(h, w).astype(np.uint8)
 
 
def dominant_non_white_indices(indices: np.ndarray, white_idx: int, name: str, max_colors: int = 2):
    """Return the up-to-`max_colors` most common non-white palette indices
    present in `indices`, ordered by pixel count (descending)."""
    unique, counts = np.unique(indices, return_counts=True)
    non_white = [(int(idx), int(cnt)) for idx, cnt in zip(unique, counts) if idx != white_idx]
    non_white.sort(key=lambda pair: -pair[1])
 
    total_non_white = sum(cnt for _, cnt in non_white)
    dominant = non_white[:max_colors]
    discarded = non_white[max_colors:]
 
    if discarded and total_non_white > 0:
        discarded_frac = sum(cnt for _, cnt in discarded) / total_non_white
        if discarded_frac > 0.02:  # more than a sliver -- likely a real 3rd color, not noise
            print(
                f"Warning: input {name} appears to have more than {max_colors} non-white "
                f"colors ({discarded_frac:.1%} of non-white pixels fall outside the "
                f"top {max_colors}); they will be forced onto the closest of the "
                f"dominant colors."
            )
 
    return [idx for idx, _ in dominant]
 
 
def indices_to_rgb_image(indices: np.ndarray, palette_rgb: np.ndarray) -> Image.Image:
    """Convert an (H, W) index array back into an RGB PIL Image."""
    rgb_arr = palette_rgb[indices].astype(np.uint8)  # (H, W, 3)
    return Image.fromarray(rgb_arr, mode="RGB")
 
 
def indices_to_indexed_png(indices: np.ndarray, palette_hex) -> Image.Image:
    """Build a true indexed-color ('P' mode) PIL Image from an (H, W) index
    array, using palette_hex as the PNG's embedded color table."""
    p_img = Image.fromarray(indices, mode="P")
 
    # PIL's putpalette expects a flat list of 768 (256*3) RGB byte values.
    flat_palette = []
    for hexval in palette_hex:
        flat_palette.extend(hex_to_rgb(hexval))
    # Pad remaining palette slots with 0 (unused) up to 256 colors.
    flat_palette.extend([0, 0, 0] * (256 - len(palette_hex)))
    p_img.putpalette(flat_palette)
 
    return p_img
 
 
def process_tile(input_path: str, max_non_white_colors: int = 2):
    palette_rgb = build_palette_array()
    white_idx = PALETTE_HEX.index(0xFFFFFF)
 
    img = Image.open(input_path)
 
    # 1. First pass: quantize against the full palette just to figure out which
    #    colors are actually "real" in this tile (vs. stray antialias noise).
    full_idx = quantize_to_indices(img, palette_rgb)
    dominant = dominant_non_white_indices(full_idx, white_idx, input_path, max_non_white_colors)
    allowed_indices = [white_idx] + dominant  # e.g. [white, red, black]

    # replace 5's with 0's.  i dont like blue tiles, lets use black :) 
    allowed_indices = [i if i != 5 else 0 for i in allowed_indices]
 
    # 2. Second pass: re-quantize the ORIGINAL pixels, but restrict every pixel
    #    to snap only to white or one of the dominant colors. This is what
    #    guarantees antialiased pixels between e.g. red and black land on
    #    red or black -- never on some unrelated third palette color -- which
    #    is required for the 1-bit (white/non-white) RLE scheme where each
    #    non-white run may only reference up to 2 colors.
    quantized_idx = quantize_to_indices(img, palette_rgb, candidate_indices=allowed_indices)
    quantized_rgb = indices_to_rgb_image(quantized_idx, palette_rgb)
 
    # 3. Scale to 255x256
    resized_rgb = quantized_rgb.resize((TARGET_W, TARGET_H), Image.LANCZOS)
 
    # Resizing interpolates colors again, so re-quantize afterward -- still
    # restricted to the same allowed set, so no new colors can appear.
    resized_idx = quantize_to_indices(resized_rgb, palette_rgb, candidate_indices=allowed_indices)
 
    # 4. Place onto a 256x256 canvas with a single reserved white column on
    #    the right (BORDER_RIGHT = 1px); no border elsewhere.
    final_idx = np.full((FINAL_SIZE, FINAL_SIZE), white_idx, dtype=np.uint8)
    final_idx[BORDER_TOP:BORDER_TOP + TARGET_H, BORDER_LEFT:BORDER_LEFT + TARGET_W] = resized_idx
 
    # Sanity check: confirm the final image really does respect the limit.
    final_colors = set(np.unique(final_idx).tolist()) - {white_idx}
    assert len(final_colors) <= max_non_white_colors, (
        f"Internal error: final image has {len(final_colors)} non-white colors, "
        f"expected at most {max_non_white_colors}"
    )
 
    # 5. Save as a true indexed (palette-mode) PNG
    final_img = indices_to_indexed_png(final_idx, PALETTE_HEX)

    return final_img
    #final_img.save(output_path, optimize=True)
    #print(f"Saved {output_path} ({final_img.size[0]}x{final_img.size[1]}, mode={final_img.mode}); "
    #      f"non-white colors used: {sorted(final_colors)}")
    

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


            


def process_quantized_image(img, tex_name, output):

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
    print("#ifndef TEXTURE_{}_H\n".format(tex_name), file=output)
    print("#define TEXTURE_{}_H\n".format(tex_name), file=output)
    #print("{} compressed bytes from {} uncompressed bytes".format(len(compressed_packets), len(raw_pixels)))
    assert raw_pixels_decompressed == raw_pixels
    

    print("u8 comp_tex_{}_packets[{}]".format(tex_name, len(compressed_packets)) + " = {", file=output)
    for idx,packet in enumerate(compressed_packets):
        print("{},".format(packet), end=" ", file=output)
        if ((idx+1) %32)==0:
            print("", file=output)
    print("};", file=output)

    print("compressed_texture comp_tex_{}".format(tex_name) + " = {", file=output)
    print("    {", end="", file=output)
    for local_idx, global_idx in enumerate(unique_colors_rev_map):
        print("{}, ".format(global_idx), end="", file=output)
        
    if len(unique_colors_rev_map) == 0:
        print("3", end="", file=output) # empty palette just output a white index to satify the compiler
    print("},", file=output)
    print("    comp_tex_{}_packets".format(tex_name), file=output)
    print("};", file=output)
    print("#endif", file=output)

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



def main():
    #parser = argparse.ArgumentParser(description="Quantize, scale, and pad a mahjong tile image.")
    #parser.add_argument("input", help="Path to input image file")
    #parser.add_argument("output", help="Path to output image file")
    #parser.add_argument(
    #@    "--max-colors", type=int, default=2,
    #    help="Max number of non-white palette colors allowed in the output "
    #         "(default: 2, matching a 1-bit white/non-white RLE scheme where "
    #         "each non-white run may reference at most 2 colors)."
    #)
    #args = parser.parse_args()
    files = [
        ("Man1.png", "one_man"),
        ("Man2.png", "two_man"),
        ("Man3.png", "three_man"),
        ("Man4.png", "four_man"),
        ("Man5.png", "five_man"),
        ("Man5-Dora.png", "five_man_red"),
        ("Man6.png", "six_man"),
        ("Man7.png", "seven_man"),
        ("Man8.png", "eight_man"),
        ("Man9.png", "nine_man"),
        ("Pin1.png", "one_pin"),
        ("Pin2.png", "two_pin"),
        ("Pin3.png", "three_pin"),
        ("Pin4.png", "four_pin"),
        ("Pin5.png", "five_pin"),
        ("Pin5-Dora.png", "five_pin_red"),
        ("Pin6.png", "six_pin"),
        ("Pin7.png", "seven_pin"),
        ("Pin8.png", "eight_pin"),
        ("Pin9.png", "nine_pin"),
        ("Sou1.png", "one_sou"),
        ("Sou2.png", "two_sou"),
        ("Sou3.png", "three_sou"),
        ("Sou4.png", "four_sou"),
        ("Sou5.png", "five_sou"),
        ("Sou5-Dora.png", "five_sou_red"),
        ("Sou6.png", "six_sou"),
        ("Sou7.png", "seven_sou"),
        ("Sou8.png", "eight_sou"),
        ("Sou9.png", "nine_sou"),
        ("Pei.png", "north"),
        ("Ton.png", "east"),
        ("Nan.png", "south"),
        ("Shaa.png", "west"),
        ("Chun.png", "red_dragon"),
        ("Haku.png", "white_dragon"),
        ("Hatsu.png", "green_dragon"),
    ]
    for (file, tex_name) in files:
        quantized_image = process_tile("tiles/{}".format(file), max_non_white_colors=2)
        out_file = "texture_{}.h".format(tex_name)
        with open(out_file, "w") as out:
            process_quantized_image(quantized_image, tex_name, out)



if __name__ == "__main__":
    main()