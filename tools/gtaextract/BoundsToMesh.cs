using System;
using System.Collections.Generic;
using CodeWalker.GameFiles;
using SharpDX;

namespace GtaExtract
{
    /// <summary>Turns CodeWalker collision (Bounds) into triangles in the bounds' own space.</summary>
    public sealed class BoundsToMesh
    {
        readonly Func<string, ushort> material;
        public int SkippedSpheres;

        public BoundsToMesh(Func<string, ushort> materialIndex) { material = materialIndex; }

        public void Add(Bounds b, Mesh into) => Add(b, Matrix.Identity, into);

        void Add(Bounds b, Matrix parent, Mesh into)
        {
            if (b == null) return;
            switch (b)
            {
                case BoundComposite comp:
                    var kids = comp.Children?.data_items;
                    if (kids != null) foreach (var child in kids) Add(child, parent, into);
                    break;
                case BoundGeometry geom:  // also BoundBVH
                    AddGeometry(geom, parent, into);
                    break;
                case BoundBox box:
                    into.AabbBox(box.BoxMin, box.BoxMax, box.Transform * parent, Mat(box));
                    break;
                case BoundCylinder _:
                case BoundCapsule _:
                case BoundDisc _:
                    // round primitives: their bounding box is close enough for a car to collide with
                    into.AabbBox(b.BoxMin, b.BoxMax, b.Transform * parent, Mat(b));
                    break;
                case BoundSphere _:
                    SkippedSpheres++;
                    break;
            }
        }

        ushort Mat(Bounds b) => material(((BoundsMaterialType)b.MaterialIndex).ToString());

        void AddGeometry(BoundGeometry g, Matrix parent, Mesh into)
        {
            var polys = g.Polygons;
            if (polys == null) return;
            Vector3 P(int i) => Vector3.TransformCoordinate(g.GetVertexPos(i), parent);  // GetVertexPos: + centre, child transform
            foreach (var poly in polys)
            {
                if (poly == null) continue;
                ushort mat = material(poly.Material.Type.ToString());
                switch (poly)
                {
                    case BoundPolygonTriangle t:
                        into.Tri(P(t.vertIndex1), P(t.vertIndex2), P(t.vertIndex3), mat);
                        break;
                    case BoundPolygonBox bx:
                        into.Box4(new[] { P(bx.boxIndex1), P(bx.boxIndex2), P(bx.boxIndex3), P(bx.boxIndex4) }, mat);
                        break;
                    case BoundPolygonCapsule cap:
                        into.Prism(P(cap.capsuleIndex1), P(cap.capsuleIndex2), cap.capsuleRadius, mat);
                        break;
                    case BoundPolygonCylinder cyl:
                        into.Prism(P(cyl.cylinderIndex1), P(cyl.cylinderIndex2), cyl.cylinderRadius, mat);
                        break;
                    case BoundPolygonSphere _:
                        SkippedSpheres++;
                        break;
                }
            }
        }
    }
}
