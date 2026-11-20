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
        /// <summary>Keep every part, also those GTA cars pass through (foliage, bullet-only collision).</summary>
        public bool KeepAllParts;
        /// <summary>Parts left out because cars don't collide with them, by reason: (parts, triangles).</summary>
        public readonly Dictionary<string, (long parts, long tris)> SkippedParts = new Dictionary<string, (long, long)>();
        /// <summary>Kept parts by their collision flags "type | collides with": triangles. For checking the filter.</summary>
        public readonly Dictionary<string, long> KeptByFlags = new Dictionary<string, long>();

        const EBoundCompositeFlags VehicleHits = EBoundCompositeFlags.VEHICLE_NOT_BVH | EBoundCompositeFlags.VEHICLE_BVH
                                               | EBoundCompositeFlags.VEHICLE_BOX;
        const EBoundCompositeFlags MapTypes = EBoundCompositeFlags.MAP_WEAPON | EBoundCompositeFlags.MAP_DYNAMIC
            | EBoundCompositeFlags.MAP_ANIMAL | EBoundCompositeFlags.MAP_COVER | EBoundCompositeFlags.MAP_VEHICLE;

        public BoundsToMesh(Func<string, ushort> materialIndex) { material = materialIndex; }

        /// <summary>Why a car would pass through this part of a composite, or null if it collides.
        /// Flags1 = what the part is, Flags2 = what it collides with.</summary>
        public static string NotForCars(BoundCompositeChildrenFlags f)
        {
            var type = f.Flags1; var with = f.Flags2;
            if (type == 0 && with == 0) return null;  // no flags: keep
            if ((type & EBoundCompositeFlags.FOLIAGE) != 0) return "foliage (cars drive through)";
            if ((type & EBoundCompositeFlags.MAP_VEHICLE) != 0 || (with & VehicleHits) != 0) return null;
            if ((type & MapTypes) != 0) return $"map parts only for {type & MapTypes}";
            return $"parts that collide only with {with}";
        }

        public void Add(Bounds b, Mesh into) => Add(b, Matrix.Identity, into);

        void Add(Bounds b, Matrix parent, Mesh into)
        {
            if (b == null) return;
            switch (b)
            {
                case BoundComposite comp:
                    var kids = comp.Children?.data_items;
                    if (kids == null) break;
                    foreach (var child in kids)
                    {
                        if (child == null) continue;
                        string why = KeepAllParts ? null : NotForCars(child.CompositeFlags1);
                        int before = into.Count, beforeVerts = into.Verts.Count;
                        Add(child, parent, into);
                        int added = into.Count - before;
                        if (why != null)
                        {
                            into.Truncate(before, beforeVerts);
                            SkippedParts.TryGetValue(why, out var c);
                            SkippedParts[why] = (c.parts + 1, c.tris + added);
                        }
                        else if (added > 0)
                        {
                            string k = $"{child.CompositeFlags1.Flags1} | {child.CompositeFlags1.Flags2}";
                            KeptByFlags.TryGetValue(k, out var t);
                            KeptByFlags[k] = t + added;
                        }
                    }
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
