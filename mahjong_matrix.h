#ifndef MAHJONG_MATRIX_H
#define MAHJONG_MATRIX_H

typedef struct {
    float m[4][4];
} matrix;

typedef struct {
    vert3f position;
    vert3f rotation;
    vert3f scale;
} transform;

matrix mat_mul_mat(const matrix *a, const matrix *b) { 
    matrix r; 
    for (int i = 0; i < 4; ++i) { 
         r.m[i][0] = (a->m[i][0] * b->m[0][0] + a->m[i][1] * b->m[1][0] + a->m[i][2] * b->m[2][0] + a->m[i][3] * b->m[3][0]);
         r.m[i][1] = (a->m[i][0] * b->m[0][1] + a->m[i][1] * b->m[1][1] + a->m[i][2] * b->m[2][1] + a->m[i][3] * b->m[3][1]);
         r.m[i][2] = (a->m[i][0] * b->m[0][2] + a->m[i][1] * b->m[1][2] + a->m[i][2] * b->m[2][2] + a->m[i][3] * b->m[3][2]);
         r.m[i][3] = (a->m[i][0] * b->m[0][3] + a->m[i][1] * b->m[1][3] + a->m[i][2] * b->m[2][3] + a->m[i][3] * b->m[3][3]);
    } 
    return r; 
}

matrix mat_inverse_affine(const matrix *a) {
    float m00 = a->m[0][0], m01 = a->m[0][1], m02 = a->m[0][2];
    float m10 = a->m[1][0], m11 = a->m[1][1], m12 = a->m[1][2];
    float m20 = a->m[2][0], m21 = a->m[2][1], m22 = a->m[2][2];

    float det = m00 * (m11 * m22 - m12 * m21) - m01 * (m10 * m22 - m12 * m20) + m02 * (m10 * m21 - m11 * m20);

    // det == 0 means non-invertible (e.g. a zero scale somewhere in the chain)
    float invDet = 1.0f / det;

    matrix r = {0};

    r.m[0][0] = (m11 * m22 - m12 * m21) * invDet;
    r.m[0][1] = (m02 * m21 - m01 * m22) * invDet;
    r.m[0][2] = (m01 * m12 - m02 * m11) * invDet;

    r.m[1][0] = (m12 * m20 - m10 * m22) * invDet;
    r.m[1][1] = (m00 * m22 - m02 * m20) * invDet;
    r.m[1][2] = (m02 * m10 - m00 * m12) * invDet;

    r.m[2][0] = (m10 * m21 - m11 * m20) * invDet;
    r.m[2][1] = (m01 * m20 - m00 * m21) * invDet;
    r.m[2][2] = (m00 * m11 - m01 * m10) * invDet;

    // t' = -R^-1 * t
    float tx = a->m[0][3], ty = a->m[1][3], tz = a->m[2][3];
    r.m[0][3] = -(r.m[0][0] * tx + r.m[0][1] * ty + r.m[0][2] * tz);
    r.m[1][3] = -(r.m[1][0] * tx + r.m[1][1] * ty + r.m[1][2] * tz);
    r.m[2][3] = -(r.m[2][0] * tx + r.m[2][1] * ty + r.m[2][2] * tz);

    r.m[3][0] = 0.0f; r.m[3][1] = 0.0f; r.m[3][2] = 0.0f; r.m[3][3] = 1.0f;
    return r;
}

matrix scale_matrix(vert3f scale) { 
    matrix r = {0}; 
    r.m[0][0] = scale.x; 
    r.m[1][1] = scale.y; 
    r.m[2][2] = scale.z;
    r.m[3][3] = 1.0f; 
    return r; 
} 

matrix translation_matrix(vert3f translate) { 
    matrix r = {0}; 
    r.m[0][0] = 1.0f; 
    r.m[1][1] = 1.0f; 
    r.m[2][2] = 1.0f;
    r.m[3][3] = 1.0f; 
    r.m[0][3] = translate.x; 
    r.m[1][3] = translate.y;
    r.m[2][3] = translate.z; 
    return r; 
} 

matrix rotation_x_matrix(f32 angle) { 
    f32 s = sinf(angle); 
    f32 c = cosf(angle); 
    matrix r = { {
        {1,0,0,0},
        {0,c,-s,0},
        {0,s,c,0},
        {0,0,0,1}
        }
    };
    return r; 
}

matrix rotation_y_matrix(f32 angle) { 
    f32 s = sinf(angle); 
    f32 c = cosf(angle); 
    matrix r = { {
        {c,0,s,0},
        {0,1,0,0},
        {-s,0,c,0},
        {0,0,0,1}
        }
    };
    return r; 
}

matrix rotation_z_matrix(f32 angle) { 
    f32 s = sinf(angle); 
    f32 c = cosf(angle); 
    matrix r = { {
        {c,-s,0,0},
        {s,c,0,0},
        {0,0,1,0},
        {0,0,0,1}
        }
    };
    return r; 
}

matrix transform_to_matrix(const transform *t) { 
    matrix sc = scale_matrix(
        t->scale
    );
    matrix tr = translation_matrix(t->position);
    matrix rx = rotation_x_matrix(t->rotation.x);
    matrix ry = rotation_y_matrix(t->rotation.y);
    matrix rz = rotation_z_matrix(t->rotation.z);

    matrix r = mat_mul_mat(&rz, &ry);
    r = mat_mul_mat(&r, &rx);
    matrix rs = mat_mul_mat(&r, &sc);
    return mat_mul_mat(&tr, &rs);

}

matrix transform_to_view_matrix(const transform *cam)
{
    matrix t = translation_matrix(scale_vert3(cam->position, -1.0f));
    matrix rx = rotation_x_matrix(-cam->rotation.x);
    matrix ry = rotation_y_matrix(-cam->rotation.y);
    matrix r = mat_mul_mat(&rx, &ry);
    return mat_mul_mat(&r, &t);
}

vert3f mat_mul_vert3(const matrix *m, const vert3f *v) {
    vert3f r;
    r.x = m->m[0][0] * v->x + m->m[0][1] * v->y + m->m[0][2] * v->z + m->m[0][3];
    r.y = m->m[1][0] * v->x + m->m[1][1] * v->y + m->m[1][2] * v->z + m->m[1][3];
    r.z = m->m[2][0] * v->x + m->m[2][1] * v->y + m->m[2][2] * v->z + m->m[2][3];
    return r;
}

vert3f mat_mul_normal(const matrix *m, const vert3f *n) {
    vert3f r;
    r.x = m->m[0][0] * n->x + m->m[0][1] * n->y + m->m[0][2] * n->z;
    r.y = m->m[1][0] * n->x + m->m[1][1] * n->y + m->m[1][2] * n->z;
    r.z = m->m[2][0] * n->x + m->m[2][1] * n->y + m->m[2][2] * n->z;

    return normalize(r);
}


void mat_mul_vert3_batch(const matrix *m, const f32_vec poses_soa[3], f32_vec out_comps[3]) {
    f32_vec vx = poses_soa[0];
    f32_vec vy = poses_soa[1];
    f32_vec vz = poses_soa[2];

    f32_vec rx = broadcast_f32_vec(m->m[0][0]) * vx + broadcast_f32_vec(m->m[0][1]) * vy + broadcast_f32_vec(m->m[0][2]) * vz + broadcast_f32_vec(m->m[0][3]);
    f32_vec ry = broadcast_f32_vec(m->m[1][0]) * vx + broadcast_f32_vec(m->m[1][1]) * vy + broadcast_f32_vec(m->m[1][2]) * vz + broadcast_f32_vec(m->m[1][3]);
    f32_vec rz = broadcast_f32_vec(m->m[2][0]) * vx + broadcast_f32_vec(m->m[2][1]) * vy + broadcast_f32_vec(m->m[2][2]) * vz + broadcast_f32_vec(m->m[2][3]);

    out_comps[0] = rx;
    out_comps[1] = ry;
    out_comps[2] = rz;
}

void mat_mul_normal_batch(const matrix *m, const f32_vec norm_comp_soa[3], f32_vec out_comps[3]) {
    /* Transpose 4 vertices into SoA lanes */
    f32_vec vx = norm_comp_soa[0];
    f32_vec vy = norm_comp_soa[1];
    f32_vec vz = norm_comp_soa[2];

    f32_vec rx = broadcast_f32_vec(m->m[0][0]) * vx + broadcast_f32_vec(m->m[0][1]) * vy + broadcast_f32_vec(m->m[0][2]) * vz;
    f32_vec ry = broadcast_f32_vec(m->m[1][0]) * vx + broadcast_f32_vec(m->m[1][1]) * vy + broadcast_f32_vec(m->m[1][2]) * vz;
    f32_vec rz = broadcast_f32_vec(m->m[2][0]) * vx + broadcast_f32_vec(m->m[2][1]) * vy + broadcast_f32_vec(m->m[2][2]) * vz;

    out_comps[0] = rx;
    out_comps[1] = ry;
    out_comps[2] = rz;
}



#endif