using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using SharpDX;

namespace GtaExtract
{
    /// <summary>
    /// .gtac: world-space collision for tools/surfacegen (read by surfacegen.bulk.load_gtac). Little endian:
    ///   "GTACOL1\0", u32 version=1, u32 nverts, u32 ntris, u32 nnames, u32 nfiles,
    ///   f32 verts[nverts*3] (GTA x, y, z), u32 tris[ntris*3], u16 mat[ntris], u32 file[ntris],
    ///   nnames x (u16 length + UTF-8) material names, nfiles x (u16 length + UTF-8) source names.
    /// Sections are streamed to temporary files and joined at the end, so memory stays small.
    /// </summary>
    public sealed class GtacWriter : IDisposable
    {
        readonly string path;
        readonly string[] tmp;
        readonly BinaryWriter verts, tris, mats, files;
        public readonly Dictionary<string, ushort> Materials = new Dictionary<string, ushort>();
        public readonly List<string> Sources = new List<string>();
        readonly Dictionary<string, uint> sourceIndex = new Dictionary<string, uint>();
        public long VertCount, TriCount;

        public GtacWriter(string outPath)
        {
            path = outPath;
            tmp = new[] { outPath + ".v.tmp", outPath + ".t.tmp", outPath + ".m.tmp", outPath + ".f.tmp" };
            verts = new BinaryWriter(File.Create(tmp[0]));
            tris = new BinaryWriter(File.Create(tmp[1]));
            mats = new BinaryWriter(File.Create(tmp[2]));
            files = new BinaryWriter(File.Create(tmp[3]));
        }

        public ushort Material(string name)
        {
            if (!Materials.TryGetValue(name, out ushort i)) { i = (ushort)Materials.Count; Materials[name] = i; }
            return i;
        }

        public uint Source(string name)
        {
            if (!sourceIndex.TryGetValue(name, out uint i)) { i = (uint)Sources.Count; Sources.Add(name); sourceIndex[name] = i; }
            return i;
        }

        /// <summary>Write a mesh placed in the world: p' = position + rotate(orientation, scale * p).</summary>
        public void Add(Mesh m, Vector3 position, Quaternion orientation, Vector3 scale, uint source)
        {
            if (m.Count == 0) return;
            long baseIndex = VertCount;
            foreach (var v in m.Verts)
            {
                var w = position + Vector3.Transform(v * scale, orientation);
                verts.Write(w.X); verts.Write(w.Y); verts.Write(w.Z);
            }
            VertCount += m.Verts.Count;
            if (VertCount > uint.MaxValue) throw new InvalidOperationException("too many vertices for the .gtac format");
            foreach (int i in m.Tris) tris.Write((uint)(baseIndex + i));
            foreach (ushort mt in m.Mats) { mats.Write(mt); files.Write(source); }
            TriCount += m.Count;
        }

        public void Dispose()
        {
            foreach (var w in new[] { verts, tris, mats, files }) w.Dispose();
            using (var o = new BinaryWriter(File.Create(path)))
            {
                o.Write(Encoding.ASCII.GetBytes("GTACOL1\0"));
                o.Write(1u);
                o.Write((uint)VertCount); o.Write((uint)TriCount);
                o.Write((uint)Materials.Count); o.Write((uint)Sources.Count);
                foreach (var t in tmp) using (var s = File.OpenRead(t)) s.CopyTo(o.BaseStream);
                var names = new string[Materials.Count];
                foreach (var kv in Materials) names[kv.Value] = kv.Key;
                foreach (var n in names) WriteString(o, n);
                foreach (var n in Sources) WriteString(o, n);
            }
            foreach (var t in tmp) File.Delete(t);
        }

        static void WriteString(BinaryWriter o, string s)
        {
            var b = Encoding.UTF8.GetBytes(s ?? "");
            o.Write((ushort)b.Length);
            o.Write(b);
        }
    }
}
