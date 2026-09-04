from collections import deque, defaultdict
import time
from contextlib import contextmanager

@contextmanager
def code_timer(block_name="Code block"):
    start = time.perf_counter()
    try:
        yield
    finally:
        end = time.perf_counter()
        print(f"[{block_name}] took {end - start:.6f} seconds")

def parse_face(f):
    return tuple(int(s) for s in f.split("/"))


# let's say a 16 vertex cache
def sort_faces(verts, faces, vert_cache_size):
    #print("sorting faces")

    vertex_set = set()

    def count_verts_in_cache(face):
        return (
            (face[0] in vertex_set) +
            (face[1] in vertex_set) +
            (face[2] in vertex_set)
        )

    def new_verts_needed(face):
        return 3 - count_verts_in_cache(face)

    def add_face_to_cache(face):
        for v in face:
            vertex_set.add(v)

    res = []

    remaining = list(faces)

    while remaining:
        vertex_set.clear()

        batch_faces = []

        while len(batch_faces) < 16 and remaining:
            best_idx = -1
            best_score = -1

            # Global search
            for i, face in enumerate(remaining):
                score = count_verts_in_cache(face)

                if score > best_score:
                    best_score = score
                    best_idx = i

            face = remaining[best_idx]

            # Does it fit the C batch cache?
            needed = new_verts_needed(face)

            if len(vertex_set) + needed > vert_cache_size:
                break

            # Accept triangle
            batch_faces.append(face)
            add_face_to_cache(face)

            remaining[best_idx] = remaining[-1]
            remaining.pop()

        res.extend(batch_faces)

        # If we somehow got stuck (bad vertex limit)
        if not batch_faces and remaining:
            res.append(remaining.pop())

    return verts, res


def quantize_pos(pos):
    scaled_pos = (pos * 32768/1.79) # scale down to -1,0.999 roughly :)
    int_pos = int(scaled_pos)
    assert int_pos > -32789 and int_pos < 32768, (int_pos, pos)
    return int_pos

def quantize_norm(norm):
    (x,y,z) = norm
    # 1. Project onto an octahedron (L1 norm normalization)
    l1_norm = abs(x) + abs(y) + abs(z)
    
    if l1_norm > 0.0:
        x /= l1_norm
        y /= l1_norm
    else:
        x, y = 0.0, 0.0

    # 2. If Z is negative, fold the corners of the octahedral square
    if z < 0.0:
        old_x = x
        x = (1.0 - abs(y)) * (1.0 if old_x >= 0.0 else -1.0)
        y = (1.0 - abs(old_x)) * (1.0 if y >= 0.0 else -1.0)

    # 3. Map float [-1.0, 1.0] to float [0.0, 255.0] (for 8-bit unsigned space)
    u_float = (x + 1.0) * 127.5
    v_float = (y + 1.0) * 127.5

    # 4. Quantize to 8-bit integers with rounding and clamping
    u_8bit = max(0, min(255, round(u_float)))
    v_8bit = max(0, min(255, round(v_float)))

    # 5. Pack into a single 16-bit unsigned integer (U lower byte, V upper byte)
    #packed_16bit = (v_8bit << 8) | u_8bit
    return (u_8bit, v_8bit)



def parse(file):
    verts = []
    vert_norms = []
    faces = []
    
    unique_verts = {}
    output_verts = []


    with open(file) as f:
        for line in f.readlines():
            #if idx % 100 == 0:
            #    print("{}".format(idx))
            stripped = line.strip()
            if len(stripped) == 0 or stripped[0] == '#':
                continue
            elif stripped.find("usemtl") == 0:
                continue
            elif stripped.find("v ") == 0:
                verts.append(tuple(quantize_pos(float(s)) for s in stripped.split(" ")[1:]))
            elif stripped.find("vn ") == 0:
                norm_vec = tuple(float(s) for s in stripped.split(" ")[1:])
                qnorm = quantize_norm(norm_vec)
                vert_norms.append(qnorm)
            elif stripped.find("vt ") == 0:
                pass
                #vert_uvs.append(tuple(float(s) for s in stripped.split(" ")[1:]))
            elif stripped.find("f ") == 0:
               
                vert_combos = stripped.split(" ")[1:]
                this_face_verts = []
                
                for vert_combo in vert_combos:
                    unique_vert = tuple(int(i) if i != '' else 0 for i in vert_combo.split("/"))
                    vidx, _, nidx = unique_vert
                    unique_vert = (vidx, nidx)

                    if unique_vert in unique_verts:
                        vert_idx = unique_verts[unique_vert]
                    else:
                        vert_idx = len(unique_verts)
                        unique_verts[unique_vert] = vert_idx

                        (vidx, nidx) = unique_vert
                        rv = verts[vidx-1]
                        rn = vert_norms[nidx-1]

                        output_verts.append((rv, rn))
                        
                    this_face_verts.append(vert_idx)
                if len(this_face_verts) == 4:
                    (v1,v2,v3,v4) = this_face_verts
                    faces.append((v1,v2,v3))
                    faces.append((v1,v3,v4))
                    #faces.append((v1,v2,v4))
                    #faces.append((v2,v3,v4))
                else:
                    assert len(this_face_verts) == 3, "{} verts".format(len(this_face_verts))

                    faces.append(tuple(this_face_verts))


        return (output_verts, faces)

def format_triplet(tup):
    x,y,z = tup
    return "{" + "{}, {}, {}".format(x,y,z) + "}"

def format_double(tup):
    x,y = tup
    return "{" + "{}, {}".format(x,y) + "}"

import sys
if __name__ == '__main__':
    obj_name = sys.argv[1]
    vert_cache_size = 16 #sys.argv[2]
    verts, faces = parse("./assets/models/{}.obj".format(obj_name))
    verts, faces = sort_faces(verts, faces, vert_cache_size)

    print("#ifndef {}_mesh_h".format(obj_name))
    print("#define {}_mesh_h".format(obj_name))
    print("const compressed_obj_vertex {}_vertexes[{}]".format(obj_name, len(verts)) + " = {")


    for (vert, norm) in verts:
        print("{" + ".pos = {}, .norm = {}".format(
            format_triplet(vert), format_double(norm), 
        ) + "},")
    print("};")

    print("const u16 {}_indexes[{}]".format(obj_name, len(faces*3)) + " = {")
    for face in faces:
        (v0, v1, v2) = face
        print("{}, {}, {},".format(v0, v1, v2))
    print("};")

    print("const compressed_obj_mesh {}_mesh".format(obj_name) + " = {")
    print("  .vertexStream = {}_vertexes,".format(obj_name))
    print("  .indexStream = {}_indexes,".format(obj_name))
    print("  .vertexCount = {},".format(len(verts)))
    print("  .indexCount = {}".format(len(faces)*3))
    print("};")

    print("#endif")

    #print(verts)
    #print(faces)