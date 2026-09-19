/*
 * czmesh_cli.cpp
 * --------------
 * Command-line front-end for the czmesh shared library.
 *
 * Usage:
 *   czmesh_cli info      <mesh>             Print mesh statistics
 *   czmesh_cli ascii      <in> <out>        Convert to ASCII OBJ
 *   czmesh_cli binary     <in> <out>        Convert to binary czobj
 *   czmesh_cli normals    <in> <out.obj>    Recompute per-vertex normals, write OBJ
 *   czmesh_cli version                            Print version & build info
 *   czmesh_cli help                               Show this help
 */
#include "../include/czmesh.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

void print_usage() {
    std::printf(
        "czmesh_cli - czmesh command-line tool\n"
        "Usage:\n"
        "  czmesh_cli info    <mesh>                Print mesh statistics\n"
        "  czmesh_cli ascii   <in> <out>            Convert to ASCII OBJ\n"
        "  czmesh_cli binary  <in> <out>            Convert to binary czobj\n"
        "  czmesh_cli normals <in> <out.obj>        Recompute normals, write OBJ\n"
        "  czmesh_cli version                        Print version & build info\n"
        "  czmesh_cli help                           Show this help\n");
}

void report_error(int rc) {
    if (rc != CZMESH_OK) {
        std::fprintf(stderr, "czmesh error (%d): %s\n", (int)rc, czmesh_last_error());
    }
}

int cmd_info(const char *path) {
    czmesh_mesh_t *m = czmesh_mesh_create();
    czmesh_status rc = czmesh_read(path, m);
    if (rc != CZMESH_OK) { report_error(rc); czmesh_mesh_free(m); return 1; }

    float mn[3] = {0}, mx[3] = {0};
    czmesh_mesh_bounding_box(m, mn, mx);
    double area = 0.0, vol = 0.0;
    czmesh_mesh_stats(m, &area, &vol);

    std::printf("file          : %s\n", path);
    std::printf("object name   : %s\n", m->has_object_name && m->object_name ? m->object_name : "(none)");
    std::printf("vertices      : %llu\n", (unsigned long long)m->num_vertices);
    std::printf("texcoords     : %llu\n", (unsigned long long)m->num_texcoords);
    std::printf("normals       : %llu\n", (unsigned long long)m->num_normals);
    std::printf("triangles     : %llu\n", (unsigned long long)m->num_triangles);
    std::printf("bbox min      : %.6f %.6f %.6f\n", mn[0], mn[1], mn[2]);
    std::printf("bbox max      : %.6f %.6f %.6f\n", mx[0], mx[1], mx[2]);
    std::printf("area          : %.6f\n", area);
    std::printf("volume (signed): %.6f\n", vol);
    czmesh_mesh_free(m);
    return 0;
}

int convert(const char *in, const char *out, bool binary) {
    czmesh_mesh_t *m = czmesh_mesh_create();
    czmesh_status rc = czmesh_read(in, m);
    if (rc != CZMESH_OK) { report_error(rc); czmesh_mesh_free(m); return 1; }
    rc = binary ? czmesh_write_binary(out, m) : czmesh_write_ascii(out, m);
    if (rc != CZMESH_OK) { report_error(rc); czmesh_mesh_free(m); return 1; }
    std::printf("wrote %s (%llu triangles)\n", out, (unsigned long long)m->num_triangles);
    czmesh_mesh_free(m);
    return 0;
}

int cmd_normals(const char *in, const char *out) {
    czmesh_mesh_t *m = czmesh_mesh_create();
    czmesh_status rc = czmesh_read(in, m);
    if (rc != CZMESH_OK) { report_error(rc); czmesh_mesh_free(m); return 1; }
    rc = czmesh_mesh_compute_normals(m);
    if (rc != CZMESH_OK) { report_error(rc); czmesh_mesh_free(m); return 1; }
    rc = czmesh_write_ascii(out, m);
    if (rc != CZMESH_OK) { report_error(rc); czmesh_mesh_free(m); return 1; }
    std::printf("wrote %s with %llu recomputed normals\n", out, (unsigned long long)m->num_normals);
    czmesh_mesh_free(m);
    return 0;
}

} /* namespace */

int main(int argc, char **argv) {
    if (argc < 2) { print_usage(); return 1; }
    std::string cmd = argv[1];

    if (cmd == "help" || cmd == "--help" || cmd == "-h") { print_usage(); return 0; }

    if (cmd == "version") {
        std::printf("%s\n", czmesh_version());
        std::printf("%s\n", czmesh_build_info());
        return 0;
    }
    if (cmd == "info" && argc >= 3)    return cmd_info(argv[2]);
    if (cmd == "ascii" && argc >= 4)  return convert(argv[2], argv[3], false);
    if (cmd == "binary" && argc >= 4) return convert(argv[2], argv[3], true);
    if (cmd == "normals" && argc >= 4) return cmd_normals(argv[2], argv[3]);

    std::fprintf(stderr, "unknown or incomplete command: %s\n", cmd.c_str());
    print_usage();
    return 1;
}
