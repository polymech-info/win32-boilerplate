#include "helpers/settings_panel_i18n.hpp"

#include <string>

namespace pmui::settings_panel_i18n {
namespace {

static std::string norm(std::string_view v)
{
    std::string s(v);
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

static const DockStrings kDockEn{
    L"Settings",
    L"Resize Settings",
    L"Compress Settings",
    L"Meta Settings",
    L"Transform Settings",
    L"Find Settings",
    L"Duplicates Settings",
    L"Resize",
    L"Compress",
    L"Meta",
    L"Transform",
    L"Find",
    L"Duplicates",
};

static const DockStrings kDockEs{
    L"Ajustes",
    L"Ajustes de redimensionado",
    L"Ajustes de compresión",
    L"Ajustes meta",
    L"Ajustes de transformación",
    L"Ajustes de búsqueda",
    L"Ajustes de duplicados",
    L"Redimensionar",
    L"Comprimir",
    L"Meta",
    L"Transformar",
    L"Buscar",
    L"Duplicados",
};

static const DockStrings kDockDe{
    L"Einstellungen",
    L"Größenänderung",
    L"Komprimierung",
    L"Meta",
    L"Transform",
    L"Suche",
    L"Duplikate",
    L"Größe",
    L"Komprimieren",
    L"Meta",
    L"Transform",
    L"Suche",
    L"Duplikate",
};

static const DockStrings kDockIt{
    L"Impostazioni",
    L"Ridimensionamento",
    L"Compressione",
    L"Meta",
    L"Transform",
    L"Ricerca",
    L"Duplicati",
    L"Dimensioni",
    L"Comprimi",
    L"Meta",
    L"Trasforma",
    L"Cerca",
    L"Duplicati",
};

static const DockStrings kDockFr{
    L"Paramètres",
    L"Redimensionnement",
    L"Compression",
    L"Méta",
    L"Transform.",
    L"Recherche",
    L"Doublons",
    L"Taille",
    L"Compresser",
    L"Méta",
    L"Transformer",
    L"Rechercher",
    L"Doublons",
};

#define RES_PRESETS_EN                                                                                     \
    L"(custom)", L"1920 × 1080   Full HD", L"1280 × 720    HD", L"1024 × 768", L"800 × 600",                \
        L"1080 × 1080   Square", L"1200 × 628    OG / FB", L"1080 × 1920   Story", L"3840 × 2160   4K UHD", \
        L"2560 × 1440   2K QHD", L"2000px  long edge", L"1500px  long edge", L"1200px  long edge",         \
        L"800px   long edge", L"400px   long edge", L"512 × 512", L"256 × 256", L"128 × 128"

#define RATIO_PRESETS_EN                                                                                   \
    L"(original / none)", L"1:1    Square", L"16:9   Wide", L"9:16   Portrait", L"4:3    Classic",         \
        L"3:4    Portrait", L"3:2", L"2:3", L"21:9   Ultra-wide", L"5:4    Square-ish"

static const ResizeStrings kResizeEn{
    L"Dimensions",
    L"Output",
    L"Quality",
    L"Options",
    L"Resolution:",
    L"Ratio:",
    L"Size:",
    L"Width:",
    L"H:",
    L"Fit:",
    L"Destination:",
    L"Folder:",
    L"Format:",
    L"Kernel:",
    L"EXIF autorotate",
    L"Allow enlargement",
    L"Strip metadata on save",
    L"Choose output folder",
    L"Error",
    {RES_PRESETS_EN},
    {RATIO_PRESETS_EN},
    {L"inside", L"cover", L"contain", L"fill", L"outside"},
    {L"Next to source", L"Next to source (_resized)", L"Custom folder…"},
    {L"auto (from path / template)", L"JPEG  (.jpg)", L"PNG   (.png)", L"WebP  (.webp)", L"TIFF  (.tiff)",
        L"AVIF  (.avif)"},
    {L"lanczos3  (best quality)", L"mitchell  (balanced)", L"lanczos2  (faster)", L"bicubic",
        L"nearest   (fastest)"},
};

static const ResizeStrings kResizeEs{
    L"Dimensiones",
    L"Salida",
    L"Calidad",
    L"Opciones",
    L"Resolución:",
    L"Proporción:",
    L"Tamaño:",
    L"Ancho:",
    L"A:",
    L"Ajuste:",
    L"Destino:",
    L"Carpeta:",
    L"Formato:",
    L"Núcleo:",
    L"Autorrotar EXIF",
    L"Permitir ampliar",
    L"Quitar metadatos al guardar",
    L"Elegir carpeta de salida",
    L"Error",
    {L"(personalizado)",
        L"1920 × 1080   Full HD",
        L"1280 × 720    HD",
        L"1024 × 768",
        L"800 × 600",
        L"1080 × 1080   Cuadrado",
        L"1200 × 628    OG / FB",
        L"1080 × 1920   Historia",
        L"3840 × 2160   4K UHD",
        L"2560 × 1440   2K QHD",
        L"2000 px  borde largo",
        L"1500 px  borde largo",
        L"1200 px  borde largo",
        L"800 px   borde largo",
        L"400 px   borde largo",
        L"512 × 512",
        L"256 × 256",
        L"128 × 128"},
    {L"(original / ninguna)", L"1:1    Cuadrado", L"16:9   Panorámico", L"9:16   Vertical", L"4:3    Clásico",
        L"3:4    Vertical", L"3:2", L"2:3", L"21:9   Ultrapanorámico", L"5:4    Casi cuadrado"},
    {L"interior", L"cubrir", L"contener", L"rellenar", L"exterior"},
    {L"Junto al origen", L"Junto al origen (_resized)", L"Carpeta personalizada…"},
    {L"auto (ruta / plantilla)", L"JPEG  (.jpg)", L"PNG   (.png)", L"WebP  (.webp)", L"TIFF  (.tiff)",
        L"AVIF  (.avif)"},
    {L"lanczos3  (mejor calidad)", L"mitchell  (equilibrado)", L"lanczos2  (más rápido)", L"bicúbico",
        L"nearest  (más rápido)"},
};

static const ResizeStrings kResizeDe{
    L"Abmessungen",
    L"Ausgabe",
    L"Qualität",
    L"Optionen",
    L"Auflösung:",
    L"Verhältnis:",
    L"Größe:",
    L"Breite:",
    L"H:",
    L"Einpassen:",
    L"Ziel:",
    L"Ordner:",
    L"Format:",
    L"Kernel:",
    L"EXIF automatisch drehen",
    L"Vergrößerung erlauben",
    L"Metadaten beim Speichern entfernen",
    L"Ausgabeordner wählen",
    L"Fehler",
    {L"(benutzerdefiniert)",
        L"1920 × 1080   Full HD",
        L"1280 × 720    HD",
        L"1024 × 768",
        L"800 × 600",
        L"1080 × 1080   Quadrat",
        L"1200 × 628    OG / FB",
        L"1080 × 1920   Story",
        L"3840 × 2160   4K UHD",
        L"2560 × 1440   2K QHD",
        L"2000 px  lange Kante",
        L"1500 px  lange Kante",
        L"1200 px  lange Kante",
        L"800 px   lange Kante",
        L"400 px   lange Kante",
        L"512 × 512",
        L"256 × 256",
        L"128 × 128"},
    {L"(Original / keines)", L"1:1    Quadrat", L"16:9   Breit", L"9:16   Hoch", L"4:3    Klassisch",
        L"3:4    Hochformat", L"3:2", L"2:3", L"21:9   Ultrabreit", L"5:4    Quadratisch"},
    {L"innen", L"cover", L"contain", L"füllen", L"außen"},
    {L"Neben Quelle", L"Neben Quelle (_resized)", L"Benutzerordner…"},
    {L"auto (Pfad / Vorlage)", L"JPEG  (.jpg)", L"PNG   (.png)", L"WebP  (.webp)", L"TIFF  (.tiff)",
        L"AVIF  (.avif)"},
    {L"lanczos3  (beste Qualität)", L"mitchell  (ausgewogen)", L"lanczos2  (schneller)", L"bikubisch",
        L"nearest  (am schnellsten)"},
};

static const ResizeStrings kResizeIt{
    L"Dimensioni",
    L"Output",
    L"Qualità",
    L"Opzioni",
    L"Risoluzione:",
    L"Proporzioni:",
    L"Dimensione:",
    L"Larghezza:",
    L"H:",
    L"Adatta:",
    L"Destinazione:",
    L"Cartella:",
    L"Formato:",
    L"Kernel:",
    L"Autorotazione EXIF",
    L"Consenti ingrandimento",
    L"Rimuovi metadati al salvataggio",
    L"Scegli cartella di output",
    L"Errore",
    {L"(personalizzato)",
        L"1920 × 1080   Full HD",
        L"1280 × 720    HD",
        L"1024 × 768",
        L"800 × 600",
        L"1080 × 1080   Quadrato",
        L"1200 × 628    OG / FB",
        L"1080 × 1920   Storia",
        L"3840 × 2160   4K UHD",
        L"2560 × 1440   2K QHD",
        L"2000 px  lato lungo",
        L"1500 px  lato lungo",
        L"1200 px  lato lungo",
        L"800 px   lato lungo",
        L"400 px   lato lungo",
        L"512 × 512",
        L"256 × 256",
        L"128 × 128"},
    {L"(originale / nessuno)", L"1:1    Quadrato", L"16:9   Wide", L"9:16   Verticale", L"4:3    Classico",
        L"3:4    Verticale", L"3:2", L"2:3", L"21:9   Ultrawide", L"5:4    Quasi quadrato"},
    {L"inside", L"cover", L"contain", L"fill", L"outside"},
    {L"Accanto alla sorgente", L"Accanto alla sorgente (_resized)", L"Cartella personalizzata…"},
    {L"auto (percorso / modello)", L"JPEG  (.jpg)", L"PNG   (.png)", L"WebP  (.webp)", L"TIFF  (.tiff)",
        L"AVIF  (.avif)"},
    {L"lanczos3  (migliore)", L"mitchell  (bilanciato)", L"lanczos2  (più veloce)", L"bicubico",
        L"nearest  (veloce)"},
};

static const ResizeStrings kResizeFr{
    L"Dimensions",
    L"Sortie",
    L"Qualité",
    L"Options",
    L"Résolution :",
    L"Rapport :",
    L"Taille :",
    L"Largeur :",
    L"H :",
    L"Ajuster :",
    L"Destination :",
    L"Dossier :",
    L"Format :",
    L"Noyau :",
    L"Rotation EXIF auto",
    L"Autoriser l'agrandissement",
    L"Supprimer les métadonnées à l'enregistrement",
    L"Dossier de sortie",
    L"Erreur",
    {L"(personnalisé)",
        L"1920 × 1080   Full HD",
        L"1280 × 720    HD",
        L"1024 × 768",
        L"800 × 600",
        L"1080 × 1080   Carré",
        L"1200 × 628    OG / FB",
        L"1080 × 1920   Story",
        L"3840 × 2160   4K UHD",
        L"2560 × 1440   2K QHD",
        L"2000 px  grand côté",
        L"1500 px  grand côté",
        L"1200 px  grand côté",
        L"800 px   grand côté",
        L"400 px   grand côté",
        L"512 × 512",
        L"256 × 256",
        L"128 × 128"},
    {L"(original / aucun)", L"1:1    Carré", L"16:9   Large", L"9:16   Portrait", L"4:3    Classique",
        L"3:4    Portrait", L"3:2", L"2:3", L"21:9   Ultra-large", L"5:4    Presque carré"},
    {L"inside", L"cover", L"contain", L"fill", L"outside"},
    {L"À côté de la source", L"À côté de la source (_resized)", L"Dossier personnalisé…"},
    {L"auto (chemin / modèle)", L"JPEG  (.jpg)", L"PNG   (.png)", L"WebP  (.webp)", L"TIFF  (.tiff)",
        L"AVIF  (.avif)"},
    {L"lanczos3  (meilleure qualité)", L"mitchell  (équilibré)", L"lanczos2  (plus rapide)", L"bicubique",
        L"nearest  (le plus rapide)"},
};

} // namespace

const DockStrings& dock_strings_for(std::string_view display_language)
{
    const std::string c = norm(display_language);
    if (c == "es")
        return kDockEs;
    if (c == "de")
        return kDockDe;
    if (c == "it")
        return kDockIt;
    if (c == "fr")
        return kDockFr;
    return kDockEn;
}

const ResizeStrings& resize_strings_for(std::string_view display_language)
{
    const std::string c = norm(display_language);
    if (c == "es")
        return kResizeEs;
    if (c == "de")
        return kResizeDe;
    if (c == "it")
        return kResizeIt;
    if (c == "fr")
        return kResizeFr;
    return kResizeEn;
}

const wchar_t* browse_button_caption(std::string_view display_language)
{
    const std::string c = norm(display_language);
    if (c == "es")
        return L"Examinar…";
    if (c == "de")
        return L"Durchsuchen…";
    if (c == "it")
        return L"Sfoglia…";
    if (c == "fr")
        return L"Parcourir…";
    return L"Browse…";
}

} // namespace pmui::settings_panel_i18n
