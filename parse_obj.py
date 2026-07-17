def parse_face(f):
    return tuple(int(s) for s in f.split("/"))


# let's say a 16 vertex cache
def sort_faces(verts, faces, vert_cache_size):

    res = []
    #faces = faces[1:]


    vertex_cache = []
    def add_face_to_cache(face):
        (v0, v1, v2) = face 

        def find_or_add(vidx):
            nonlocal vertex_cache
            for idx in vertex_cache:
                if idx == vidx:
                    return
            # otherwise, pop first element, shift left, and place at end of cache
            if len(vertex_cache) == vert_cache_size:
                vertex_cache.pop()
            vertex_cache = [vidx] + vertex_cache


        find_or_add(v0)
        find_or_add(v1)
        find_or_add(v2)

    def count_verts_in_cache(face):
        (v0, v1, v2) = face
        count = 0
        for idx in vertex_cache:
            if idx == v0 or idx == v1 or idx == v2:
                count += 1
        return count   


    res = [faces[0]]
    add_face_to_cache(faces[0])
    faces = faces[1:]

    cache_hits = 0
    cache_misses = 0
    while len(faces):
        best_res = None
        best_res_face = None

        for face_idx,face in enumerate(faces):
            verts_in_cache = count_verts_in_cache(face)
            if best_res is None or verts_in_cache > best_res:
                best_res_face = face_idx
                best_res = verts_in_cache
        
        picked_face = faces.pop(best_res_face)
        res.append(picked_face)
        cache_hits += best_res 
        cache_misses += (3 - best_res)
        add_face_to_cache(picked_face)




    #print("hits: {}, misses: {}".format(cache_hits, cache_misses))
    #print("hit rate {}".format(cache_hits/(len(res)*3)))
    #exit(1)
    return verts, res

def parse(file):
    verts = []
    vert_norms = []
    vert_uvs = []
    faces = []
    
    unique_verts = {}
    output_norms = []
    output_uvs = []
    output_verts = []

    mirror_uv = 00

    uv_adjust_lookup = {
        "chrome": (729/1024,520/1024),
        "mirrors": (729/1024,520/1024),
        "black_chrome": (333/1024, 605/1024),
        "black_plastic": (861/1024, 422/1024)
    }

    current_material = None
    with open(file) as f:
        for line in f.readlines():
            stripped = line.strip()
            if len(stripped) == 0 or stripped[0] == '#':
                continue
            elif stripped.find("usemtl") == 0:
                current_material = stripped.split("usemtl ")[1]
                continue
            elif stripped.find("v ") == 0:
                verts.append(tuple(float(s) for s in stripped.split(" ")[1:]))

            elif stripped.find("vn ") == 0:
                vert_norms.append(tuple(float(s) for s in stripped.split(" ")[1:]))
            elif stripped.find("vt ") == 0:
                vert_uvs.append(tuple(float(s) for s in stripped.split(" ")[1:]))
            elif stripped.find("f ") == 0:
                adjust_uvs = False 
                if current_material in ["hondaF", "hondaR", "spdglass"]:
                    continue
                if current_material in ["black_plastic", "black_chrome", "chrome", "mirrors"]:
                    adjust_uvs = True 
                    adjusted_uv = uv_adjust_lookup[current_material]
                vert_combos = stripped.split(" ")[1:]
                this_face_verts = []
                
                for vert_combo in vert_combos:
                    
                    unique_vert = tuple(int(i) for i in vert_combo.split("/"))
                    if unique_vert in unique_verts:
                        vert_idx = unique_verts[unique_vert]
                    else:
                        vert_idx = len(unique_verts)
                        unique_verts[unique_vert] = vert_idx
                        output_verts.append(
                            (verts[unique_vert[0]-1],
                             adjusted_uv if adjust_uvs else vert_uvs[unique_vert[1]-1],
                             vert_norms[unique_vert[2]-1])
                        )
                        
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
    vert_cache_size = 4 #sys.argv[2]
    verts, faces = parse("./models/{}.obj".format(obj_name))
    verts, faces = sort_faces(verts, faces, vert_cache_size)
    
    print("obj_vertex {}_vertexes[]".format(obj_name) + " = {")
    for (vert, uv, norm) in verts:
        print("{" + ".pos = {}, .norm = {}, .uv = {}".format(
            format_triplet(vert), format_triplet(norm), format_double(uv)
        ) + "},")
    print("};")

    print("u16 {}_indexes[]".format(obj_name) + " = {")
    for face in faces:
        (v0, v1, v2) = face
        print("{}, {}, {},".format(v0, v1, v2))
    print("};")

    #print(verts)
    #print(faces)