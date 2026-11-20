using System;
using System.Collections.Generic;
using SharpDX;

namespace GtaExtract
{
    /// <summary>Triangles in one coordinate space, with a material name index per triangle.</summary>
    public sealed class Mesh
    {
        public readonly List<Vector3> Verts = new List<Vector3>();
        public readonly List<int> Tris = new List<int>();   // 3 per triangle
        public readonly List<ushort> Mats = new List<ushort>();
        readonly Dictionary<Vector3, int> index = new Dictionary<Vector3, int>();

        public int Count => Mats.Count;

        /// <summary>Undo everything added after Count was `tris` and Verts.Count was `verts`.</summary>
        public void Truncate(int tris, int verts)
        {
            for (int i = verts; i < Verts.Count; i++) index.Remove(Verts[i]);
            Verts.RemoveRange(verts, Verts.Count - verts);
            Tris.RemoveRange(tris * 3, Tris.Count - tris * 3);
            Mats.RemoveRange(tris, Mats.Count - tris);
        }

        public int Vert(Vector3 v)
        {
            if (!index.TryGetValue(v, out int i)) { i = Verts.Count; Verts.Add(v); index[v] = i; }
            return i;
        }

        public void Tri(Vector3 a, Vector3 b, Vector3 c, ushort mat)
        {
            int ia = Vert(a), ib = Vert(b), ic = Vert(c);
            if (ia == ib || ib == ic || ia == ic) return;
            Tris.Add(ia); Tris.Add(ib); Tris.Add(ic);
            Mats.Add(mat);
        }

        /// <summary>Triangle facing away from `centre` (closed shapes made here face outwards, like GTA's own).</summary>
        public void OutwardTri(Vector3 a, Vector3 b, Vector3 c, Vector3 centre, ushort mat)
        {
            var n = Vector3.Cross(b - a, c - a);
            if (Vector3.Dot(n, (a + b + c) / 3f - centre) < 0) Tri(a, c, b, mat); else Tri(a, b, c, mat);
        }

        /// <summary>GTA box polygon: 4 alternating corners; the other 4 are mirrored through the centre.</summary>
        public void Box4(Vector3[] v, ushort mat)
        {
            var c = (v[0] + v[1] + v[2] + v[3]) / 4f;
            var o = new Vector3[4];
            for (int i = 0; i < 4; i++) o[i] = 2f * c - v[i];
            for (int i = 0; i < 4; i++)
                for (int j = i + 1; j < 4; j++)
                {
                    int k = -1, l = -1;
                    for (int x = 0; x < 4; x++) if (x != i && x != j) { if (k < 0) k = x; else l = x; }
                    OutwardTri(v[i], o[k], v[j], c, mat);
                    OutwardTri(v[i], v[j], o[l], c, mat);
                }
        }

        /// <summary>Axis-aligned box (in the given transform's space) from min/max corners.</summary>
        public void AabbBox(Vector3 mn, Vector3 mx, Matrix m, ushort mat)
        {
            var p = new Vector3[8];
            for (int i = 0; i < 8; i++)
                p[i] = Vector3.TransformCoordinate(new Vector3((i & 1) != 0 ? mx.X : mn.X, (i & 2) != 0 ? mx.Y : mn.Y,
                                                              (i & 4) != 0 ? mx.Z : mn.Z), m);
            var c = Vector3.TransformCoordinate((mn + mx) / 2f, m);
            int[][] faces = { new[] { 0, 1, 3, 2 }, new[] { 4, 5, 7, 6 }, new[] { 0, 1, 5, 4 },
                              new[] { 2, 3, 7, 6 }, new[] { 0, 2, 6, 4 }, new[] { 1, 3, 7, 5 } };
            foreach (var f in faces)
            {
                OutwardTri(p[f[0]], p[f[1]], p[f[2]], c, mat);
                OutwardTri(p[f[0]], p[f[2]], p[f[3]], c, mat);
            }
        }

        /// <summary>8-sided column from a to b with the given radius (capsules and cylinders: poles, trunks, bollards).</summary>
        public void Prism(Vector3 a, Vector3 b, float radius, ushort mat, int sides = 8)
        {
            var axis = b - a;
            float len = axis.Length();
            if (len < 1e-4f || radius <= 0) return;
            axis /= len;
            var side = Math.Abs(axis.Z) < 0.9f ? Vector3.Cross(axis, Vector3.UnitZ) : Vector3.Cross(axis, Vector3.UnitX);
            side.Normalize();
            var up = Vector3.Cross(axis, side);
            var c = (a + b) / 2f;
            var ra = new Vector3[sides];
            var rb = new Vector3[sides];
            for (int i = 0; i < sides; i++)
            {
                double t = 2 * Math.PI * i / sides;
                var off = (float)Math.Cos(t) * radius * side + (float)Math.Sin(t) * radius * up;
                ra[i] = a + off; rb[i] = b + off;
            }
            for (int i = 0; i < sides; i++)
            {
                int j = (i + 1) % sides;
                OutwardTri(ra[i], ra[j], rb[j], c, mat);
                OutwardTri(ra[i], rb[j], rb[i], c, mat);
                if (i > 0 && j > 0)
                {
                    OutwardTri(ra[0], ra[i], ra[j], c, mat);  // end caps
                    OutwardTri(rb[0], rb[j], rb[i], c, mat);
                }
            }
        }
    }
}
