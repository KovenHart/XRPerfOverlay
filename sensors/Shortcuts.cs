// Raccourcis « XR Perf Studio » sur le Bureau et dans le menu Démarrer de l'utilisateur, vers XRPerfSensors.
// Installer.bat les crée ; la page Interface les ajoute ou les retire ; uninstall.bat les supprime.
// Lancé alors qu'il tourne déjà, XRPerfSensors ouvre simplement sa fenêtre (Program.Main).
using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace XRPerfSensors
{
    internal static class Shortcuts
    {
        public const string Name = "XR Perf Studio.lnk";

        public static string DesktopPath { get { return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), Name); } }
        public static string StartMenuPath { get { return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Programs), Name); } }

        public static bool OnDesktop { get { return File.Exists(DesktopPath); } }
        public static bool InStartMenu { get { return File.Exists(StartMenuPath); } }

        /// <summary>Crée (true) ou retire (false) le raccourci. false si Windows refuse.</summary>
        public static bool Set(bool desktop, bool on)
        {
            string path = desktop ? DesktopPath : StartMenuPath;
            try
            {
                if (!on)
                {
                    if (File.Exists(path)) File.Delete(path);
                    return true;
                }
                Create(path);
                return File.Exists(path);
            }
            catch (Exception)
            {
                return false;
            }
        }

        // Raccourci Windows par l'objet WScript.Shell (présent sur tous les Windows).
        private static void Create(string path)
        {
            string exe = Application.ExecutablePath;
            Type type = Type.GetTypeFromProgID("WScript.Shell");
            if (type == null) throw new InvalidOperationException("WScript.Shell");
            object shell = Activator.CreateInstance(type);
            object link = null;
            try
            {
                link = type.InvokeMember("CreateShortcut", BindingFlags.InvokeMethod, null, shell, new object[] { path });
                Type lt = link.GetType();
                lt.InvokeMember("TargetPath", BindingFlags.SetProperty, null, link, new object[] { exe });
                lt.InvokeMember("WorkingDirectory", BindingFlags.SetProperty, null, link, new object[] { Path.GetDirectoryName(exe) });
                lt.InvokeMember("IconLocation", BindingFlags.SetProperty, null, link, new object[] { exe + ",0" });
                lt.InvokeMember("Description", BindingFlags.SetProperty, null, link,
                                new object[] { Loc.T("Performances, vidéo et direct en VR", "VR performance, video and live streaming") });
                lt.InvokeMember("Save", BindingFlags.InvokeMethod, null, link, null);
            }
            finally
            {
                if (link != null) Marshal.FinalReleaseComObject(link);
                Marshal.FinalReleaseComObject(shell);
            }
        }
    }
}
