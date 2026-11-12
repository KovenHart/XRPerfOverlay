// Clés de stream (Twitch, YouTube) : gardées chiffrées dans XRPerfSensors.ini par Windows (DPAPI, liées au
// compte Windows : illisibles ailleurs ou par un autre compte), jamais montrées dans l'interface ni écrites
// dans un journal.
using System;
using System.Runtime.InteropServices;
using System.Text;

namespace XRPerfSensors
{
    internal static class StreamKey
    {
        [StructLayout(LayoutKind.Sequential)]
        private struct DATA_BLOB
        {
            public int cbData;
            public IntPtr pbData;
        }

        [DllImport("crypt32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        private static extern bool CryptProtectData(ref DATA_BLOB dataIn, string description, IntPtr entropy, IntPtr reserved, IntPtr prompt, int flags, out DATA_BLOB dataOut);

        [DllImport("crypt32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        private static extern bool CryptUnprotectData(ref DATA_BLOB dataIn, IntPtr description, IntPtr entropy, IntPtr reserved, IntPtr prompt, int flags, out DATA_BLOB dataOut);

        [DllImport("kernel32.dll")]
        private static extern IntPtr LocalFree(IntPtr memory);

        private const int CRYPTPROTECT_UI_FORBIDDEN = 0x1;

        /// <summary>Clé en clair -> texte chiffré (base64) ; "" pour une clé vide ou en cas d'échec.</summary>
        public static string Protect(string key)
        {
            if (string.IsNullOrEmpty(key)) return "";
            return Transform(Encoding.UTF8.GetBytes(key), true) is byte[] data ? Convert.ToBase64String(data) : "";
        }

        /// <summary>Texte chiffré -> clé en clair ; "" si illisible (autre compte, fichier copié).</summary>
        public static string Unprotect(string stored)
        {
            if (string.IsNullOrEmpty(stored)) return "";
            try
            {
                byte[] data = Transform(Convert.FromBase64String(stored), false);
                return data != null ? Encoding.UTF8.GetString(data) : "";
            }
            catch (FormatException)
            {
                return "";
            }
        }

        private static byte[] Transform(byte[] input, bool protect)
        {
            IntPtr buffer = Marshal.AllocHGlobal(Math.Max(1, input.Length));
            try
            {
                Marshal.Copy(input, 0, buffer, input.Length);
                var blobIn = new DATA_BLOB { cbData = input.Length, pbData = buffer };
                DATA_BLOB blobOut;
                bool ok = protect ? CryptProtectData(ref blobIn, "XR Perf Overlay", IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, CRYPTPROTECT_UI_FORBIDDEN, out blobOut)
                                  : CryptUnprotectData(ref blobIn, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, CRYPTPROTECT_UI_FORBIDDEN, out blobOut);
                if (!ok || blobOut.pbData == IntPtr.Zero) return null;
                try
                {
                    var output = new byte[blobOut.cbData];
                    Marshal.Copy(blobOut.pbData, output, 0, blobOut.cbData);
                    return output;
                }
                finally
                {
                    LocalFree(blobOut.pbData);
                }
            }
            catch (Exception)
            {
                return null;
            }
            finally
            {
                Marshal.FreeHGlobal(buffer);
            }
        }

        /// <summary>Clé plausible : caractères visibles, sans espace, 8 à 200.</summary>
        public static bool Valid(string key)
        {
            if (key == null || key.Length < 8 || key.Length > 200) return false;
            foreach (char c in key)
                if (c <= ' ' || c > '~') return false;
            return true;
        }
    }
}
