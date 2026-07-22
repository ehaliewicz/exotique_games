

# load actual palette
# load "mixing" table
from PIL import Image
import numpy as np
import matplotlib
import colour as colour

def create_perceptual_highlight_palette(base_palette_rgb: np.ndarray, boost_factor: float = 1.20) -> np.ndarray:
    """
    Takes an (N, 3) palette in uint8 sRGB [0..255] and returns a perceptually 
    highlighted palette using OKLab lightness scaling.
    """
    # 1. Normalize sRGB to [0.0, 1.0]
    srgb_float = base_palette_rgb.astype(np.float32) / 255.0
    
    # 2. Convert sRGB -> OKLab space
    # OKLab represents color as [Lightness (L), a, b]
    oklab = colour.XYZ_to_Oklab(colour.sRGB_to_XYZ(srgb_float))
    
    # 3. Boost Lightness (L) component perceptually and clamp to 1.0
    oklab[:, 0] = np.minimum(1.0, oklab[:, 0] * boost_factor)
    
    # 4. Convert back: OKLab -> sRGB
    srgb_boosted = colour.XYZ_to_sRGB(colour.Oklab_to_XYZ(oklab))
    
    # 5. Clamp to [0, 1] range and scale back to uint8 [0..255]
    srgb_boosted = np.clip(srgb_boosted, 0.0, 1.0)
    return list((srgb_boosted * 255.0).astype(np.uint8))



def color_difference_fast(rgba1: np.ndarray, rgba2: np.ndarray) -> float:
    """
    Computes a fast, perceptually-weighted RGB distance as a scalar float.
    
    Inputs:
        rgba1, rgba2: uint8 arrays of shape (3,) or (4,) e.g. [R, G, B, A]
    """
    # 1. Handle Transparency
    #a1 = rgba1[3] if len(rgba1) > 3 else 255
    #a2 = rgba2[3] if len(rgba2) > 3 else 255

    #if a1 < 128 and a2 < 128:
    #    return 0.0
    #if (a1 < 128) != (a2 < 128):
    #    return 1000.0

    # 2. Extract channels as float32
    r1, g1, b1 = float(rgba1[0]), float(rgba1[1]), float(rgba1[2])
    r2, g2, b2 = float(rgba2[0]), float(rgba2[1]), float(rgba2[2])

    rmean = (r1 + r2) / 2.0
    dr = r1 - r2
    dg = g1 - g2
    db = b1 - b2

    # Weighting factors based on red average
    weight_r = 2.0 + (rmean / 256.0)
    weight_g = 4.0
    weight_b = 2.0 + ((255.0 - rmean) / 256.0)

    # Sum components into a scalar before taking the square root
    dist_sq = (weight_r * (dr ** 2)) + (weight_g * (dg ** 2)) + (weight_b * (db ** 2))
    return float(np.sqrt(dist_sq))


def get_candidate_combos_for_block(
    block_palette_indices: np.ndarray, 
    master_palette: np.ndarray, 
    highlight_palette: np.ndarray,
    hl_target: int = 0,
    k_candidates: int = 10
) -> tuple[np.ndarray, np.ndarray]:
    """
    Selects the best K candidate palette indices for a 4x4 block and builds
    the (7, N_combos, 3) color candidates array for matrix multiplication.
    
    Inputs:
        block_palette_indices: (16,) uint8 array of master palette indices in the block
        master_palette:        (256, 3) uint8 array of base RGB colors
        highlight_palette:     (256, 3) uint8 array of boosted OKLab RGB colors
        hl_target:             0, 1, or 2 (which header slot gets highlighted)
        k_candidates:          Number of nearest palette entries to test (8 to 12)
        
    Returns:
        candidate_combos: (7, N_combos, 3) float32 array
        grid:             (N_combos, 3) uint8 array mapping back to palette indices
    """
    weights = np.array([2.0, 4.0, 3.0], dtype=np.float32)

    # 1. Extract unique RGBs present in this 4x4 block
    unique_block_indices = np.unique(block_palette_indices)
    block_rgbs = master_palette[unique_block_indices].astype(np.float32) # (U, 3)

    # 2. Compute distance from ALL 256 master palette colors to the block's unique colors
    # master_palette: (256, 1, 3), block_rgbs: (1, U, 3)
    diff = master_palette[:, np.newaxis, :].astype(np.float32) - block_rgbs[np.newaxis, :, :]
    dists_per_color = np.sum((diff ** 2) * weights, axis=-1)  # (256, U)
    
    # Minimum distance to ANY color in the block
    min_dists = np.min(dists_per_color, axis=1)  # (256,)

    # Pick the top K palette indices with smallest distance
    candidate_indices = np.argsort(min_dists)[:k_candidates]  # (K,)

    # 3. Generate all K x K x K candidate triplets
    # grid shape: (K^3, 3) -> each row is [idx0, idx1, idx2]
    grid = np.array(np.meshgrid(candidate_indices, candidate_indices, candidate_indices)).T.reshape(-1, 3)
    n_combos = grid.shape[0]

    # 4. Extract base colors for Index 0, 1, and 2 across all combinations
    c0 = master_palette[grid[:, 0]].astype(np.float32)  # (N_combos, 3)
    c1 = master_palette[grid[:, 1]].astype(np.float32)  # (N_combos, 3)
    c2 = master_palette[grid[:, 2]].astype(np.float32)  # (N_combos, 3)

    # Extract the highlighted color based on hl_target (0, 1, or 2)
    hl_palette_slot = grid[:, hl_target]
    c_hl = highlight_palette[hl_palette_slot].astype(np.float32) # (N_combos, 3)

    # 5. Build the 7 available colors per combination
    # Shape: (7, N_combos, 3)
    candidate_combos = np.stack([
        c0,               # Code 000: Index 0
        (c0 + c1) * 0.5,  # Code 001: Mix 0 + 1
        c1,               # Code 010: Index 1
        (c0 + c2) * 0.5,  # Code 011: Mix 0 + 2
        c2,               # Code 100: Index 2
        (c1 + c2) * 0.5,  # Code 101: Mix 1 + 2
        c_hl              # Code 110: Highlight Target
    ], axis=0)

    return candidate_combos, grid

def main():
    bkgd_img = Image.open("models/background_original.png") #sys.argv[1]
    mix_table_img = Image.open("mix_table.ppm")
    palette_img = Image.open("palette.ppm")
    w,h = bkgd_img.size

    palette = [None] * 256
    for y in range(16):
        for x in range(16):
            idx = y*16+x
            palette[idx] = palette_img.getpixel((x,y))


    mix_table = [[None for _ in range(256)] for _ in range(256)]
    for y in range(256):
        for x in range(256):
            mix_table[y][x] = mix_table_img.getpixel((y,x))

    palette = np.array([[r,g,b] for (r,g,b) in palette], dtype=np.uint8)
    mix_table = np.array(mix_table, dtype=np.uint8)
    highlight_palette = create_perceptual_highlight_palette(palette, 1.2)


    num_blocks_x = w//4
    num_blocks_y = h//4
    assert num_blocks_x*4 == w
    assert num_blocks_y*4 == h

    compressed_blocks = []
    for block_y in range(num_blocks_y):
        for block_x in range(num_blocks_x):
            print("{}/{}".format(block_y*num_blocks_x+block_x, (num_blocks_x*num_blocks_y)))
            block = []
            for y in range(4):
                for x in range(4):
                    yy = block_y*4+y
                    xx = block_x*4+x
                    r,g,b,_ = bkgd_img.getpixel((xx,yy))
                    block.append([r,g,b])
            block = np.asarray(block, dtype=np.uint8)
            
            best_block = None
            best_block_total_diff = None

            unique_colors = set([(r,g,b) for (r,g,b) in block.tolist()])
            #[(col,idx) for idx,col in enumerate(palette)]
            print(unique_colors)
            for pal_idx0 in range(256):
                print(pal_idx0)
                col0 = palette[pal_idx0]
                mix_table_row0 = mix_table[pal_idx0]
                for pal_idx1 in range(pal_idx0+1,256):
                    col1 = palette[pal_idx1]
                    
                    mix_table_row1 = mix_table[pal_idx1]
                    mix01 = mix_table_row0[pal_idx1]

                    for pal_idx2 in range(pal_idx1+1, 256):
                        col2 = palette[pal_idx2]
                        mix02 = mix_table_row0[pal_idx2]
                        mix12 = mix_table_row1[pal_idx2]
                        pal_idxs = [pal_idx0, pal_idx1, pal_idx2]
                        colors = np.stack([col0, col1, col2, mix01, mix12, mix02, mix02], axis=0) #, None (don't need transparency yet)
                        for highlight_pal_idx in range(3):
                            hightlight_base = pal_idxs[highlight_pal_idx]
                            hightlight_idx = highlight_palette[hightlight_base]
                            colors[6] = hightlight_idx

                            #block_total_diff = 0
                            
                            #early_out = False 

                            #block_indexes = [None] * 16


                            #for block_pix_idx,block_pix in enumerate(block):
                                
                            #    differences = [
                            #        (color_difference_fast(block_pix, color),idx) for idx,color in enumerate(colors)
                            #    ]
                            #    best_diff, best_diff_idx = min(differences)



                            #    block_total_diff += best_diff*best_diff
                            #    block_indexes[block_pix_idx] = best_diff_idx

                            #    if best_block_total_diff is not None and block_total_diff > best_block_total_diff:
                            #         early_out = True
                            #         break 
                            
                            #if early_out:
                            #    continue
                        
                            # block becomes shape (1, 1, 16, 3)
                            #diff = colors[:, np.newaxis, :] - block[np.newaxis, :, :]
                            #weights = np.array([2.0, 4.0, 3.0], dtype=np.float32)
                            #sq_err = np.sum((diff ** 2) * weights, axis=-1)  # Shape: (7, 16)

                            #best_codes = np.argmin(sq_err, axis=0)     # Shape: (16,)
                            #total_error = np.sum(np.min(sq_err, axis=0)) # Scalar total block error

                            fast_encode_block_matrix(block, colors)
                            if best_block_total_diff is None or total_error < best_block_total_diff:
                                best_block_total_diff = total_error 
                                # calculate block indexes and everything 
                                best_block = (colors[0:3], highlight_pal_idx, best_codes)
    
            compressed_blocks.append(best_block)



if __name__ == '__main__':
    main()