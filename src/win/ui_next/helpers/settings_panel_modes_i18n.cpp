#include "helpers/settings_panel_i18n.hpp"

#include <string>

namespace pmui::settings_panel_i18n {
namespace {

// Tooltips: English for all locales for now (localize in a later pass).
#define TT_TFORM_REFRESH  L"Reload model and collection lists from the service"
#define TT_TFORM_PRESETS  L"Open saved prompt presets"
#define TT_TFORM_API      L"Open provider API keys and credentials"
#define TT_TFORM_ADDREF   L"Pick an image as a reference (e.g. logo, brand sheet)"
#define TT_TFORM_CLEAR_R  L"Remove all images from the reference list"
#define TT_COMMON_REFRESH TT_TFORM_REFRESH
#define TT_FIND_ADDREF   TT_TFORM_ADDREF
#define TT_FIND_CLEAR    TT_TFORM_CLEAR_R
#define TT_FIND_API      L"Open provider API keys (same as Transform)"

static std::string norm(std::string_view v)
{
    std::string s(v);
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

template<typename T>
const T& pick(const T& en, const T& es, const T& de, const T& it, const T& fr, std::string_view lang)
{
    const std::string c = norm(lang);
    if (c == "es") return es;
    if (c == "de") return de;
    if (c == "it") return it;
    if (c == "fr") return fr;
    return en;
}

// ── Compress ───────────────────────────────────────────────────────────────

static const CompressStrings kCmpEn{
    L"Output",
    L"Destination:",
    L"Folder:",
    {L"Next to source", L"Next to source (_compressed)", L"Custom folder…"},
    L"Format",
    L"Compressor:",
    {L"PNG  (lossless)", L"MozJPEG  (JPEG)"},
    L"PNG compression",
    L"Level:",
    L"Quantise",
    L"Palette quantise (libimagequant)",
    L"Colors:",
    {L"256  (full palette)", L"128", L"64", L"32  (small files)"},
    L"Quality:",
    L"Advanced",
    L"Zopfli ultra-compress  (slow)",
    L"MozJPEG options",
    L"Quality:",
    L"Progressive JPEG",
    L"Trellis quantisation  (slower, smaller)",
    L"Options",
    L"Strip metadata on save",
    L"Choose compress output folder",
};

static const CompressStrings kCmpEs{
    L"Salida",
    L"Destino:",
    L"Carpeta:",
    {L"Junto al origen", L"Junto al origen (_compressed)", L"Carpeta personalizada…"},
    L"Formato",
    L"Compresor:",
    {L"PNG (sin pérdida)", L"MozJPEG (JPEG)"},
    L"Compresión PNG",
    L"Nivel:",
    L"Cuantización",
    L"Cuantización de paleta (libimagequant)",
    L"Colores:",
    {L"256 (paleta completa)", L"128", L"64", L"32 (archivos pequeños)"},
    L"Calidad:",
    L"Avanzado",
    L"Ultra-compresión Zopfli (lento)",
    L"Opciones MozJPEG",
    L"Calidad:",
    L"JPEG progresivo",
    L"Cuantización trellis (más lento, menor tamaño)",
    L"Opciones",
    L"Quitar metadatos al guardar",
    L"Elegir carpeta de salida (compresión)",
};

static const CompressStrings kCmpDe{
    L"Ausgabe",
    L"Ziel:",
    L"Ordner:",
    {L"Neben Quelle", L"Neben Quelle (_compressed)", L"Benutzerordner…"},
    L"Format",
    L"Kompressionsformat:",
    {L"PNG (verlustfrei)", L"MozJPEG (JPEG)"},
    L"PNG-Kompression",
    L"Level:",
    L"Quantisierung",
    L"Palettenquantisierung (libimagequant)",
    L"Farben:",
    {L"256 (volle Palette)", L"128", L"64", L"32 (kleine Dateien)"},
    L"Qualität:",
    L"Erweitert",
    L"Zopfli Ultra-Kompression (langsam)",
    L"MozJPEG-Optionen",
    L"Qualität:",
    L"Progressives JPEG",
    L"Trellis-Quantisierung (langsamer, kleiner)",
    L"Optionen",
    L"Metadaten beim Speichern entfernen",
    L"Komprimier-Ausgabeordner wählen",
};

static const CompressStrings kCmpIt{
    L"Output",
    L"Destinazione:",
    L"Cartella:",
    {L"Accanto alla sorgente", L"Accanto alla sorgente (_compressed)", L"Cartella personalizzata…"},
    L"Formato",
    L"Compressore:",
    {L"PNG (senza perdita)", L"MozJPEG (JPEG)"},
    L"Compressione PNG",
    L"Livello:",
    L"Quantizzazione",
    L"Quantizzazione palette (libimagequant)",
    L"Colori:",
    {L"256 (palette piena)", L"128", L"64", L"32 (file piccoli)"},
    L"Qualità:",
    L"Avanzate",
    L"Ultra-compressione Zopfli (lenta)",
    L"Opzioni MozJPEG",
    L"Qualità:",
    L"JPEG progressivo",
    L"Quantizzazione trellis (più lento, file minori)",
    L"Opzioni",
    L"Rimuovi metadati al salvataggio",
    L"Scegli cartella di output compressione",
};

static const CompressStrings kCmpFr{
    L"Sortie",
    L"Destination :",
    L"Dossier :",
    {L"À côté de la source", L"À côté de la source (_compressed)", L"Dossier personnalisé…"},
    L"Format",
    L"Compresseur :",
    {L"PNG (sans perte)", L"MozJPEG (JPEG)"},
    L"Compression PNG",
    L"Niveau :",
    L"Quantification",
    L"Quantification de palette (libimagequant)",
    L"Couleurs :",
    {L"256 (palette complète)", L"128", L"64", L"32 (petits fichiers)"},
    L"Qualité :",
    L"Avancé",
    L"Ultra-compression Zopfli (lent)",
    L"Options MozJPEG",
    L"Qualité :",
    L"JPEG progressif",
    L"Quantification treillis (plus lent, plus petit)",
    L"Options",
    L"Supprimer les métadonnées à l'enregistrement",
    L"Dossier de sortie compression",
};

// ── Transform ──────────────────────────────────────────────────────────────

static const TransformStrings kTfEn{
    L"Model",
    L"Provider:",
    L"Collection",
    L"Model:",
    {L"Gemini 3 Pro Image  (best)", L"Gemini 3.1 Flash Image  (fast)"},
    L"Output",
    L"Aspect:",
    {L"(auto)", L"1:1   Square", L"3:2", L"4:3", L"16:9  Wide", L"9:16  Portrait"},
    L"Size:",
    {L"(default)", L"512", L"1K", L"2K", L"4K"},
    L"Prompt",
    L"Presets…",
    L"API Keys…",
    L"References (logo / brand sheet)",
    L"+ Add reference…",
    L"Clear",
    L"Only for camera RAW / HEIC",
    TT_TFORM_REFRESH,
    TT_TFORM_PRESETS,
    TT_TFORM_API,
    TT_TFORM_ADDREF,
    TT_TFORM_CLEAR_R,
};

static const TransformStrings kTfEs{
    L"Modelo",
    L"Proveedor:",
    L"Colección",
    L"Modelo:",
    {L"Gemini 3 Pro Image (mejor)", L"Gemini 3.1 Flash Image (rápido)"},
    L"Salida",
    L"Aspecto:",
    {L"(auto)", L"1:1   Cuadrado", L"3:2", L"4:3", L"16:9   Panorámico", L"9:16   Vertical"},
    L"Tamaño:",
    {L"(predeterminado)", L"512", L"1K", L"2K", L"4K"},
    L"Instrucciones",
    L"Valores predefinidos…",
    L"Claves API…",
    L"Referencias (logo / marca)",
    L"+ Añadir referencia…",
    L"Borrar",
    L"Solo RAW/HEIC de cámara",
    TT_TFORM_REFRESH,
    TT_TFORM_PRESETS,
    TT_TFORM_API,
    TT_TFORM_ADDREF,
    TT_TFORM_CLEAR_R,
};

static const TransformStrings kTfDe{
    L"Modell",
    L"Anbieter:",
    L"Sammlung",
    L"Modell:",
    {L"Gemini 3 Pro Image (beste)", L"Gemini 3.1 Flash Image (schnell)"},
    L"Ausgabe",
    L"Seitenverhältnis:",
    {L"(auto)", L"1:1   Quadrat", L"3:2", L"4:3", L"16:9   Breit", L"9:16   Hoch"},
    L"Größe:",
    {L"(Standard)", L"512", L"1K", L"2K", L"4K"},
    L"Aufforderung",
    L"Voreinstellungen…",
    L"API-Schlüssel…",
    L"Referenzen (Logo / Branding)",
    L"+ Referenz hinzufügen…",
    L"Leeren",
    L"Nur Kamera-RAW / HEIC",
    TT_TFORM_REFRESH,
    TT_TFORM_PRESETS,
    TT_TFORM_API,
    TT_TFORM_ADDREF,
    TT_TFORM_CLEAR_R,
};

static const TransformStrings kTfIt{
    L"Modello",
    L"Provider:",
    L"Raccolta",
    L"Modello:",
    {L"Gemini 3 Pro Image (migliore)", L"Gemini 3.1 Flash Image (veloce)"},
    L"Output",
    L"Proporzioni:",
    {L"(auto)", L"1:1   Quadrato", L"3:2", L"4:3", L"16:9   Wide", L"9:16   Verticale"},
    L"Dimensione:",
    {L"(predefinita)", L"512", L"1K", L"2K", L"4K"},
    L"Prompt",
    L"Preset…",
    L"Chiavi API…",
    L"Riferimenti (logo / brand)",
    L"+ Aggiungi riferimento…",
    L"Pulisci",
    L"Solo RAW/HEIC fotocamera",
    TT_TFORM_REFRESH,
    TT_TFORM_PRESETS,
    TT_TFORM_API,
    TT_TFORM_ADDREF,
    TT_TFORM_CLEAR_R,
};

static const TransformStrings kTfFr{
    L"Modèle",
    L"Fournisseur :",
    L"Collection",
    L"Modèle :",
    {L"Gemini 3 Pro Image (meilleur)", L"Gemini 3.1 Flash Image (rapide)"},
    L"Sortie",
    L"Aspect :",
    {L"(auto)", L"1:1   Carré", L"3:2", L"4:3", L"16:9   Large", L"9:16   Portrait"},
    L"Taille :",
    {L"(défaut)", L"512", L"1K", L"2K", L"4K"},
    L"Invite",
    L"Préréglages…",
    L"Clés API…",
    L"Références (logo / charte)",
    L"+ Ajouter une référence…",
    L"Effacer",
    L"Uniquement RAW / HEIC (appareil photo)",
    TT_TFORM_REFRESH,
    TT_TFORM_PRESETS,
    TT_TFORM_API,
    TT_TFORM_ADDREF,
    TT_TFORM_CLEAR_R,
};

// ── Meta ───────────────────────────────────────────────────────────────────

static const MetaStrings kMetaEn{
    L"Output",
    L"Folder:",
    L"Write",
    L"Markdown description (.md)",
    L"JSON (.json, includes EXIF)",
    L"Update EXIF ImageDescription",
    L"Pre-resize",
    L"Resize first (in memory) — faster, cheaper",
    L"Width:",
    {L"256", L"512  (recommended)", L"768", L"1024"},
    L"Provider",
    L"Provider:",
    L"Collection",
    L"Google",
    L"Model:",
    {L"gemini-3-pro-image-preview  (fast, cheap)", L"gemini-2.5-pro    (best quality)"},
    L"Prompt",
    L"Preset:",
    {L"Default cataloguer", L"Ecommerce product", L"Photo journal", L"(custom)"},
    L"API Keys…",
    L"Choose meta output folder",
    TT_COMMON_REFRESH,
};

static const MetaStrings kMetaEs{
    L"Salida",
    L"Carpeta:",
    L"Generar",
    L"Descripción Markdown (.md)",
    L"JSON (.json, incluye EXIF)",
    L"Actualizar ImageDescription EXIF",
    L"Pre-redimensionado",
    L"Redimensionar primero (en memoria): más rápido y barato",
    L"Ancho:",
    {L"256", L"512 (recomendado)", L"768", L"1024"},
    L"Proveedor",
    L"Proveedor:",
    L"Colección",
    L"Google",
    L"Modelo:",
    {L"gemini-3-pro-image-preview (rápido, barato)", L"gemini-2.5-pro (mejor calidad)"},
    L"Instrucciones",
    L"Plantilla:",
    {L"Catálogo por defecto", L"Producto ecommerce", L"Diario fotográfico", L"(personalizado)"},
    L"Claves API…",
    L"Elegir carpeta de salida meta",
    TT_COMMON_REFRESH,
};

static const MetaStrings kMetaDe{
    L"Ausgabe",
    L"Ordner:",
    L"Ausgabe",
    L"Markdown-Beschreibung (.md)",
    L"JSON (.json, mit EXIF)",
    L"EXIF ImageDescription aktualisieren",
    L"Vorab-Skalierung",
    L"Zuerst skalieren (im RAM) — schneller, günstiger",
    L"Breite:",
    {L"256", L"512 (empfohlen)", L"768", L"1024"},
    L"Anbieter",
    L"Anbieter:",
    L"Sammlung",
    L"Google",
    L"Modell:",
    {L"gemini-3-pro-image-preview (schnell, günstig)", L"gemini-2.5-pro (beste Qualität)"},
    L"Aufforderung",
    L"Voreinstellung:",
    {L"Standard-Katalogisierung", L"E-Commerce-Produkt", L"Fotojournal", L"(benutzerdefiniert)"},
    L"API-Schlüssel…",
    L"Meta-Ausgabeordner wählen",
    TT_COMMON_REFRESH,
};

static const MetaStrings kMetaIt{
    L"Output",
    L"Cartella:",
    L"Scrittura",
    L"Descrizione Markdown (.md)",
    L"JSON (.json, include EXIF)",
    L"Aggiorna ImageDescription EXIF",
    L"Pre-ridimensionamento",
    L"Ridimensiona prima (in memoria): più veloce ed economico",
    L"Larghezza:",
    {L"256", L"512 (consigliato)", L"768", L"1024"},
    L"Provider",
    L"Provider:",
    L"Raccolta",
    L"Google",
    L"Modello:",
    {L"gemini-3-pro-image-preview (veloce, economico)", L"gemini-2.5-pro (migliore qualità)"},
    L"Prompt",
    L"Preset:",
    {L"Catalogazione predefinita", L"Prodotto e-commerce", L"Diario fotografico", L"(personalizzato)"},
    L"Chiavi API…",
    L"Scegli cartella output meta",
    TT_COMMON_REFRESH,
};

static const MetaStrings kMetaFr{
    L"Sortie",
    L"Dossier :",
    L"Écriture",
    L"Description Markdown (.md)",
    L"JSON (.json, inclut EXIF)",
    L"Mettre à jour EXIF ImageDescription",
    L"Pré-redimensionnement",
    L"Redimensionner d'abord (en mémoire) — plus rapide",
    L"Largeur :",
    {L"256", L"512 (recommandé)", L"768", L"1024"},
    L"Fournisseur",
    L"Fournisseur :",
    L"Collection",
    L"Google",
    L"Modèle :",
    {L"gemini-3-pro-image-preview (rapide)", L"gemini-2.5-pro (meilleure qualité)"},
    L"Invite",
    L"Préréglage :",
    {L"Catalogage par défaut", L"Produit e-commerce", L"Journal photo", L"(personnalisé)"},
    L"Clés API…",
    L"Dossier de sortie méta",
    TT_COMMON_REFRESH,
};

// ── Find ───────────────────────────────────────────────────────────────────

static const FindStrings kFindEn{
    L"Search query",
    L"Mode",
    L"Use LLM (semantic match)",
    L"Recurse into subfolders",
    L"Name: also match parent folder names",
    L"LLM cache",
    L"Bypass cache (re-describe)",
    L"Don't generate missing meta",
    L"Read sidecar .md",
    L"Read sidecar .json",
    L"Read libvips EXIF",
    L"LLM",
    L"Model:",
    {L"gemini-3-pro-image-preview  (fast, cheap)", L"gemini-2.5-pro    (best quality)"},
    L"Resize:",
    {L"256", L"512  (recommended)", L"768", L"1024"},
    L"Pre-resize in memory (longest edge, before LLM calls)",
    L"Max:",
    L"References (LLM)",
    L"+ Add reference…",
    L"Clear",
    L"API Keys…",
    TT_COMMON_REFRESH,
    TT_FIND_ADDREF,
    TT_FIND_CLEAR,
    TT_FIND_API,
};

static const FindStrings kFindEs{
    L"Búsqueda",
    L"Modo",
    L"Usar LLM (coincidencia semántica)",
    L"Incluir subcarpetas",
    L"Nombre: coincidir también carpetas superiores",
    L"Caché LLM",
    L"Omitir caché (volver a describir)",
    L"No generar meta faltante",
    L"Leer .md anexo",
    L"Leer .json anexo",
    L"Leer EXIF libvips",
    L"LLM",
    L"Modelo:",
    {L"gemini-3-pro-image-preview (rápido, barato)", L"gemini-2.5-pro (mejor calidad)"},
    L"Redimensionado:",
    {L"256", L"512 (recomendado)", L"768", L"1024"},
    L"Pre-redimensionar en memoria (lado largo, antes de LLM)",
    L"Máx.:",
    L"Referencias (LLM)",
    L"+ Añadir referencia…",
    L"Borrar",
    L"Claves API…",
    TT_COMMON_REFRESH,
    TT_FIND_ADDREF,
    TT_FIND_CLEAR,
    TT_FIND_API,
};

static const FindStrings kFindDe{
    L"Suchanfrage",
    L"Modus",
    L"LLM nutzen (semantische Übereinstimmung)",
    L"Unterordner einbeziehen",
    L"Name: auch übergeordnete Ordner",
    L"LLM-Cache",
    L"Cache ignorieren (neu beschreiben)",
    L"Fehlende Meta nicht erzeugen",
    L"Sidecar .md lesen",
    L"Sidecar .json lesen",
    L"libvips-EXIF lesen",
    L"LLM",
    L"Modell:",
    {L"gemini-3-pro-image-preview (schnell, günstig)", L"gemini-2.5-pro (beste Qualität)"},
    L"Skalierung:",
    {L"256", L"512 (empfohlen)", L"768", L"1024"},
    L"Im Speicher vorab skalieren (lange Kante, vor LLM)",
    L"Max.:",
    L"Referenzen (LLM)",
    L"+ Referenz hinzufügen…",
    L"Leeren",
    L"API-Schlüssel…",
    TT_COMMON_REFRESH,
    TT_FIND_ADDREF,
    TT_FIND_CLEAR,
    TT_FIND_API,
};

static const FindStrings kFindIt{
    L"Query di ricerca",
    L"Modalità",
    L"Usa LLM (corrispondenza semantica)",
    L"Sottocartelle ricorsive",
    L"Nome: includi anche cartelle superiori",
    L"Cache LLM",
    L"Ignora cache (nuova descrizione)",
    L"Non generare meta mancanti",
    L"Leggi sidecar .md",
    L"Leggi sidecar .json",
    L"Leggi EXIF libvips",
    L"LLM",
    L"Modello:",
    {L"gemini-3-pro-image-preview (veloce)", L"gemini-2.5-pro (migliore qualità)"},
    L"Ridimensiona:",
    {L"256", L"512 (consigliato)", L"768", L"1024"},
    L"Pre-riduci in memoria (lato lungo, prima LLM)",
    L"Max:",
    L"Riferimenti (LLM)",
    L"+ Aggiungi riferimento…",
    L"Pulisci",
    L"Chiavi API…",
    TT_COMMON_REFRESH,
    TT_FIND_ADDREF,
    TT_FIND_CLEAR,
    TT_FIND_API,
};

static const FindStrings kFindFr{
    L"Requête",
    L"Mode",
    L"Utiliser un LLM (sémantique)",
    L"Sous-dossiers récursifs",
    L"Nom : dossiers parents aussi",
    L"Cache LLM",
    L"Ignorer le cache (re-décrire)",
    L"Ne pas générer la méta manquante",
    L"Lire sidecar .md",
    L"Lire sidecar .json",
    L"Lire EXIF libvips",
    L"LLM",
    L"Modèle :",
    {L"gemini-3-pro-image-preview (rapide)", L"gemini-2.5-pro (meilleure qualité)"},
    L"Redimensionnement :",
    {L"256", L"512 (recommandé)", L"768", L"1024"},
    L"Pré-redimensionner en mémoire (grand côté, avant LLM)",
    L"Max :",
    L"Références (LLM)",
    L"+ Ajouter une référence…",
    L"Effacer",
    L"Clés API…",
    TT_COMMON_REFRESH,
    TT_FIND_ADDREF,
    TT_FIND_CLEAR,
    TT_FIND_API,
};

} // namespace

const CompressStrings& compress_strings_for(std::string_view display_language)
{
    return pick(kCmpEn, kCmpEs, kCmpDe, kCmpIt, kCmpFr, display_language);
}

const TransformStrings& transform_strings_for(std::string_view display_language)
{
    return pick(kTfEn, kTfEs, kTfDe, kTfIt, kTfFr, display_language);
}

const MetaStrings& meta_strings_for(std::string_view display_language)
{
    return pick(kMetaEn, kMetaEs, kMetaDe, kMetaIt, kMetaFr, display_language);
}

const FindStrings& find_strings_for(std::string_view display_language)
{
    return pick(kFindEn, kFindEs, kFindDe, kFindIt, kFindFr, display_language);
}

} // namespace pmui::settings_panel_i18n
