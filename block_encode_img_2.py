

import numpy as np
from PIL import Image

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
    return (srgb_boosted * 255.0).astype(np.uint8)


def get_smart_candidates(block_pixels, master_palette, weights, k=10):
    unique_block_rgbs = np.unique(block_pixels, axis=0).astype(np.float32)
    
    # 1. Distances from palette to block colors
    diff = master_palette[:, None, :] - unique_block_rgbs[None, :, :]
    dists = np.sum((diff ** 2) * weights, axis=-1)
    
    # Direct nearest neighbors
    nearest_idx = np.argsort(np.min(dists, axis=1))[:k//2]
    
    # 2. Add extrema candidates (min/max bounds of the block) to capture gradients/contrast
    min_rgb = np.min(unique_block_rgbs, axis=0)
    max_rgb = np.max(unique_block_rgbs, axis=0)
    extrema_rgbs = np.array([min_rgb, max_rgb], dtype=np.float32)
    
    diff_ext = master_palette[:, None, :] - extrema_rgbs[None, :, :]
    extrema_dists = np.sum((diff_ext ** 2) * weights, axis=-1)
    extrema_idx = np.argsort(np.min(extrema_dists, axis=1))[:k - len(nearest_idx)]
    
    # Combine unique candidates
    candidates = np.unique(np.concatenate([nearest_idx, extrema_idx]))
    return candidates

# =============================================================================
# ENCODER & DECODER
# =============================================================================

def encode_block_raw_rgb(block_rgb_4x4, master_palette, highlight_palette, mixing_table, k=10):
    """
    Directly matches a (4, 4, 3) raw RGB block against candidate colors.
    """
    weights = np.array([2.0, 4.0, 3.0], dtype=np.float32)
    
    # Reshape (4, 4, 3) raw image pixels to (16, 3)
    block_pixels = block_rgb_4x4.reshape(16, 3)
    
    # Find K nearest palette entries to the unique RGB colors in this raw block
    #unique_block_rgbs = np.unique(block_pixels, axis=0).astype(np.float32)
    #diff = master_palette[:, None, :] - unique_block_rgbs[None, :, :]
    #min_dists = np.min(np.sum((diff ** 2) * weights, axis=-1), axis=1)
    #candidates = np.argsort(min_dists)[:k]

    candidates = get_smart_candidates(block_pixels, master_palette, weights, k)


    # Build triplet matrix across all 3 highlight targets -> shape (3 * K^3, 4)
    triplets = np.array(np.meshgrid(candidates, candidates, candidates)).T.reshape(-1, 3)
    configs = np.vstack([np.hstack([triplets, np.full((len(triplets), 1), hl)]) for hl in range(3)])

    i0, i1, i2 = configs[:, 0], configs[:, 1], configs[:, 2]

    # Fetch precomputed mix and highlight indices
    i01, i02, i12 = mixing_table[i0, i1], mixing_table[i0, i2], mixing_table[i1, i2]
    hl_slots = np.choose(configs[:, 3], [i0, i1, i2])

    # Resolve candidate RGBs: Shape (7, N_configs, 3)
    candidate_combos = np.stack([
        master_palette[i0], master_palette[i01], master_palette[i1],
        master_palette[i02], master_palette[i2], master_palette[i12],
        highlight_palette[hl_slots]
    ], axis=0).astype(np.float32)

    # Fast Matrix Multiplication against RAW RGB pixels
    b_w = block_pixels.astype(np.float32) * np.sqrt(weights)
    c_w = candidate_combos * np.sqrt(weights)
    
    sq_err = (np.sum(c_w**2, axis=-1)[:, :, None] + 
              np.sum(b_w**2, axis=-1)[None, None, :] - 
              2.0 * (c_w.reshape(-1, 3) @ b_w.T).reshape(7, -1, 16))

    best_config_idx = np.argmin(np.sum(np.min(sq_err, axis=0), axis=1))
    
    return {
        "idx0": int(configs[best_config_idx, 0]),
        "idx1": int(configs[best_config_idx, 1]),
        "idx2": int(configs[best_config_idx, 2]),
        "hl_target": int(configs[best_config_idx, 3]),
        "pixel_codes": np.argmin(sq_err, axis=0)[best_config_idx].astype(np.uint8)
    }


def decode_block(data, master_palette, highlight_palette, mixing_table):
    i0, i1, i2, hl = data["idx0"], data["idx1"], data["idx2"], data["hl_target"]
    hl_idx = [i0, i1, i2][min(hl, 2)]

    lut = np.stack([
        master_palette[i0], master_palette[mixing_table[i0, i1]], master_palette[i1],
        master_palette[mixing_table[i0, i2]], master_palette[i2], master_palette[mixing_table[i1, i2]],
        highlight_palette[hl_idx], np.array([0, 0, 0], dtype=np.uint8)
    ])
    return lut[data["pixel_codes"]].reshape(4, 4, 3)


# =============================================================================
# RUN ON RAW IMAGE
# =============================================================================

# Load raw RGB image directly
img = Image.open("models/background_original_dithered.png").convert("RGB")
img_np = np.array(img, dtype=np.uint8)

# 1. Load Palettes and Mix Table from files
palette_img = Image.open("palette.ppm")
mix_table_img = Image.open("mix_table.ppm")

# Extract 256 RGB palette
palette = np.zeros((256, 3), dtype=np.uint8)
rev_palette = {}
for y in range(16):
    for x in range(16):
        pal_pix = palette_img.getpixel((x,y))
        idx = y * 16 + x
        palette[idx] = pal_pix
        rev_palette[pal_pix] = idx

# Extract 256x256 Mix Table
mix_table = np.zeros((256, 256), dtype=np.uint8)
for y in range(256):
    print("mix table row {}".format(y))
    for x in range(256):
        mix_table_color = mix_table_img.getpixel((x, y))
        mix_table[y][x] = rev_palette[mix_table_color]
       

# Create highlight palette (using your function)
highlight_palette = create_perceptual_highlight_palette(palette, 1.2)

h, w, _ = img_np.shape
decoded_img_np = np.zeros_like(img_np)

# Loop over 4x4 raw RGB patches directly
blocks = []
for y in range(0, h, 4):
    print("encode row {}".format(y))
    for x in range(0, w, 4):
        raw_rgb_block = img_np[y:y+4, x:x+4]
        
        # Encode raw RGBs
        block_data = encode_block_raw_rgb(raw_rgb_block, palette, highlight_palette, mix_table, k=10)
        
        # Decode back to RGB
        decoded_img_np[y:y+4, x:x+4] = decode_block(block_data, palette, highlight_palette, mix_table)

# Save result
Image.fromarray(decoded_img_np, "RGB").save("output2.png")

# write out result 
