using System;
using CodeWalker.GameFiles;
using SharpDX;

namespace GtaExtract
{
    /// <summary>Checks the geometry helpers and writes a small known .gtac (read back by surfacegen's tests).</summary>
    public static class SelfTest
    {
        static int failures;
        static void Check(bool ok, string what) { if (!ok) { failures++; Console.WriteLine("FAIL: " + what); } }

        public static int Run(string outPath)
        {
            var box = new Mesh();
            box.Box4(new[] { new Vector3(0, 0, 0), new Vector3(1, 1, 0), new Vector3(1, 0, 1), new Vector3(0, 1, 1) }, 0);
            Check(box.Count == 12 && box.Verts.Count == 8, $"box polygon: 12 triangles on 8 corners (got {box.Count}/{box.Verts.Count})");
            Check(AllOutward(box, new Vector3(0.5f, 0.5f, 0.5f)), "box faces point outwards");

            var col = new Mesh();
            col.Prism(new Vector3(0, 0, 0), new Vector3(0, 0, 3), 0.2f, 0);
            Check(col.Count == 8 * 2 + 6 * 2, $"8-sided column: 16 side + 12 cap triangles (got {col.Count})");
            Check(AllOutward(col, new Vector3(0, 0, 1.5f)), "column faces point outwards");

            var aabb = new Mesh();
            aabb.AabbBox(new Vector3(-1, -2, 0), new Vector3(1, 2, 1), Matrix.Translation(10, 0, 0), 0);
            Check(aabb.Count == 12 && Math.Abs(aabb.Verts[0].X - 9) < 1e-5, "box primitive placed by its transform");

            var cut = new Mesh();
            cut.Tri(new Vector3(0, 0, 0), new Vector3(1, 0, 0), new Vector3(0, 1, 0), 0);
            cut.Tri(new Vector3(5, 0, 0), new Vector3(6, 0, 0), new Vector3(5, 1, 0), 0);
            cut.Truncate(1, 3);
            cut.Tri(new Vector3(5, 0, 0), new Vector3(6, 0, 0), new Vector3(5, 1, 0), 0);
            Check(cut.Count == 2 && cut.Verts.Count == 6, "truncate removes triangles and their new corners");

            BoundCompositeChildrenFlags F(EBoundCompositeFlags a, EBoundCompositeFlags b) =>
                new BoundCompositeChildrenFlags { Flags1 = a, Flags2 = b };
            Check(BoundsToMesh.NotForCars(F(0, 0)) == null, "part without flags is kept");
            Check(BoundsToMesh.NotForCars(F(EBoundCompositeFlags.MAP_WEAPON | EBoundCompositeFlags.MAP_VEHICLE,
                EBoundCompositeFlags.VEHICLE_BVH)) == null, "map part for vehicles is kept");
            Check(BoundsToMesh.NotForCars(F(EBoundCompositeFlags.MAP_WEAPON, EBoundCompositeFlags.PROJECTILE)) != null,
                "bullet-only map part is left out");
            Check(BoundsToMesh.NotForCars(F(EBoundCompositeFlags.FOLIAGE, EBoundCompositeFlags.VEHICLE_BVH)) != null,
                "foliage is left out");
            Check(BoundsToMesh.NotForCars(F(EBoundCompositeFlags.OBJECT, EBoundCompositeFlags.VEHICLE_NOT_BVH)) == null,
                "object part that vehicles hit is kept");

            using (var w = new GtacWriter(outPath))
            {
                var tri = new Mesh();
                tri.Tri(new Vector3(0, 0, 0), new Vector3(1, 0, 0), new Vector3(0, 1, 0), w.Material("TARMAC"));
                // placed at (100, 200, 30), turned 90 degrees about Z, scaled x2
                var q = Quaternion.RotationAxis(Vector3.UnitZ, (float)(Math.PI / 2));
                w.Add(tri, new Vector3(100, 200, 30), q, new Vector3(2, 2, 2), w.Source("obj:test_prop"));
                w.Add(box, Vector3.Zero, Quaternion.Identity, Vector3.One, w.Source("box_0.ybn.xml"));
            }
            Console.WriteLine(failures == 0 ? "self-test OK" : $"self-test: {failures} failure(s)");
            return failures == 0 ? 0 : 1;
        }

        static bool AllOutward(Mesh m, Vector3 centre)
        {
            for (int t = 0; t < m.Count; t++)
            {
                var a = m.Verts[m.Tris[3 * t]]; var b = m.Verts[m.Tris[3 * t + 1]]; var c = m.Verts[m.Tris[3 * t + 2]];
                if (Vector3.Dot(Vector3.Cross(b - a, c - a), (a + b + c) / 3f - centre) <= 0) return false;
            }
            return true;
        }
    }
}
