// Commandes de l'écran tactile au poignet (layer/src/shared_block.h, section 6) : mémoire partagée
// "Local\XRPerfOverlay.Commands", créée ici. La layer (jeu OpenXR) ou XRPerfVR (jeu SteamVR) y déposent les
// tuiles touchées qui relèvent de XRPerfSensors (vidéo, direct, capture d'écran, preset d'image) ;
// XRPerfSensors y publie en retour ce que les tuiles doivent montrer.
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Text;

namespace XRPerfSensors
{
    internal sealed class TouchCommands : IDisposable
    {
        public const int Size = 96;
        private const uint Magic = 0x43505258;  // "XRPC"
        private const uint Version = 1;
        private const string Name = @"Local\XRPerfOverlay.Commands";
        private const int Slots = 8;
        private const int OffsetTick = 8, OffsetFlags = 16, OffsetSequence = 20, OffsetPreset = 24, PresetBytes = 32, OffsetPosted = 56, OffsetCommands = 64;

        // Drapeaux publiés : ce que XRPerfSensors peut faire en ce moment.
        public const uint VideoReady = 1, LiveReady = 2, WindowReady = 4;
        // Tuiles exécutées ici (TouchAction de layer/src/touch_pad.h).
        public const int ActionVideo = 0, ActionLive = 1, ActionSnapshot = 2, ActionPreset = 5;

        private const uint PAGE_READWRITE = 0x04;
        private const uint FILE_MAP_WRITE = 0x0002;
        private const uint SDDL_REVISION_1 = 1;
        private static readonly IntPtr InvalidHandle = new IntPtr(-1);
        // Les jeux (comptes ordinaires, applications du Store) y écrivent leurs commandes.
        private const string Sddl = "D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;AU)(A;;GRGW;;;AC)";

        [StructLayout(LayoutKind.Sequential)]
        private struct SECURITY_ATTRIBUTES
        {
            public int nLength;
            public IntPtr lpSecurityDescriptor;
            public int bInheritHandle;
        }

        [DllImport("advapi32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        private static extern bool ConvertStringSecurityDescriptorToSecurityDescriptorW(
            string stringSecurityDescriptor, uint revision, out IntPtr securityDescriptor, IntPtr securityDescriptorSize);

        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        private static extern IntPtr CreateFileMappingW(
            IntPtr file, ref SECURITY_ATTRIBUTES attributes, uint protect, uint maximumSizeHigh, uint maximumSizeLow, string name);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr MapViewOfFile(IntPtr mapping, uint desiredAccess, uint offsetHigh, uint offsetLow, UIntPtr bytes);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool UnmapViewOfFile(IntPtr address);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool CloseHandle(IntPtr handle);

        [DllImport("kernel32.dll")]
        private static extern IntPtr LocalFree(IntPtr memory);

        private IntPtr _mapping;
        private IntPtr _view;
        private int _sequence;
        private uint _read;            // dernière commande exécutée
        private long _waitingSince;    // case pas encore écrite depuis (0 : aucune)

        public TouchCommands()
        {
            if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(Sddl, SDDL_REVISION_1, out IntPtr descriptor, IntPtr.Zero))
                throw new Win32Exception(Marshal.GetLastWin32Error());
            try
            {
                var attributes = new SECURITY_ATTRIBUTES { nLength = Marshal.SizeOf<SECURITY_ATTRIBUTES>(), lpSecurityDescriptor = descriptor };
                _mapping = CreateFileMappingW(InvalidHandle, ref attributes, PAGE_READWRITE, 0, Size, Name);
                if (_mapping == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
            }
            finally
            {
                LocalFree(descriptor);
            }
            _view = MapViewOfFile(_mapping, FILE_MAP_WRITE, 0, 0, new UIntPtr(Size));
            if (_view == IntPtr.Zero)
            {
                int error = Marshal.GetLastWin32Error();
                CloseHandle(_mapping);
                _mapping = IntPtr.Zero;
                throw new Win32Exception(error);
            }
            _sequence = Marshal.ReadInt32(_view, OffsetSequence) & ~1;
            // Commandes déposées avant ce lancement : ignorées (elles ne sont plus d'actualité).
            _read = unchecked((uint)Marshal.ReadInt32(_view, OffsetPosted));
            Marshal.WriteInt32(_view, 0, unchecked((int)Magic));
            Marshal.WriteInt32(_view, 4, (int)Version);
        }

        /// <summary>Publie l'état (plusieurs fois par seconde : sert aussi de signe de vie).</summary>
        public void Publish(uint flags, string preset)
        {
            if (_view == IntPtr.Zero) return;
            byte[] name = new byte[PresetBytes];
            byte[] ascii = Encoding.ASCII.GetBytes(Ascii(preset ?? ""));
            Array.Copy(ascii, name, Math.Min(ascii.Length, PresetBytes - 1));
            Marshal.WriteInt32(_view, OffsetSequence, ++_sequence);  // impair : écriture en cours
            Marshal.WriteInt32(_view, OffsetFlags, unchecked((int)flags));
            Marshal.Copy(name, 0, IntPtr.Add(_view, OffsetPreset), PresetBytes);
            Marshal.WriteInt32(_view, OffsetSequence, ++_sequence);
            Marshal.WriteInt64(_view, OffsetTick, Environment.TickCount64);
        }

        /// <summary>Commandes déposées depuis le dernier appel, dans l'ordre.</summary>
        public List<int> Take()
        {
            var actions = new List<int>();
            if (_view == IntPtr.Zero) return actions;
            uint posted = unchecked((uint)Marshal.ReadInt32(_view, OffsetPosted));
            // Plus de commandes en retard que de cases : les plus anciennes sont perdues.
            if (unchecked(posted - _read) > Slots) _read = unchecked(posted - Slots);
            while (_read != posted)
            {
                uint number = unchecked(_read + 1);
                uint value = unchecked((uint)Marshal.ReadInt32(_view, OffsetCommands + 4 * (int)((number - 1) % Slots)));
                if ((value >> 8) != (number & 0xFFFFFF))
                {
                    // Numéro réservé, case pas encore écrite : relue au prochain passage (abandonnée au bout d'une seconde).
                    if (_waitingSince == 0) _waitingSince = Environment.TickCount64;
                    if (Environment.TickCount64 - _waitingSince < 1000) break;
                }
                else
                {
                    actions.Add((int)(value & 0xFF));
                }
                _waitingSince = 0;
                _read = number;
            }
            return actions;
        }

        // Texte affichable par le casque : lettres sans accents, ASCII imprimable.
        public static string Ascii(string text)
        {
            var sb = new StringBuilder();
            foreach (char c in text.Normalize(NormalizationForm.FormD))
            {
                if (CharUnicodeInfo.GetUnicodeCategory(c) == UnicodeCategory.NonSpacingMark) continue;
                if (c >= ' ' && c <= '~') sb.Append(c);
                else if (c == 'œ') sb.Append("oe");
                else if (c == 'æ') sb.Append("ae");
                else if (c == 'ß') sb.Append("ss");
                else if (!char.IsControl(c)) sb.Append('?');
            }
            return sb.ToString().Trim();
        }

        public void Dispose()
        {
            if (_view != IntPtr.Zero)
            {
                Marshal.WriteInt64(_view, OffsetTick, 0);  // plus personne pour exécuter les commandes
                UnmapViewOfFile(_view);
            }
            if (_mapping != IntPtr.Zero) CloseHandle(_mapping);
            _view = _mapping = IntPtr.Zero;
        }
    }
}
