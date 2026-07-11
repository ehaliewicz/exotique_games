def parse_face(f):
    return tuple(int(s) for s in f.split("/"))


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

if __name__ == '__main__':
    motorcycle_obj = "./src/models/Honda_Shadow_RS_2010.obj"
    verts, faces = parse("./src/models/mahjong_tile.obj")
    
    print("obj_vertex vertexes[] = {")
    for (vert, uv, norm) in verts:
        print("{" + ".pos = {}, .norm = {}, .uv = {}".format(
            format_triplet(vert), format_triplet(norm), format_double(uv)
        ) + "},")
    print("};")

    print("u32 indexes[] = {")
    for face in faces:
        (v0, v1, v2) = face
        print("{}, {}, {},".format(v0, v1, v2))
    print("};")

    #print(verts)
    #print(faces)