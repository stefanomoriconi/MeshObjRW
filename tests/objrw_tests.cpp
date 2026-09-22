/*
 * objrw_tests.cpp
 * ----------------
 * Progressive, self-checking test suite for objrw.
 *
 * Covers:
 *   1. Version / build info sanity
 *   2. Programmatic mesh construction (push_* helpers)
 *   3. ASCII OBJ write + read round-trip (positions, texcoords, normals)
 *   4. Binary objrw write + read round-trip
 *   5. Auto-detect dispatch (read() picks binary vs ascii by magic)
 *   6. n-gon fan triangulation correctness
 *   7. Negative (relative) indices
 *   8. Per-corner v//vn and v/vt/vn syntax
 *   9. Bounding box, area & volume of a known cube
 *   10. Recomputed normals of a known face
 *   11. Edge cases: comments, blank lines, unknown keywords, empty file
 *
 * Exit code 0 = all passed.
 */
#include "../include/objrw.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

int g_pass = 0;
int g_fail = 0;

#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        if (cond) { ++g_pass; }                                             \
        else {                                                              \
            ++g_fail;                                                       \
            std::fprintf(stderr, "  FAIL: %s (line %d)\n", msg, __LINE__);  \
        }                                                                   \
    } while (0)

bool approx(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
bool approxd(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }

void write_text(const char *path, const std::string &s) {
    std::ofstream f(path, std::ios::binary);
    f.write(s.data(), (std::streamsize)s.size());
}

/* Build a unit cube: 8 vertices, 6 quads (12 tris). */
objrw_mesh_t *make_cube() {
    objrw_mesh_t *m = objrw_mesh_create();
    float V[8][3] = {
        {0,0,0},{1,0,0},{1,1,0},{0,1,0},
        {0,0,1},{1,0,1},{1,1,1},{0,1,1},
    };
    for (int i = 0; i < 8; ++i) objrw_mesh_push_vertex(m, V[i][0], V[i][1], V[i][2]);

    // 6 quads -> fan triangulated to 2 tris each, using v//vn per corner.
    int quads[6][4] = {
        {0,1,2,3}, // bottom (z=0)
        {5,4,7,6}, // top    (z=1)
        {4,0,3,7}, // -x
        {1,5,6,2}, // +x
        {3,2,6,7}, // +y
        {4,5,1,0}, // -y
    };
    for (int q = 0; q < 6; ++q) {
        int a=quads[q][0], b=quads[q][1], c=quads[q][2], d=quads[q][3];
        objrw_mesh_push_triangle(m, a,b,c, OBJRW_NONE,OBJRW_NONE,OBJRW_NONE, OBJRW_NONE,OBJRW_NONE,OBJRW_NONE);
        objrw_mesh_push_triangle(m, a,c,d, OBJRW_NONE,OBJRW_NONE,OBJRW_NONE, OBJRW_NONE,OBJRW_NONE,OBJRW_NONE);
    }
    return m;
}

/* Compare two meshes (positions + connectivity), ignoring normals/texcoords. */
bool same_geometry(const objrw_mesh_t *a, const objrw_mesh_t *b) {
    if (a->num_vertices != b->num_vertices) return false;
    if (a->num_triangles != b->num_triangles) return false;
    for (uint64_t i = 0; i < a->num_vertices; ++i)
        for (int k = 0; k < 3; ++k)
            if (!approx(a->positions[3*i+k], b->positions[3*i+k])) return false;
    for (uint64_t i = 0; i < a->num_triangles * 3; ++i)
        if (a->tri_pos[i] != b->tri_pos[i]) return false;
    return true;
}

} /* namespace */

/* ================================================================== */
int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    std::printf("objrw test suite\n=================\n");
    const char *tmp_obj  = "objrw_tmp_test.obj";
    const char *tmp_bin  = "objrw_tmp_test.objrw";

    /* ---------------- 1. version / build info ---------------- */
    {
        std::printf("[1] version & build info\n");
        CHECK(objrw_version() != nullptr, "version non-null");
        CHECK(std::strcmp(objrw_version(), "1.0.0") == 0, "version == 1.0.0");
        CHECK(objrw_build_info() != nullptr, "build_info non-null");
        std::printf("      %s | %s\n", objrw_version(), objrw_build_info());
    }

    /* ---------------- 2. programmatic construction ---------------- */
    {
        std::printf("[2] programmatic construction\n");
        objrw_mesh_t *m = objrw_mesh_create();
        CHECK(m != nullptr, "mesh created");
        CHECK(objrw_mesh_push_vertex(m, 0,0,0) == OBJRW_OK, "push vertex");
        CHECK(objrw_mesh_push_vertex(m, 1,0,0) == OBJRW_OK, "push vertex");
        CHECK(objrw_mesh_push_vertex(m, 0,1,0) == OBJRW_OK, "push vertex");
        CHECK(objrw_mesh_push_triangle(m, 0,1,2, OBJRW_NONE,OBJRW_NONE,OBJRW_NONE,
                                        OBJRW_NONE,OBJRW_NONE,OBJRW_NONE) == OBJRW_OK, "push triangle");
        CHECK(m->num_vertices == 3, "vertex count");
        CHECK(m->num_triangles == 1, "triangle count");
        objrw_mesh_free(m);
    }

    /* ---------------- 3. ASCII round-trip ---------------- */
    {
        std::printf("[3] ASCII OBJ round-trip\n");
        objrw_mesh_t *a = make_cube();
        CHECK(objrw_write_ascii(tmp_obj, a) == OBJRW_OK, "write ascii");

        objrw_mesh_t *b = objrw_mesh_create();
        CHECK(objrw_read_ascii(tmp_obj, b) == OBJRW_OK, "read ascii");
        CHECK(same_geometry(a, b), "geometry matches after ascii round-trip");
        objrw_mesh_free(a); objrw_mesh_free(b);
    }

    /* ---------------- 4. Binary round-trip ---------------- */
    {
        std::printf("[4] Binary objrw round-trip\n");
        objrw_mesh_t *a = make_cube();
        CHECK(objrw_write_binary(tmp_bin, a) == OBJRW_OK, "write binary");

        objrw_mesh_t *b = objrw_mesh_create();
        CHECK(objrw_read_binary(tmp_bin, b) == OBJRW_OK, "read binary");
        CHECK(same_geometry(a, b), "geometry matches after binary round-trip");
        objrw_mesh_free(a); objrw_mesh_free(b);
    }

    /* ---------------- 4b. Binary round-trip WITH object name ---------------- */
    {
        std::printf("[4b] Binary round-trip with object name\n");
        objrw_mesh_t *a = make_cube();
        std::string nm = "named_cube";
        char *dup = (char *)std::malloc(nm.size() + 1);
        std::memcpy(dup, nm.c_str(), nm.size() + 1);
        a->object_name = dup;
        a->has_object_name = 1;
        CHECK(objrw_write_binary(tmp_bin, a) == OBJRW_OK, "write named binary");

        objrw_mesh_t *b = objrw_mesh_create();
        CHECK(objrw_read_binary(tmp_bin, b) == OBJRW_OK, "read named binary");
        CHECK(same_geometry(a, b), "named geometry matches");
        CHECK(b->has_object_name && std::strcmp(b->object_name, "named_cube") == 0, "name preserved");
        objrw_mesh_free(a); objrw_mesh_free(b);
    }

    /* ---------------- 5. Auto-detect dispatch ---------------- */
    {
        std::printf("[5] Auto-detect (read() dispatch)\n");
        objrw_mesh_t *a = make_cube();
        objrw_write_ascii(tmp_obj, a);
        objrw_write_binary(tmp_bin, a);

        objrw_mesh_t *r1 = objrw_mesh_create();
        CHECK(objrw_read(tmp_obj, r1) == OBJRW_OK, "read ascii via auto-detect");
        CHECK(same_geometry(a, r1), "auto-detect ascii geometry");
        objrw_mesh_free(r1);

        objrw_mesh_t *r2 = objrw_mesh_create();
        CHECK(objrw_read(tmp_bin, r2) == OBJRW_OK, "read binary via auto-detect");
        CHECK(same_geometry(a, r2), "auto-detect binary geometry");
        objrw_mesh_free(r2);

        objrw_mesh_free(a);
    }

    /* ---------------- 6. n-gon fan triangulation ---------------- */
    {
        std::printf("[6] n-gon fan triangulation\n");
        // 5-gon in plane z=0, vertices CCW -> should yield 3 triangles.
        std::string s =
            "v 0 0 0\n"
            "v 2 0 0\n"
            "v 3 1 0\n"
            "v 2 2 0\n"
            "v 0 2 0\n"
            "f 1 2 3 4 5\n";
        write_text(tmp_obj, s);
        objrw_mesh_t *m = objrw_mesh_create();
        CHECK(objrw_read_ascii(tmp_obj, m) == OBJRW_OK, "read ngon");
        CHECK(m->num_vertices == 5, "ngon vertex count");
        CHECK(m->num_triangles == 3, "ngon -> 3 triangles");
        objrw_mesh_free(m);
    }

    /* ---------------- 7. Negative indices ---------------- */
    {
        std::printf("[7] Negative (relative) indices\n");
        // 4 vertices, face uses -4 -3 -2  (== 1 2 3, 0-based)
        std::string s =
            "v 0 0 0\n"
            "v 1 0 0\n"
            "v 0 1 0\n"
            "v 1 1 0\n"
            "f -4 -3 -2\n";
        write_text(tmp_obj, s);
        objrw_mesh_t *m = objrw_mesh_create();
        CHECK(objrw_read_ascii(tmp_obj, m) == OBJRW_OK, "read negative indices");
        CHECK(m->num_triangles == 1, "one triangle");
        if (m->num_triangles == 1) {
            CHECK(m->tri_pos[0] == 0, "neg idx -> 0");
            CHECK(m->tri_pos[1] == 1, "neg idx -> 1");
            CHECK(m->tri_pos[2] == 2, "neg idx -> 2");
        }
        objrw_mesh_free(m);
    }

    /* ---------------- 8. Per-corner v//vn and v/vt/vn ---------------- */
    {
        std::printf("[8] Per-corner attribute syntax\n");
        std::string s =
            "v 0 0 0\n"
            "v 1 0 0\n"
            "v 0 1 0\n"
            "vt 0 0\n"
            "vt 1 0\n"
            "vt 0 1\n"
            "vn 0 0 1\n"
            "f 1/1/1 2/2/1 3/3/1\n";
        write_text(tmp_obj, s);
        objrw_mesh_t *m = objrw_mesh_create();
        CHECK(objrw_read_ascii(tmp_obj, m) == OBJRW_OK, "read per-corner attrs");
        CHECK(m->num_texcoords == 3, "3 texcoords");
        CHECK(m->num_normals == 1, "1 normal");
        if (m->num_triangles == 1) {
            CHECK(m->tri_tex[0] == 0 && m->tri_tex[1] == 1 && m->tri_tex[2] == 2, "tex indices");
            CHECK(m->tri_nor[0] == 0 && m->tri_nor[1] == 0 && m->tri_nor[2] == 0, "nor indices");
        }
        objrw_mesh_free(m);
    }

    /* ---------------- 9. Bounding box + area/volume of unit cube ---------------- */
    {
        std::printf("[9] Bounding box, area, volume (unit cube)\n");
        objrw_mesh_t *m = make_cube();
        float mn[3] = {0}, mx[3] = {0};
        CHECK(objrw_mesh_bounding_box(m, mn, mx) == OBJRW_OK, "bbox");
        CHECK(approx(mn[0],0)&&approx(mn[1],0)&&approx(mn[2],0), "bbox min == 0");
        CHECK(approx(mx[0],1)&&approx(mx[1],1)&&approx(mx[2],1), "bbox max == 1");
        double area = 0, vol = 0;
        CHECK(objrw_mesh_stats(m, &area, &vol) == OBJRW_OK, "stats");
        CHECK(approxd(area, 6.0), "unit cube area == 6");
        CHECK(approxd(std::fabs(vol), 1.0), "unit cube |volume| == 1");
        std::printf("      area=%.4f |volume|=%.4f\n", area, std::fabs(vol));
        objrw_mesh_free(m);
    }

    /* ---------------- 10. Recomputed normals ---------------- */
    {
        std::printf("[10] Recomputed normals\n");
        // Single triangle in XY plane: normal should be +/-Z.
        objrw_mesh_t *m = objrw_mesh_create();
        objrw_mesh_push_vertex(m, 0,0,0);
        objrw_mesh_push_vertex(m, 1,0,0);
        objrw_mesh_push_vertex(m, 0,1,0);
        objrw_mesh_push_triangle(m, 0,1,2, OBJRW_NONE,OBJRW_NONE,OBJRW_NONE,
                                  OBJRW_NONE,OBJRW_NONE,OBJRW_NONE);
        CHECK(objrw_mesh_compute_normals(m) == OBJRW_OK, "compute normals");
        CHECK(m->num_normals == 3, "normal count == vertex count");
        if (m->num_normals == 3) {
            float nz = m->normals[2];
            CHECK(std::fabs(nz) > 0.99f, "normal is along Z");
            float len = std::sqrt(m->normals[0]*m->normals[0] +
                                  m->normals[1]*m->normals[1] +
                                  m->normals[2]*m->normals[2]);
            CHECK(approx(len, 1.0f), "normal normalised");
        }
        objrw_mesh_free(m);
    }

    /* ---------------- 11. Robustness / edge cases ---------------- */
    {
        std::printf("[11] Robustness & edge cases\n");

        // Comments + blank lines + unknown keywords
        std::string s =
            "# a comment\n"
            "\n"
            "mtllib scene.mtl\n"
            "o my_object\n"
            "v 0 0 0\n"
            "usemtl mat1\n"
            "v 1 0 0\n"
            "s off\n"
            "v 0 1 0\n"
            "g group1\n"
            "f 1 2 3\n";
        write_text(tmp_obj, s);
        objrw_mesh_t *m = objrw_mesh_create();
        CHECK(objrw_read_ascii(tmp_obj, m) == OBJRW_OK, "read with comments/keywords");
        CHECK(m->num_vertices == 3 && m->num_triangles == 1, "counts");
        CHECK(m->has_object_name && std::strcmp(m->object_name, "my_object") == 0, "object name kept");
        objrw_mesh_free(m);

        // Empty file -> EMPTY error
        write_text(tmp_obj, "");
        objrw_mesh_t *m2 = objrw_mesh_create();
        CHECK(objrw_read_ascii(tmp_obj, m2) == OBJRW_ERR_EMPTY, "empty file -> EMPTY");
        objrw_mesh_free(m2);

        // Bad index -> INCONSISTENT
        std::string bad = "v 0 0 0\nv 1 0 0\nf 1 2 99\n";
        write_text(tmp_obj, bad);
        objrw_mesh_t *m3 = objrw_mesh_create();
        CHECK(objrw_read_ascii(tmp_obj, m3) == OBJRW_ERR_INCONSISTENT, "bad index -> INCONSISTENT");
        objrw_mesh_free(m3);

        // Binary magic on an ASCII file -> UNSUPPORTED (explicit binary read)
        std::string ascii = "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
        write_text(tmp_bin, ascii);
        objrw_mesh_t *m4 = objrw_mesh_create();
        CHECK(objrw_read_binary(tmp_bin, m4) == OBJRW_ERR_UNSUPPORTED, "ascii passed to binary reader -> UNSUPPORTED");
        objrw_mesh_free(m4);
    }

    /* ---------------- summary ---------------- */
    std::remove(tmp_obj);
    std::remove(tmp_bin);

    std::printf("\n=================\n");
    std::printf("PASS: %d   FAIL: %d\n", g_pass, g_fail);
    std::printf(g_fail == 0 ? "ALL TESTS PASSED\n" : "SOME TESTS FAILED\n", "");
    return g_fail == 0 ? 0 : 1;
}
