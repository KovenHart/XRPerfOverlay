// Langue des textes de XRPerfSensors (menu de l'icône, messages, état de la capture...) : français ou
// anglais. « auto » suit la langue de Windows : français pour un Windows en français, anglais sinon.

using System;
using System.Globalization;

namespace XRPerfSensors
{
    internal static class Loc
    {
        /// <summary>Choix enregistrés (Settings.Language et [overlay] language du .ini), dans l'ordre de la layer.</summary>
        public static readonly string[] Names = { "auto", "fr", "en" };

        /// <summary>Textes en anglais (sinon en français).</summary>
        public static bool English { get; private set; }

        /// <summary>Langue effective : "fr" ou "en".</summary>
        public static string Code
        {
            get { return English ? "en" : "fr"; }
        }

        /// <summary>Applique un choix ("auto", "fr" ou "en").</summary>
        public static void Set(string choice)
        {
            English = Resolve(choice) == "en";
        }

        /// <summary>"fr" ou "en" pour un choix donné ; "auto" (ou inconnu) suit la langue de Windows.</summary>
        public static string Resolve(string choice)
        {
            if (choice == "fr" || choice == "en") return choice;
            string windows = CultureInfo.CurrentUICulture.TwoLetterISOLanguageName;
            return string.Equals(windows, "fr", StringComparison.OrdinalIgnoreCase) ? "fr" : "en";
        }

        /// <summary>Indice du choix dans <see cref="Names"/> (0 = auto si inconnu).</summary>
        public static int IndexOf(string choice)
        {
            int index = Array.IndexOf(Names, choice);
            return index >= 0 ? index : 0;
        }

        /// <summary>Le texte dans la langue en cours.</summary>
        public static string T(string french, string english)
        {
            return English ? english : french;
        }
    }
}
