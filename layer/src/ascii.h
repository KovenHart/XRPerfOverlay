// Réduction d'un texte aux caractères que le panneau sait dessiner (ASCII imprimable).
// Code portable, testé dans tests/preview.cpp.
#pragma once

#include <cstddef>
#include <initializer_list>
#include <string>
#include <type_traits>

namespace xrpo {

// `text` : chaîne terminée par zéro ou longue de `capacity` caractères au plus (UTF-8 ou UTF-16).
// Les caractères hors ASCII sont retirés, les caractères de contrôle et les espaces répétés réduits
// à un seul espace, et le résultat limité à `maxLength` caractères.
template <typename Char>
std::string AsciiText(const Char* text, size_t capacity, size_t maxLength = 47) {
    std::string out;
    if (!text) return out;
    for (size_t i = 0; i < capacity && text[i] != 0 && out.size() < maxLength; ++i) {
        auto code = static_cast<typename std::make_unsigned<Char>::type>(text[i]);
        if (code < 32) code = 32;  // tabulation, retour à la ligne : un espace
        if (code > 126) continue;
        if (code == 32 && (out.empty() || out.back() == ' ')) continue;
        out.push_back(static_cast<char>(code));
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

// Nom de modèle de processeur ou de carte graphique, tel que Windows le donne, rendu lisible :
// "Intel(R) Core(TM) i9-13900K" -> "Intel Core i9-13900K". Les marques (R), (TM), (C) sont retirées
// et les espaces réduits ; le reste est gardé tel quel.
inline std::string ModelName(const std::string& ascii) {
    std::string text = ascii;
    for (const char* mark : {"(R)", "(r)", "(TM)", "(tm)", "(Tm)", "(C)", "(c)"}) {
        const std::string m(mark);
        for (size_t at = text.find(m); at != std::string::npos; at = text.find(m, at)) text.erase(at, m.size());
    }
    std::string out;
    for (char c : text) {
        if (c == ' ' && (out.empty() || out.back() == ' ')) continue;
        out.push_back(c);
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

}  // namespace xrpo
