#include "helpers/dock_chrome_i18n.hpp"

#include <string>

namespace pmui::dock_chrome_i18n {
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

static const Strings kEn{
    L"Chat",
    L"Chat",
    L"Files",
    L"Explorer",
    L"Queue",
    L"File Queue",
    L"Log",
    L"Log",
    L"File Info",
    L"File Info",
    L"Find",
    L"Find Results",
    L"Duplicates",
    L"Duplicate groups",
    L"Nodes",
    L"Nodes",
    L"Copy",
    L"Copy the entire log to the clipboard",
    L"Clear",
    L"Clear the log",
    L"Preview",
    L"Preview",
};

static const Strings kEs{
    L"Chat",
    L"Chat",
    L"Archivos",
    L"Explorador",
    L"Cola",
    L"Cola de archivos",
    L"Registro",
    L"Registro",
    L"Info del archivo",
    L"Info del archivo",
    L"Buscar",
    L"Resultados",
    L"Duplicados",
    L"Grupos duplicados",
    L"Nodos",
    L"Nodos",
    L"Copiar",
    L"Copiar todo el registro al portapapeles",
    L"Vaciar",
    L"Vaciar el registro",
    L"Vista previa",
    L"Vista previa",
};

static const Strings kDe{
    L"Chat",
    L"Chat",
    L"Dateien",
    L"Explorer",
    L"Warteschlange",
    L"Dateiwarteschlange",
    L"Protokoll",
    L"Protokoll",
    L"Dateiinfo",
    L"Dateiinfo",
    L"Suche",
    L"Suchergebnisse",
    L"Duplikate",
    L"Duplikatgruppen",
    L"Knoten",
    L"Knoten",
    L"Kopieren",
    L"Gesamtes Protokoll in die Zwischenablage kopieren",
    L"Leeren",
    L"Protokoll leeren",
    L"Vorschau",
    L"Vorschau",
};

static const Strings kIt{
    L"Chat",
    L"Chat",
    L"File",
    L"Esplora risorse",
    L"Coda",
    L"Coda file",
    L"Log",
    L"Log",
    L"Info file",
    L"Info file",
    L"Cerca",
    L"Risultati ricerca",
    L"Duplicati",
    L"Gruppi duplicati",
    L"Nodi",
    L"Nodi",
    L"Copia",
    L"Copia l'intero registro negli appunti",
    L"Svuota",
    L"Svuota il registro",
    L"Anteprima",
    L"Anteprima",
};

static const Strings kFr{
    L"Chat",
    L"Chat",
    L"Fichiers",
    L"Explorateur",
    L"File",
    L"File d'attente",
    L"Journal",
    L"Journal",
    L"Infos fichier",
    L"Infos fichier",
    L"Recherche",
    L"Résultats",
    L"Doublons",
    L"Groupes de doublons",
    L"Nœuds",
    L"Nœuds",
    L"Copier",
    L"Copier tout le journal dans le presse-papiers",
    L"Vider",
    L"Vider le journal",
    L"Aperçu",
    L"Aperçu",
};

} // namespace

const Strings& strings_for(std::string_view display_language)
{
    const std::string c = norm(display_language);
    if (c == "es")
        return kEs;
    if (c == "de")
        return kDe;
    if (c == "it")
        return kIt;
    if (c == "fr")
        return kFr;
    return kEn;
}

} // namespace pmui::dock_chrome_i18n
