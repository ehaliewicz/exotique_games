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
def parse(file):
    verts = []
    vert_norms = []
    vert_uvs = []
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
                verts.append(tuple(float(s) for s in stripped.split(" ")[1:]))
            elif stripped.find("vn ") == 0:
                vert_norms.append(tuple(float(s) for s in stripped.split(" ")[1:]))
            elif stripped.find("vt ") == 0:
                vert_uvs.append(tuple(float(s) for s in stripped.split(" ")[1:]))
            elif stripped.find("f ") == 0:
               
                vert_combos = stripped.split(" ")[1:]
                this_face_verts = []
                
                for vert_combo in vert_combos:
                    unique_vert = tuple(int(i) if i != '' else 0 for i in vert_combo.split("/"))
                    if unique_vert in unique_verts:
                        vert_idx = unique_verts[unique_vert]
                    else:
                        vert_idx = len(unique_verts)
                        unique_verts[unique_vert] = vert_idx

                        (vidx, uvidx, nidx) = unique_vert
                        rv = verts[vidx-1]
                        ruv = (0.5, 0.5)#(0.333,0.216)
                        if len(vert_uvs) > 0:
                            ruv = vert_uvs[uvidx-1]
                        rn = vert_norms[nidx-1]

                        output_verts.append((rv, ruv, rn))
                        
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
    return "{" + ".x = {}f, .y = {}f, .z = {}f".format(x,y,z) + "}"

def format_double(tup):
    x,y = tup
    return "{" + ".x = {}f, .y = {}f".format(x,y) + "}"

import sys
if __name__ == '__main__':
    obj_name = sys.argv[1]
    vert_cache_size = 16 #sys.argv[2]
    verts, faces = parse("./assets/models/{}.obj".format(obj_name))
    verts, faces = sort_faces(verts, faces, vert_cache_size)

    print("#ifndef {}_mesh_h".format(obj_name))
    print("#define {}_mesh_h".format(obj_name))
    print("const obj_vertex {}_vertexes[{}]".format(obj_name, len(verts)) + " = {")


    for (vert, uv, norm) in verts:
        print("{" + ".pos = {}, .norm = {}, .uv = {}".format(
            format_triplet(vert), format_triplet(norm), format_double(uv)
        ) + "},")
    print("};")

    print("const u16 {}_indexes[{}]".format(obj_name, len(faces*3)) + " = {")
    for face in faces:
        (v0, v1, v2) = face
        print("{}, {}, {},".format(v0, v1, v2))
    print("};")

    print("const obj_mesh {}_mesh".format(obj_name) + " = {")
    print("  .vertexStream = {}_vertexes,".format(obj_name))
    print("  .indexStream = {}_indexes,".format(obj_name))
    print("  .vertexCount = {},".format(len(verts)))
    print("  .indexCount = {}".format(len(faces)*3))
    print("};")

    print("#endif")

    #print(verts)
    #print(faces)