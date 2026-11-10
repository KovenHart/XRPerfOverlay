// Presets des filtres d'image (page Image) : ceux fournis, et ceux de l'utilisateur, enregistrés dans
// %LOCALAPPDATA%\XRPerfOverlay\ImagePresets.json. Un preset fixe tous les réglages d'image sauf le mode
// (casque ou vidéo) : ce qu'il ne cite pas revient à zéro.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;

namespace XRPerfSensors
{
    internal static class ImagePresets
    {
        // Réglages qu'un preset fixe (noms JSON de SettingsJson.WriteOverlay).
        public static readonly string[] Keys =
        {
            "ImageSharpen", "ImageSharpness", "ImageSharpenLimit", "ImageSharpenRadius", "ImageBrightness", "ImageContrast", "ImageSaturation", "ImageVibrance",
            "ImageGamma", "ImageShadows", "ImageTemperature", "ImageDeband", "ImageColorblind", "ImageHdr",
        };

        public const int MaxName = 40;
        public const int MaxUser = 50;

        private sealed class Preset
        {
            public string Id;      // « lowres »… pour ceux fournis (nom traduit par l'interface), « user:Nom » sinon
            public string Name;    // nom choisi par l'utilisateur
            public string Values;  // objet JSON des réglages
        }

        // Fournis : partir de l'un d'eux, puis ajuster.
        private static readonly Preset[] BuiltIns =
        {
            new Preset { Id = "lowres", Values = "{\"ImageSharpen\":\"rcas\",\"ImageSharpness\":70}" },
            new Preset { Id = "crisp", Values = "{\"ImageSharpen\":\"luma\",\"ImageSharpness\":60,\"ImageSharpenLimit\":6}" },
            new Preset { Id = "vivid", Values = "{\"ImageSharpen\":\"cas\",\"ImageSharpness\":40,\"ImageContrast\":5,\"ImageSaturation\":10,\"ImageVibrance\":40}" },
            new Preset { Id = "hdr", Values = "{\"ImageSharpen\":\"cas\",\"ImageSharpness\":40,\"ImageVibrance\":25,\"ImageHdr\":60}" },
            new Preset { Id = "cinema", Values = "{\"ImageContrast\":10,\"ImageSaturation\":-10,\"ImageVibrance\":15,\"ImageTemperature\":12,\"ImageHdr\":20}" },
            new Preset { Id = "dark", Values = "{\"ImageBrightness\":6,\"ImageGamma\":85,\"ImageShadows\":25}" },
            new Preset { Id = "oled", Values = "{\"ImageShadows\":35,\"ImageDeband\":60}" },
        };

        private static List<Preset> _user;

        private static string FilePath
        {
            get { return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "XRPerfOverlay", "ImagePresets.json"); }
        }

        private static List<Preset> User
        {
            get
            {
                if (_user != null) return _user;
                _user = new List<Preset>();
                try
                {
                    if (!File.Exists(FilePath)) return _user;
                    using (JsonDocument doc = JsonDocument.Parse(File.ReadAllText(FilePath, Encoding.UTF8)))
                    {
                        if (!doc.RootElement.TryGetProperty("presets", out JsonElement list) || list.ValueKind != JsonValueKind.Array) return _user;
                        foreach (JsonElement p in list.EnumerateArray())
                        {
                            if (p.ValueKind != JsonValueKind.Object || !p.TryGetProperty("name", out JsonElement name) || name.ValueKind != JsonValueKind.String ||
                                !p.TryGetProperty("values", out JsonElement values) || values.ValueKind != JsonValueKind.Object) continue;
                            string n = Clean(name.GetString());
                            if (n == null || _user.Count >= MaxUser || Find("user:" + n) != null) continue;
                            _user.Add(new Preset { Id = "user:" + n, Name = n, Values = values.GetRawText() });
                        }
                    }
                }
                catch (Exception)
                {
                    // Fichier abîmé : mis de côté plutôt qu'écrasé au prochain enregistrement.
                    try { File.Copy(FilePath, FilePath + ".illisible", true); }
                    catch (Exception) { }
                }
                return _user;
            }
        }

        private static string Clean(string name)
        {
            if (name == null) return null;
            name = new string(name.Where(c => !char.IsControl(c)).ToArray()).Trim();
            if (name.Length == 0) return null;
            return name.Length > MaxName ? name.Substring(0, MaxName).Trim() : name;
        }

        private static Preset Find(string id)
        {
            foreach (Preset p in BuiltIns)
                if (p.Id == id) return p;
            foreach (Preset p in _user ?? User)
                if (string.Equals(p.Id, id, StringComparison.OrdinalIgnoreCase)) return p;
            return null;
        }

        /// <summary>Réglages d'image à zéro (aucun effet) ; le mode et l'autorisation ne changent pas.</summary>
        public static void Neutral(OverlaySettings o)
        {
            var fresh = new OverlaySettings();
            o.ImageSharpen = fresh.ImageSharpen; o.ImageSharpness = fresh.ImageSharpness;
            o.ImageSharpenLimit = fresh.ImageSharpenLimit; o.ImageSharpenRadius = fresh.ImageSharpenRadius;
            o.ImageBrightness = fresh.ImageBrightness; o.ImageContrast = fresh.ImageContrast; o.ImageSaturation = fresh.ImageSaturation;
            o.ImageVibrance = fresh.ImageVibrance; o.ImageGamma = fresh.ImageGamma; o.ImageShadows = fresh.ImageShadows;
            o.ImageTemperature = fresh.ImageTemperature; o.ImageDeband = fresh.ImageDeband; o.ImageColorblind = fresh.ImageColorblind;
            o.ImageHdr = fresh.ImageHdr;
        }

        private static void Load(OverlaySettings o, string values)
        {
            Neutral(o);
            using (JsonDocument doc = JsonDocument.Parse(values))
                foreach (JsonProperty p in doc.RootElement.EnumerateObject())
                    if (Array.IndexOf(Keys, p.Name) >= 0) SettingsJson.ApplyOverlay(o, p.Name, p.Value);
        }

        // Objet JSON des réglages d'image de `o` (tous les Keys).
        private static void WriteValues(Utf8JsonWriter w, OverlaySettings o)
        {
            w.WriteStartObject();
            w.WriteString("ImageSharpen", o.ImageSharpen);
            w.WriteNumber("ImageSharpness", o.ImageSharpness);
            w.WriteNumber("ImageSharpenLimit", o.ImageSharpenLimit);
            w.WriteNumber("ImageSharpenRadius", o.ImageSharpenRadius);
            w.WriteNumber("ImageBrightness", o.ImageBrightness);
            w.WriteNumber("ImageContrast", o.ImageContrast);
            w.WriteNumber("ImageSaturation", o.ImageSaturation);
            w.WriteNumber("ImageVibrance", o.ImageVibrance);
            w.WriteNumber("ImageGamma", o.ImageGamma);
            w.WriteNumber("ImageShadows", o.ImageShadows);
            w.WriteNumber("ImageTemperature", o.ImageTemperature);
            w.WriteNumber("ImageDeband", o.ImageDeband);
            w.WriteString("ImageColorblind", o.ImageColorblind);
            w.WriteNumber("ImageHdr", o.ImageHdr);
            w.WriteEndObject();
        }

        /// <summary>Liste pour l'interface : [{ id, name, builtin, values }], fournis d'abord.</summary>
        public static void Write(Utf8JsonWriter w)
        {
            w.WriteStartArray();
            foreach (Preset p in BuiltIns.Concat(User))
            {
                var o = new OverlaySettings();
                try { Load(o, p.Values); }
                catch (JsonException) { continue; }
                w.WriteStartObject();
                w.WriteString("id", p.Id);
                w.WriteString("name", p.Name ?? "");
                w.WriteBoolean("builtin", p.Name == null);
                w.WritePropertyName("values");
                WriteValues(w, o);
                w.WriteEndObject();
            }
            w.WriteEndArray();
        }

        /// <summary>Applique un preset ; mode « Désactivés » : passe à « Dans le casque » pour qu'il se voie.</summary>
        public static bool Apply(OverlaySettings o, string id)
        {
            Preset p = id == null ? null : Find(id);
            if (p == null) return false;
            try { Load(o, p.Values); }
            catch (JsonException) { return false; }
            if (o.ImageMode == "off") o.ImageMode = "headset";
            return true;
        }

        /// <summary>Enregistre les réglages actuels sous `name` (remplace un preset du même nom).</summary>
        public static bool Save(OverlaySettings o, string name)
        {
            name = Clean(name);
            if (name == null) return false;
            List<Preset> user = User;
            var buffer = new MemoryStream();
            using (var w = new Utf8JsonWriter(buffer)) WriteValues(w, o);
            string values = Encoding.UTF8.GetString(buffer.ToArray());
            Preset existing = user.FirstOrDefault(p => string.Equals(p.Name, name, StringComparison.OrdinalIgnoreCase));
            if (existing != null)
                existing.Values = values;
            else if (user.Count >= MaxUser)
                return false;
            else
                user.Add(new Preset { Id = "user:" + name, Name = name, Values = values });
            return Persist();
        }

        /// <summary>Supprime un preset de l'utilisateur.</summary>
        public static bool Delete(string id)
        {
            List<Preset> user = User;
            int removed = user.RemoveAll(p => string.Equals(p.Id, id, StringComparison.OrdinalIgnoreCase));
            return removed > 0 && Persist();
        }

        private static bool Persist()
        {
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(FilePath));
                var buffer = new MemoryStream();
                using (var w = new Utf8JsonWriter(buffer, new JsonWriterOptions { Indented = true }))
                {
                    w.WriteStartObject();
                    w.WriteStartArray("presets");
                    foreach (Preset p in User)
                    {
                        w.WriteStartObject();
                        w.WriteString("name", p.Name);
                        w.WritePropertyName("values");
                        using (JsonDocument doc = JsonDocument.Parse(p.Values)) doc.RootElement.WriteTo(w);
                        w.WriteEndObject();
                    }
                    w.WriteEndArray();
                    w.WriteEndObject();
                }
                string temp = FilePath + ".tmp";
                File.WriteAllBytes(temp, buffer.ToArray());
                if (File.Exists(FilePath)) File.Replace(temp, FilePath, null);
                else File.Move(temp, FilePath);
                return true;
            }
            catch (Exception)
            {
                return false;
            }
        }
    }
}
