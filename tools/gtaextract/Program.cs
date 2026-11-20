using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using CodeWalker.GameFiles;
using SharpDX;

namespace GtaExtract
{
    /// <summary>
    /// GtaExtract: all collision a car can touch in GTA V, placed in the world, in one .gtac file.
    ///   GtaExtract --gta "C:\Program Files\Epic Games\GTAVEnhanced" --out work\gta_collision.gtac
    /// Passes: interiors (MLO instances: tunnels, underpasses), placed objects (map entities with built-in
    /// collision: freeway pieces, trees, ...), map collision (.ybn, normal before hi@, never ma@).
    /// Newest DLC/patch version of every file wins (CodeWalker's DLC handling).
    /// </summary>
    public static class Program
    {
        public static int Main(string[] args)
        {
            if (args.Contains("--self-test")) return SelfTest.Run(Arg(args, "--out") ?? "selftest.gtac");
            string gta = Arg(args, "--gta");
            string outPath = Arg(args, "--out") ?? "gta_collision.gtac";
            bool fragments = args.Contains("--fragments");
            bool props = !args.Contains("--no-objects");
            bool keepDynamic = args.Contains("--keep-movable");
            if (gta == null || !Directory.Exists(gta))
            {
                Console.Error.WriteLine("usage: GtaExtract --gta <GTA V folder> --out <file.gtac> [--fragments] [--no-objects] [--keep-movable] [--all-parts]");
                return 2;
            }
            bool gen9 = File.Exists(Path.Combine(gta, "GTA5_Enhanced.exe"));
            var sw = Stopwatch.StartNew();
            Log($"GTA folder: {gta} ({(gen9 ? "Enhanced" : "Legacy")})");

            GTA5Keys.LoadFromPath(gta, gen9, null);
            var cache = new GameFileCache(4L * 1024 * 1024 * 1024, 60, gta, gen9, "", false, "Installers;_CommonRedist");
            cache.EnableDlc = true;  // newest DLC/patch version of every file
            cache.LoadVehicles = false;
            cache.LoadPeds = false;
            cache.LoadAudio = false;
            cache.Init(s => { if (s.StartsWith("Loading") || s.StartsWith("Scan")) Log(s); }, e => Log("CodeWalker: " + e));
            BoundsMaterialTypes.Init(cache);  // material names (TARMAC, GRASS, ...) from materials.dat
            Log($"scanned in {sw.Elapsed.TotalSeconds:0}s: {cache.YmapDict.Count} map files, {cache.YbnDict.Count} collision files, " +
                $"{BoundsMaterialTypes.Materials?.Count ?? 0} materials");
            if ((BoundsMaterialTypes.Materials?.Count ?? 0) == 0) Log("WARNING: materials.dat not loaded - every surface will become road");

            using (var w = new GtacWriter(outPath))
            {
                var conv = new BoundsToMesh(w.Material) { KeepAllParts = args.Contains("--all-parts") };
                var placedInteriors = new HashSet<uint>();
                var meshCache = new Dictionary<uint, Mesh>();  // archetype -> collision in its own space
                long entities = 0, withCollision = 0, interiors = 0, ymapsDone = 0, movable = 0;
                var movableNames = new Dictionary<string, int>();

                foreach (var entry in cache.YmapDict.Values)
                {
                    YmapFile ymap;
                    try { ymap = cache.RpfMan.GetFile<YmapFile>(entry); }
                    catch (Exception ex) { Log($"skipped map file {entry.Name}: {ex.Message}"); continue; }
                    if (++ymapsDone % 1000 == 0) Log($"  map files {ymapsDone} / {cache.YmapDict.Count}: {interiors} interiors, {withCollision} objects with collision");
                    if (ymap?.AllEntities == null) continue;
                    foreach (var ent in ymap.AllEntities)
                    {
                        uint hash = ent._CEntityDef.archetypeName.Hash;
                        if (ent.IsMlo)
                        {
                            var mesh = InteriorMesh(cache, conv, meshCache, hash);
                            if (mesh == null) continue;
                            w.Add(mesh, ent.Position, ent.Orientation, Vector3.One, w.Source($"{Name(hash)}@{++interiors}"));
                            placedInteriors.Add(hash);
                            continue;
                        }
                        if (!props) continue;
                        var lod = ent._CEntityDef.lodLevel;
                        if (lod != rage__eLodType.LODTYPES_DEPTH_HD && lod != rage__eLodType.LODTYPES_DEPTH_ORPHANHD) continue;
                        if ((ent._CEntityDef.flags & 4) != 0) continue;  // the map turns this object's collision off
                        entities++;
                        if (!keepDynamic && IsMovable(cache, hash))
                        {
                            // GTA lets cars push these away (cones, bins, chairs, ...): a fixed wall in AC would be wrong
                            movable++;
                            string mn = Name(hash);
                            movableNames.TryGetValue(mn, out int k);
                            movableNames[mn] = k + 1;
                            continue;
                        }
                        var m = ObjectMesh(cache, conv, meshCache, hash, fragments);
                        if (m == null || m.Count == 0) continue;
                        withCollision++;
                        w.Add(m, ent.Position, ent.Orientation, ent.Scale, w.Source("obj:" + Name(hash)));
                    }
                }
                Log($"placed {interiors} interiors and {withCollision} of {entities} objects ({sw.Elapsed.TotalMinutes:0.0} min); " +
                    $"left out {movable} movable objects, most common: " +
                    string.Join(", ", movableNames.OrderByDescending(kv => kv.Value).Take(12).Select(kv => $"{kv.Key} {kv.Value}")));

                // Map collision: world-space .ybn files. Normal before hi@ (cars drive on the normal ones), never ma@,
                // and not the interiors' own files (placed above, in their local coordinates otherwise).
                var names = cache.YbnDict.Values.Select(e => e.Name.ToLowerInvariant()).ToHashSet();
                long ybns = 0;
                foreach (var kv in cache.YbnDict)
                {
                    string name = kv.Value.Name.ToLowerInvariant();
                    if (name.StartsWith("ma@")) continue;
                    if (name.StartsWith("hi@") && names.Contains(name.Substring(3))) continue;
                    if (placedInteriors.Contains(kv.Key)) continue;
                    YbnFile ybn;
                    try { ybn = cache.RpfMan.GetFile<YbnFile>(kv.Value); }
                    catch (Exception ex) { Log($"skipped {name}: {ex.Message}"); continue; }
                    if (ybn?.Bounds == null) continue;
                    var mesh = new Mesh();
                    conv.Add(ybn.Bounds, mesh);
                    w.Add(mesh, Vector3.Zero, Quaternion.Identity, Vector3.One, w.Source(name + ".xml"));
                    if (++ybns % 2000 == 0) Log($"  collision files {ybns} / {cache.YbnDict.Count}");
                }
                Log($"map collision: {ybns} files. Total {w.TriCount:N0} triangles, {w.Materials.Count} materials; " +
                    $"{conv.SkippedSpheres} sphere shapes skipped");
                foreach (var kv in conv.SkippedParts.OrderByDescending(kv => kv.Value.tris))
                    Log($"  left out {kv.Value.tris:N0} triangles in {kv.Value.parts:N0} {kv.Key}");
                Log("  kept triangles by collision flags (type | collides with):");
                foreach (var kv in conv.KeptByFlags.OrderByDescending(kv => kv.Value).Take(10))
                    Log($"    {kv.Value,12:N0}  {kv.Key}");
            }
            Log($"wrote {outPath} ({new FileInfo(outPath).Length / 1e6:0} MB) in {sw.Elapsed.TotalMinutes:0.0} min");
            return 0;
        }

        static Mesh InteriorMesh(GameFileCache cache, BoundsToMesh conv, Dictionary<uint, Mesh> meshes, uint hash)
        {
            if (meshes.TryGetValue(hash, out var m)) return m;
            m = null;
            var e = cache.GetYbnEntry(hash);
            if (e != null)
            {
                try
                {
                    var ybn = cache.RpfMan.GetFile<YbnFile>(e);
                    if (ybn?.Bounds != null) { m = new Mesh(); conv.Add(ybn.Bounds, m); }
                }
                catch (Exception ex) { Log($"interior {Name(hash)}: {ex.Message}"); }
            }
            meshes[hash] = m;
            return m;
        }

        const uint ArchetypeDynamic = 131072;  // CBaseArchetypeDef flag "Dynamic": GTA lets it move when hit

        static bool IsMovable(GameFileCache cache, uint hash)
        {
            var arch = cache.GetArchetype(hash);
            return arch != null && !(arch is MloArchetype) && (arch._BaseArchetypeDef.flags & ArchetypeDynamic) != 0;
        }

        static readonly Dictionary<uint, YddFile> ydds = new Dictionary<uint, YddFile>();

        static Mesh ObjectMesh(GameFileCache cache, BoundsToMesh conv, Dictionary<uint, Mesh> meshes, uint hash, bool fragments)
        {
            if (meshes.TryGetValue(hash, out var m)) return m;
            m = new Mesh();
            try
            {
                var arch = cache.GetArchetype(hash);
                if (arch != null && !(arch is MloArchetype))
                {
                    Bounds bound = null;
                    if (arch.DrawableDict.Hash != 0)
                    {
                        if (!ydds.TryGetValue(arch.DrawableDict.Hash, out var ydd))
                        {
                            var e = cache.GetYddEntry(arch.DrawableDict.Hash);
                            ydd = e != null ? cache.RpfMan.GetFile<YddFile>(e) : null;
                            if (ydds.Count > 200) ydds.Clear();  // keep memory bounded
                            ydds[arch.DrawableDict.Hash] = ydd;
                        }
                        if (ydd?.Dict != null && ydd.Dict.TryGetValue(hash, out var d)) bound = d.Bound;
                    }
                    else
                    {
                        var e = cache.GetYdrEntry(hash);
                        if (e != null) bound = cache.RpfMan.GetFile<YdrFile>(e)?.Drawable?.Bound;
                        else if (fragments)
                        {
                            var fe = cache.GetYftEntry(hash);
                            var frag = fe != null ? cache.RpfMan.GetFile<YftFile>(fe)?.Fragment : null;
                            bound = frag?.Drawable?.Bound ?? frag?.PhysicsLODGroup?.PhysicsLOD1?.Bound;
                        }
                    }
                    if (bound != null) conv.Add(bound, m);
                }
            }
            catch (Exception ex) { Log($"object {Name(hash)}: {ex.Message}"); }
            meshes[hash] = m;
            return m;
        }

        static string Name(uint hash)
        {
            var s = JenkIndex.GetString(hash);
            return string.IsNullOrEmpty(s) ? "0x" + hash.ToString("x8") : s;
        }

        static string Arg(string[] a, string key)
        {
            int i = Array.IndexOf(a, key);
            return i >= 0 && i + 1 < a.Length ? a[i + 1] : null;
        }

        public static void Log(string s) => Console.WriteLine($"[{DateTime.Now:HH:mm:ss}] {s}");
    }
}
