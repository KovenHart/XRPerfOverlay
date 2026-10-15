# Composants tiers

## En-têtes OpenXR (`layer/external/openxr/`)

Copyright 2017-2026 The Khronos Group Inc. — licence Apache-2.0 OU MIT (voir l'en-tête de chaque fichier).
Copiés depuis https://github.com/KhronosGroup/OpenXR-SDK (version 1.1.63).

## OpenVR (`layer/external/openvr/`)

En-tête `openvr.h` et bibliothèque `openvr_api.dll` (64 bits) d'OpenVR SDK 2.15.6, utilisés par XRPerfVR.
https://github.com/ValveSoftware/openvr — fichiers distribués sans modification.

```
Copyright (c) 2015, Valve Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors
may be used to endorse or promote products derived from this software without
specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## LibreHardwareMonitorLib

Bibliothèque de lecture des capteurs utilisée par `sensors/` (téléchargée par NuGet à la compilation, version 0.9.6).
Licence MPL-2.0 — https://github.com/LibreHardwareMonitor/LibreHardwareMonitor
Elle est distribuée telle quelle, sans modification ; son code source est disponible à l'adresse ci-dessus.

Les bibliothèques dont elle dépend (HidSharp, sous licence Apache-2.0, et les autres DLL du dossier
`sensors\` qui ne font pas partie de .NET) sont installées par NuGet et distribuées sous leur propre
licence, indiquée sur la page NuGet de chacune.

## .NET (paquet de distribution)

Le paquet de distribution contient le runtime .NET et Windows Forms dans `programme\sensors\`, pour que
XRPerfSensors fonctionne sans rien installer. Copyright (c) .NET Foundation and Contributors — licence MIT —
https://github.com/dotnet/runtime et https://github.com/dotnet/winforms

## PawnIO (non inclus)

Pilote facultatif, à installer séparément, qui donne accès à la température et à la fréquence du processeur.
Il n'est pas distribué avec XR Perf Overlay — https://pawnio.eu/

## Police DejaVu Sans Mono (`layer/src/font_data.h`)

Les glyphes du panneau sont des images pré-calculées à partir de DejaVu Sans Mono — https://dejavu-fonts.github.io/

Copyright (c) 2003 by Bitstream, Inc. All Rights Reserved. Bitstream Vera is a trademark of Bitstream, Inc.
DejaVu changes are in public domain.

```
Permission is hereby granted, free of charge, to any person obtaining a copy
of the fonts accompanying this license ("Fonts") and associated
documentation files (the "Font Software"), to reproduce and distribute the
Font Software, including without limitation the rights to use, copy, merge,
publish, distribute, and/or sell copies of the Font Software, and to permit
persons to whom the Font Software is furnished to do so, subject to the
following conditions:

The above copyright and trademark notices and this permission notice shall
be included in all copies of one or more of the Font Software typefaces.

The Font Software may be modified, altered, or added to, and in particular
the designs of glyphs or characters in the Fonts may be modified and
additional glyphs or characters may be added to the Fonts, only if the fonts
are renamed to names not containing either the words "Bitstream" or the word
"Vera".

This License becomes null and void to the extent applicable to Fonts or Font
Software that has been modified and is distributed under the "Bitstream
Vera" names.

The Font Software may be sold as part of a larger software package but no
copy of one or more of the Font Software typefaces may be sold by itself.

THE FONT SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO ANY WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF COPYRIGHT, PATENT,
TRADEMARK, OR OTHER RIGHT. IN NO EVENT SHALL BITSTREAM OR THE GNOME
FOUNDATION BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, INCLUDING
ANY GENERAL, SPECIAL, INDIRECT, INCIDENTAL, OR CONSEQUENTIAL DAMAGES,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
THE USE OR INABILITY TO USE THE FONT SOFTWARE OR FROM OTHER DEALINGS IN THE
FONT SOFTWARE.

Except as contained in this notice, the names of Gnome, the Gnome
Foundation, and Bitstream Inc., shall not be used in advertising or
otherwise to promote the sale, use or other dealings in this Font Software
without prior written authorization from the Gnome Foundation or Bitstream
Inc., respectively. For further information, contact: fonts at gnome dot
org.
```
